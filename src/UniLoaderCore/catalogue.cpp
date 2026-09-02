// Reading the package's Plugins/ folder as a catalogue.
//
// A plugin is one folder. Its .w2p files are what the game loads, its info.txt
// is what it says about itself, and its PNGs are the screenshots the client
// runs as a slideshow. The author writes plain files in a folder; nobody has to
// learn a format, and nothing has to be kept in step with a list somewhere else.
// A base/ folder at the package root describes the mod itself the same way.
//
// Names look like "0_basegame", "1_DAIFE", "2_Insane", "3_Legacy of Dalaran".
// The number is the order dannyldd wants them read in, so it is the order they
// are shown in, and it is not part of the name on screen.
//
// The catalogue is built from a list of paths rather than by walking a disk:
// the host walks, this decides what the walk means. That is what lets the
// grouping, the ordering and the description parsing be tested on any machine.

#include "uniloader/uniloader.h"

#include "util.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <new>
#include <string>
#include <vector>

namespace ul {

struct Plugin {
  std::string id;             // the folder name, exactly as it appears
  std::string name;           // what to show: info.txt's first line, or the folder
  std::string description;
  // The watchable YouTube things found in the info.txt: videos, and playlists
  // (which have a list id and no video id), in the order the author wrote them.
  std::vector<YouTubeItem> videos;
  mutable std::vector<std::string> video_urls;   // built on demand, see the getter
  mutable std::vector<std::string> video_embeds;
  bool has_info = false;
  // The .w2p files, which are alternatives rather than a set: most plugins
  // hold one, and the ones that hold two or three are difficulty settings.
  std::vector<std::string> variants;  // package-relative, in name order
  // Everything else in the folder: the author's notes, a link to the original
  // thread, a zip of the files a sub-mod started from. Kept so the catalogue
  // accounts for every file it saw, never installed.
  std::vector<std::string> notes;
  std::vector<std::string> images;  // package-relative, in slideshow order
  std::vector<std::string> variant_names;
  long long order = -1;             // the folder's numeric prefix, -1 if none
};

}  // namespace ul

struct ul_catalogue {
  std::vector<ul::Plugin> plugins;
  std::map<std::string, size_t> by_id;   // lowercased id -> index
  // The base/ folder, when the package has one: the mod's own description,
  // screenshots and videos. Not a plugin — nothing in it is ever installed as
  // one — but the same shape, because the author writes it the same way.
  ul::Plugin base;
  bool finished = false;
};

namespace ul {
namespace {

bool IsImage(const std::string& path) {
  const std::string extension = Extension(path);
  // PNG is what the packages use. The others cost nothing to accept and mean a
  // screenshot saved from a phone still shows up rather than silently not.
  return extension == "png" || extension == "jpg" || extension == "jpeg" ||
         extension == "bmp" || extension == "gif";
}

/// The one file this reads: the folder's read-me, read as prose. It was once a
/// three-field `plugin.txt` format instead, with info.txt read as fields
/// before that — which produced a list of plugins called "mod originally taken
/// from:", because these files are written for people. Prose it is, then:
/// shown whole, mined for video links, never split into fields.
bool IsInfo(const std::string& path) {
  return EqualsNoCase(BaseName(path), "info.txt");
}

/// A plugin proper: the file the game loads, and the only thing ever copied
/// into the game folder.
bool IsVariant(const std::string& path) { return Extension(path) == "w2p"; }

/// A folder name turned into something to put on a button.
///
/// "3_Legacy of Dalaran" -> "Legacy of Dalaran", "0_basegame" -> "Basegame",
/// "1_DAIFE" -> "DAIFE". The prefix goes, underscores become spaces, and the
/// first letter is raised — but only when the word is not already capitalised,
/// because DAIFE is an acronym and "Daife" would be wrong.
std::string NameFromFolder(const std::string& folder) {
  std::string name = folder;
  const size_t underscore = name.find('_');
  if (underscore != std::string::npos) {
    bool all_digits = underscore > 0;
    for (size_t i = 0; i < underscore; ++i) {
      if (name[i] < '0' || name[i] > '9') { all_digits = false; break; }
    }
    if (all_digits) name = name.substr(underscore + 1);
  }
  for (char& c : name) {
    if (c == '_') c = ' ';
  }
  name = Trim(name);
  if (name.empty()) return folder;
  const unsigned char first = static_cast<unsigned char>(name[0]);
  if (std::islower(first)) {
    name[0] = static_cast<char>(std::toupper(first));
  }
  return name;
}

/// The number a folder is prefixed with, or -1.
long long OrderFromFolder(const std::string& folder) {
  size_t i = 0;
  long long value = 0;
  while (i < folder.size() && folder[i] >= '0' && folder[i] <= '9') {
    value = value * 10 + (folder[i] - '0');
    ++i;
  }
  if (i == 0) return -1;
  return value;
}

size_t FindOrAdd(ul_catalogue* c, const std::string& id) {
  const std::string key = Lower(id);
  const auto found = c->by_id.find(key);
  if (found != c->by_id.end()) return found->second;
  Plugin plugin;
  plugin.id = id;
  // The folder's own name, cleaned up — what the list shows. The info.txt
  // never overrides it: it is prose, and for now nothing in it names things.
  plugin.name = NameFromFolder(id);
  plugin.order = OrderFromFolder(id);
  c->plugins.push_back(std::move(plugin));
  const size_t index = c->plugins.size() - 1;
  c->by_id.emplace(key, index);
  return index;
}

}  // namespace
}  // namespace ul

namespace {

constexpr char kPluginsFolder[] = "Plugins";
/// The package-root folder describing the mod itself, read the same way a
/// plugin folder is. No package ships one yet; see the header.
constexpr char kBaseFolder[] = "base";
/// "No plugin" is the empty id. It is not a folder and never appears in the
/// catalogue: it is the name of an empty selection, and choosing it removes
/// every plugin file the manager placed.
constexpr char kNoneId[] = "";

const ul::Plugin* At(const ul_catalogue* c, int index) {
  if (!c || index < 0 || index >= static_cast<int>(c->plugins.size())) return nullptr;
  return &c->plugins[static_cast<size_t>(index)];
}

}  // namespace

// ------------------------------------------------------------------- the ABI

extern "C" {

ul_catalogue* ul_catalogue_create(void) { return new (std::nothrow) ul_catalogue(); }

void ul_catalogue_free(ul_catalogue* c) { delete c; }

void ul_catalogue_add_path(ul_catalogue* c, const char* relative_path) {
  if (!c || c->finished || !relative_path) return;
  const std::string path = ul::NormaliseSlashes(relative_path);

  const std::string top = ul::Segment(path, 0);
  ul::Plugin* plugin = nullptr;
  if (ul::EqualsNoCase(top, kBaseFolder)) {
    // "base" on its own is the folder, not a file in it.
    if (path.size() <= top.size() + 1) return;
    plugin = &c->base;
  } else if (ul::EqualsNoCase(top, kPluginsFolder)) {
    const std::string id = ul::Segment(path, 1);
    if (id.empty()) return;
    // "Plugins/2_Insane" on its own is the folder, not a file in it. A host
    // that reports directories as well as files should not create an empty
    // plugin.
    if (path.size() <= std::string(kPluginsFolder).size() + 1 + id.size()) return;
    plugin = &c->plugins[ul::FindOrAdd(c, id)];
  } else {
    return;
  }

  if (ul::IsInfo(path)) return;          // read separately, through add_info
  if (ul::IsImage(path)) {
    plugin->images.push_back(path);
    return;
  }
  if (ul::IsVariant(path)) {
    plugin->variants.push_back(path);
    return;
  }
  plugin->notes.push_back(path);
}

void ul_catalogue_add_info(ul_catalogue* c, const char* plugin_id, const char* text,
                           size_t length) {
  if (!c || c->finished || !plugin_id || !text) return;
  // The empty id is the base/ folder: the mod describing itself.
  ul::Plugin& plugin =
      *plugin_id ? c->plugins[ul::FindOrAdd(c, plugin_id)] : c->base;

  // The whole file, as written, minus the trailing whitespace every editor
  // leaves — it is the author's prose, and its shape is the author's too. The
  // name stays the folder's: for now nothing in the file names the plugin.
  std::string whole(text, length);
  while (!whole.empty()) {
    const char last = whole.back();
    if (last != '\r' && last != '\n' && last != ' ' && last != '\t') break;
    whole.pop_back();
  }
  if (whole.empty()) return;

  plugin.description = whole;
  plugin.has_info = true;

  // Every YouTube video and playlist link anywhere in the file. Authors write
  // them mid-sentence, and they end up in the gallery beside the screenshots.
  // A channel is not watchable and is left alone.
  plugin.videos = ul::FindYouTubeItems(whole);
}

void ul_catalogue_finish(ul_catalogue* c) {
  if (!c || c->finished) return;
  auto settle = [](ul::Plugin& plugin) {
    // Name order within a folder, so an author gets the slideshow order they
    // meant by calling the files 1.png, 2.png, 3.png. Compared case-insensitively
    // and without the folder, so "A.png" and "b.png" do not depend on the case
    // the filesystem reported.
    auto by_name = [](const std::string& a, const std::string& b) {
      return ul::Lower(ul::BaseName(a)) < ul::Lower(ul::BaseName(b));
    };
    std::sort(plugin.images.begin(), plugin.images.end(), by_name);
    // Name order for the variants too, so "plugin trolls 1" comes before
    // "plugin trolls 2" — which for a difficulty pair is easiest first, and is
    // the order the author gets by numbering them.
    std::sort(plugin.variants.begin(), plugin.variants.end(), by_name);
    std::sort(plugin.notes.begin(), plugin.notes.end(), by_name);
    // The names shown for the variants, built once. The file's own name
    // without its extension: not friendly, but the author's.
    plugin.variant_names.clear();
    for (const std::string& variant : plugin.variants) {
      std::string name = ul::BaseName(variant);
      const size_t dot = name.find_last_of('.');
      if (dot != std::string::npos) name = name.substr(0, dot);
      plugin.variant_names.push_back(name);
    }
  };
  for (ul::Plugin& plugin : c->plugins) settle(plugin);
  settle(c->base);
  std::stable_sort(c->plugins.begin(), c->plugins.end(),
                   [](const ul::Plugin& a, const ul::Plugin& b) {
                     // A numbered folder before an unnumbered one: the numbers
                     // are the author's running order and start at 0_basegame,
                     // and a folder added later without one belongs at the end.
                     if ((a.order < 0) != (b.order < 0)) return a.order >= 0;
                     if (a.order != b.order && a.order >= 0) return a.order < b.order;
                     return ul::Lower(a.id) < ul::Lower(b.id);
                   });
  c->by_id.clear();
  for (size_t i = 0; i < c->plugins.size(); ++i) {
    c->by_id.emplace(ul::Lower(c->plugins[i].id), i);
  }
  c->finished = true;
}

int ul_catalogue_count(const ul_catalogue* c) {
  return c ? static_cast<int>(c->plugins.size()) : 0;
}

const char* ul_plugin_id(const ul_catalogue* c, int index) {
  const ul::Plugin* p = At(c, index);
  return p ? p->id.c_str() : "";
}

const char* ul_plugin_name(const ul_catalogue* c, int index) {
  const ul::Plugin* p = At(c, index);
  return p ? p->name.c_str() : "";
}

int ul_plugin_video_count(const ul_catalogue* c, int index) {
  const ul::Plugin* p = At(c, index);
  return p ? static_cast<int>(p->videos.size()) : 0;
}

const char* ul_plugin_video_id(const ul_catalogue* c, int index, int video) {
  const ul::Plugin* p = At(c, index);
  if (!p || video < 0 || video >= static_cast<int>(p->videos.size())) return "";
  return p->videos[static_cast<size_t>(video)].video.c_str();
}

const char* ul_plugin_video_list_id(const ul_catalogue* c, int index, int video) {
  const ul::Plugin* p = At(c, index);
  if (!p || video < 0 || video >= static_cast<int>(p->videos.size())) return "";
  return p->videos[static_cast<size_t>(video)].list.c_str();
}

namespace {

/// The page a browser wants for one gallery item: the watch page for a video,
/// the playlist page for a playlist.
std::string WatchUrl(const ul::YouTubeItem& item) {
  if (!item.video.empty()) return "https://www.youtube.com/watch?v=" + item.video;
  return "https://www.youtube.com/playlist?list=" + item.list;
}

/// The embed form of the same item. videoseries is how YouTube spells "embed a
/// playlist"; rel=0 keeps the "up next" grid at the end to the same channel,
/// which for a mod showcase means dannyldd's own videos rather than whatever
/// YouTube would rather show. playsinline matters on nothing here and costs
/// nothing.
std::string EmbedUrl(const ul::YouTubeItem& item) {
  if (!item.video.empty()) {
    return "https://www.youtube.com/embed/" + item.video + "?rel=0&playsinline=1";
  }
  return "https://www.youtube.com/embed/videoseries?list=" + item.list +
         "&rel=0&playsinline=1";
}

}  // namespace

const char* ul_plugin_video_url(const ul_catalogue* c, int index, int video) {
  const ul::Plugin* p = At(c, index);
  if (!p || video < 0 || video >= static_cast<int>(p->videos.size())) return "";
  // Built on demand into a slot the plugin owns, so the pointer handed back
  // lives as long as the catalogue does, like every other getter here.
  p->video_urls.resize(p->videos.size());
  auto& slot = p->video_urls[static_cast<size_t>(video)];
  if (slot.empty()) slot = WatchUrl(p->videos[static_cast<size_t>(video)]);
  return slot.c_str();
}

const char* ul_plugin_video_embed_url(const ul_catalogue* c, int index, int video) {
  const ul::Plugin* p = At(c, index);
  if (!p || video < 0 || video >= static_cast<int>(p->videos.size())) return "";
  p->video_embeds.resize(p->videos.size());
  auto& slot = p->video_embeds[static_cast<size_t>(video)];
  if (slot.empty()) slot = EmbedUrl(p->videos[static_cast<size_t>(video)]);
  return slot.c_str();
}

const char* ul_plugin_description(const ul_catalogue* c, int index) {
  const ul::Plugin* p = At(c, index);
  return p ? p->description.c_str() : "";
}

int ul_plugin_has_info(const ul_catalogue* c, int index) {
  const ul::Plugin* p = At(c, index);
  return (p && p->has_info) ? 1 : 0;
}

int ul_plugin_variant_count(const ul_catalogue* c, int index) {
  const ul::Plugin* p = At(c, index);
  return p ? static_cast<int>(p->variants.size()) : 0;
}

const char* ul_plugin_variant_name(const ul_catalogue* c, int index, int variant) {
  const ul::Plugin* p = At(c, index);
  if (!p || variant < 0 || variant >= static_cast<int>(p->variant_names.size())) return "";
  return p->variant_names[static_cast<size_t>(variant)].c_str();
}

const char* ul_plugin_variant_path(const ul_catalogue* c, int index, int variant) {
  const ul::Plugin* p = At(c, index);
  if (!p || variant < 0 || variant >= static_cast<int>(p->variants.size())) return "";
  return p->variants[static_cast<size_t>(variant)].c_str();
}

int ul_plugin_image_count(const ul_catalogue* c, int index) {
  const ul::Plugin* p = At(c, index);
  return p ? static_cast<int>(p->images.size()) : 0;
}

const char* ul_plugin_image(const ul_catalogue* c, int index, int image) {
  const ul::Plugin* p = At(c, index);
  if (!p || image < 0 || image >= static_cast<int>(p->images.size())) return "";
  return p->images[static_cast<size_t>(image)].c_str();
}

int ul_catalogue_find(const ul_catalogue* c, const char* plugin_id) {
  if (!c || !plugin_id) return -1;
  const auto found = c->by_id.find(ul::Lower(plugin_id));
  return found == c->by_id.end() ? -1 : static_cast<int>(found->second);
}

const char* ul_plugin_none_id(void) { return kNoneId; }

const char* ul_base_description(const ul_catalogue* c) {
  return c ? c->base.description.c_str() : "";
}

int ul_base_image_count(const ul_catalogue* c) {
  return c ? static_cast<int>(c->base.images.size()) : 0;
}

const char* ul_base_image(const ul_catalogue* c, int image) {
  if (!c || image < 0 || image >= static_cast<int>(c->base.images.size())) return "";
  return c->base.images[static_cast<size_t>(image)].c_str();
}

int ul_base_video_count(const ul_catalogue* c) {
  return c ? static_cast<int>(c->base.videos.size()) : 0;
}

const char* ul_base_video_id(const ul_catalogue* c, int video) {
  if (!c || video < 0 || video >= static_cast<int>(c->base.videos.size())) return "";
  return c->base.videos[static_cast<size_t>(video)].video.c_str();
}

const char* ul_base_video_list_id(const ul_catalogue* c, int video) {
  if (!c || video < 0 || video >= static_cast<int>(c->base.videos.size())) return "";
  return c->base.videos[static_cast<size_t>(video)].list.c_str();
}

const char* ul_base_video_url(const ul_catalogue* c, int video) {
  if (!c || video < 0 || video >= static_cast<int>(c->base.videos.size())) return "";
  // Built on demand into a slot the entry owns, like ul_plugin_video_url.
  c->base.video_urls.resize(c->base.videos.size());
  auto& slot = c->base.video_urls[static_cast<size_t>(video)];
  if (slot.empty()) slot = WatchUrl(c->base.videos[static_cast<size_t>(video)]);
  return slot.c_str();
}

}  // extern "C"

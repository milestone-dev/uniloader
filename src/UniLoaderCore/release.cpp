// What GameBanana says the current release is.
//
// The mod page is the source of truth because it already was one: dannyldd
// bumps its version and writes a changelog entry as part of publishing, and has
// done for fifty-five releases. Reading it asks nothing new of him, and there is
// no second place for the two to disagree. Both documents are anonymous:
//
//   https://gamebanana.com/apiv11/Mod/644456/ProfilePage
//   https://gamebanana.com/apiv11/Mod/644456/Updates?_nPage=1&_nPerpage=20
//
// The page holds the version but not the payload. The package outgrew what the
// site accepts — dannyldd's words are that uploading it "causes some upload
// bug-desyncs" — so the file attached to the post is 197 bytes: a rar holding a
// single text file holding a single link to the OneDrive folder with the real
// package in it. That indirection predates this program and is still the
// published route, so ReleaseSources follows it rather than replacing it.

#include "uniloader/uniloader.h"

#include "json.hpp"
#include "util.hpp"
#include "version.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace ul {

struct ReleaseSource {
  int kind = UL_SOURCE_ALTERNATE;
  std::string url;
  int64_t size = -1;
};

struct ReleaseNote {
  std::string version;
  std::string text;
  int64_t date = 0;
};

}  // namespace ul

struct ul_release {
  std::string mod_name;
  std::string version;
  std::string page_url;
  std::string author;
  std::string author_url;
  std::string description;
  int64_t modified = 0;
  std::vector<std::string> images;
  std::vector<ul::ReleaseSource> sources;
  std::vector<ul::ReleaseNote> notes;

  // Scratch, so the getters can hand back a `const char*` that outlives the call.
  mutable std::string scratch;
};

namespace ul {
namespace {

/// Below this, an attachment is a pointer to the package rather than the
/// package. The real thing is hundreds of megabytes and the pointer is 197
/// bytes, so anything in between is a judgement call that never arises; a
/// megabyte sits comfortably in the gap and does not depend on the pointer
/// staying exactly the size it is today.
constexpr int64_t kPointerMaxBytes = 1024 * 1024;

bool LooksLikeArchive(const std::string& url) {
  // The extension may be followed by a query string, so this looks at the path.
  std::string path = url;
  const size_t query = path.find_first_of("?#");
  if (query != std::string::npos) path = path.substr(0, query);
  const std::string extension = Extension(path);
  return extension == "rar" || extension == "zip" || extension == "7z";
}

/// Whether an alternate source is plausibly where the package lives. dannyldd's
/// alternate sources today are two YouTube links, so this has to be able to say
/// no — a launcher that tried to unpack a video would be worse than one that
/// asked for a link.
bool LooksLikeDownload(const std::string& url) {
  if (LooksLikeArchive(url)) return true;
  if (ContainsNoCase(url, "youtube.com") || ContainsNoCase(url, "youtu.be")) return false;
  // A OneDrive link is a download when it is not a folder. A folder is kept
  // too, at the back of the queue, because it is still a lead.
  return ContainsNoCase(url, "1drv.ms") || ContainsNoCase(url, "onedrive.live.com") ||
         ContainsNoCase(url, "sharepoint.com") ||
         ContainsNoCase(url, "microsoftpersonalcontent.com") ||
         ContainsNoCase(url, "mediafire.com") || ContainsNoCase(url, "drive.google.com") ||
         ContainsNoCase(url, "dropbox.com") || ContainsNoCase(url, "github.com");
}

void AddSource(ul_release* release, int kind, const std::string& url, int64_t size) {
  if (url.empty()) return;
  for (const ReleaseSource& existing : release->sources) {
    if (EqualsNoCase(existing.url, url)) return;
  }
  release->sources.push_back(ReleaseSource{kind, url, size});
}

/// Best first, and stable within a rank so that the order dannyldd listed his
/// own alternate sources in is the order they are tried.
void SortSources(ul_release* release) {
  auto rank = [](const ReleaseSource& source) {
    // A direct archive beats a share link of the same kind: it needs no
    // rewriting, no listing, and no second request to find out it is a folder.
    const int direct = LooksLikeArchive(source.url) ? 0 : 1;
    return source.kind * 2 + direct;
  };
  std::stable_sort(release->sources.begin(), release->sources.end(),
                   [&](const ReleaseSource& a, const ReleaseSource& b) {
                     return rank(a) < rank(b);
                   });
}

}  // namespace

ul_release* ParseRelease(const std::string& json, int64_t mod_id) {
  Json document;
  if (!Json::Parse(json.c_str(), json.size(), document)) {
    SetLastError("GameBanana's reply was not readable JSON.");
    return nullptr;
  }
  if (document.type != Json::Type::Object) {
    SetLastError("GameBanana's reply was not a mod page.");
    return nullptr;
  }
  const int64_t id = document.Int("_idRow", 0);
  if (id == 0) {
    // An error document, or a page shape this build predates. Either way there
    // is no version in it, and offering one anyway would be worse than saying so.
    SetLastError("GameBanana's reply had no mod in it.");
    return nullptr;
  }
  if (mod_id != 0 && id != mod_id) {
    // A redirect, a cache, or a wrong id in the settings. Checked because the
    // consequence of getting it wrong is installing a different mod entirely.
    SetLastError("GameBanana answered with a different mod than the one asked for.");
    return nullptr;
  }

  auto release = new (std::nothrow) ul_release();
  if (!release) return nullptr;

  release->mod_name = document.Str("_sName");
  release->version = Trim(document.Str("_sVersion"));
  release->modified = document.Int("_tsDateModified", 0);
  release->page_url = document.Str("_sProfileUrl");
  if (release->page_url.empty() && id != 0) {
    release->page_url = "https://gamebanana.com/mods/" + std::to_string(id);
  }
  if (const Json* submitter = document.Find("_aSubmitter")) {
    release->author = submitter->Str("_sName");
    release->author_url = submitter->Str("_sProfileUrl");
  }
  // The page's own prose, under either of the two names it has been served as.
  const std::string description =
      !document.Str("_sDescription").empty() ? document.Str("_sDescription")
                                             : document.Str("_sText");
  release->description = HtmlToText(description);

  if (const Json* preview = document.Find("_aPreviewMedia")) {
    for (const Json& image : preview->Array("_aImages")) {
      const std::string base = image.Str("_sBaseUrl");
      const std::string file = image.Str("_sFile");
      if (base.empty() || file.empty()) continue;
      release->images.push_back(base + "/" + file);
    }
  }

  for (const Json& source : document.Array("_aAlternateFileSources")) {
    const std::string url = source.Str("url");
    if (LooksLikeDownload(url)) AddSource(release, UL_SOURCE_ALTERNATE, url, -1);
  }

  for (const Json& file : document.Array("_aFiles")) {
    const std::string url = file.Str("_sDownloadUrl");
    if (url.empty()) continue;
    const int64_t size = file.Int("_nFilesize", -1);
    // Big enough to be the package: an attachment. Small enough to be a note
    // pointing at one: a pointer, whose *contents* are fetched and read.
    // Unknown size is treated as a pointer, because reading 197 bytes to find
    // out is cheap and downloading the wrong hundreds of megabytes is not.
    const bool is_pointer = size < 0 || size < kPointerMaxBytes;
    AddSource(release, is_pointer ? UL_SOURCE_POINTER : UL_SOURCE_ATTACHMENT, url, size);
  }

  if (release->version.empty()) {
    // No version field. Fall back to the newest attachment's name, which is
    // where the version lived before GameBanana had a field for it.
    for (const Json& file : document.Array("_aFiles")) {
      const std::string guessed = VersionFromFilename(file.Str("_sFile"));
      if (!guessed.empty()) { release->version = guessed; break; }
    }
  }

  SortSources(release);
  return release;
}

int AddPointerText(ul_release* release, const std::string& text) {
  if (!release) return 0;
  int added = 0;
  for (const std::string& url : FindUrls(text)) {
    if (!LooksLikeDownload(url)) continue;
    const size_t before = release->sources.size();
    // Ranked below every source already known: this one came from following a
    // lead, and a link stated outright on the page is better evidence.
    AddSource(release, UL_SOURCE_FOLDER, url, -1);
    if (release->sources.size() != before) ++added;
  }
  SortSources(release);
  return added;
}

int AddUpdates(ul_release* release, const std::string& json) {
  if (!release) return UL_ERR_PARSE;
  Json document;
  if (!Json::Parse(json.c_str(), json.size(), document)) {
    SetLastError("GameBanana's update list was not readable JSON.");
    return UL_ERR_PARSE;
  }
  const std::vector<Json>& records = document.Array("_aRecords");
  if (records.empty() && !document.Find("_aRecords")) {
    SetLastError("GameBanana's reply had no update list in it.");
    return UL_ERR_PARSE;
  }
  for (const Json& record : records) {
    ReleaseNote note;
    note.version = Trim(record.Str("_sVersion"));
    note.date = record.Int("_tsDateAdded", 0);
    note.text = HtmlToText(record.Str("_sText"));
    if (note.text.empty()) {
      // Some entries carry only the snippet the site generates for a preview.
      if (const Json* preview = record.Find("_aPreviewMedia")) {
        if (const Json* metadata = preview->Find("_aMetadata")) {
          note.text = HtmlToText(metadata->Str("_sSnippet"));
        }
      }
    }
    if (note.version.empty() && note.text.empty()) continue;
    release->notes.push_back(std::move(note));
  }
  // Newest first, which is the order they are shown in and not reliably the
  // order they arrive in once a page boundary is involved.
  std::stable_sort(release->notes.begin(), release->notes.end(),
                   [](const ReleaseNote& a, const ReleaseNote& b) {
                     if (a.date != b.date) return a.date > b.date;
                     return CompareVersions(a.version, b.version) > 0;
                   });
  return UL_OK;
}

std::string NotesSince(const ul_release* release, const std::string& installed) {
  if (!release) return {};
  std::string joined;
  for (const ReleaseNote& note : release->notes) {
    // Everything published after what the user has. Someone three releases
    // behind should see all three, not just the newest — that is the difference
    // between "there is an update" and knowing whether to take it.
    if (!installed.empty() && !note.version.empty() &&
        CompareVersions(installed, note.version) >= 0) {
      continue;
    }
    if (!joined.empty()) joined += "\n\n";
    if (!note.version.empty()) joined += note.version + "\n";
    joined += note.text;
  }
  return joined;
}

}  // namespace ul

// ------------------------------------------------------------------- the ABI

namespace {
const char* Borrow(const std::string& s) { return s.c_str(); }
}  // namespace

extern "C" {

ul_release* ul_release_parse(const char* json, size_t length, int64_t mod_id) {
  if (!json) return nullptr;
  return ul::ParseRelease(std::string(json, length), mod_id);
}

void ul_release_free(ul_release* r) { delete r; }

const char* ul_release_mod_name(const ul_release* r) { return r ? Borrow(r->mod_name) : ""; }
const char* ul_release_version(const ul_release* r) { return r ? Borrow(r->version) : ""; }
int64_t ul_release_modified(const ul_release* r) { return r ? r->modified : 0; }
const char* ul_release_page_url(const ul_release* r) { return r ? Borrow(r->page_url) : ""; }
const char* ul_release_author(const ul_release* r) { return r ? Borrow(r->author) : ""; }
const char* ul_release_author_url(const ul_release* r) { return r ? Borrow(r->author_url) : ""; }
const char* ul_release_description(const ul_release* r) {
  return r ? Borrow(r->description) : "";
}

int ul_release_image_count(const ul_release* r) {
  return r ? static_cast<int>(r->images.size()) : 0;
}

const char* ul_release_image(const ul_release* r, int index) {
  if (!r || index < 0 || index >= static_cast<int>(r->images.size())) return "";
  return Borrow(r->images[static_cast<size_t>(index)]);
}

int ul_release_source_count(const ul_release* r) {
  return r ? static_cast<int>(r->sources.size()) : 0;
}

int ul_release_source_kind(const ul_release* r, int index) {
  if (!r || index < 0 || index >= static_cast<int>(r->sources.size())) return -1;
  return r->sources[static_cast<size_t>(index)].kind;
}

const char* ul_release_source_url(const ul_release* r, int index) {
  if (!r || index < 0 || index >= static_cast<int>(r->sources.size())) return "";
  return Borrow(r->sources[static_cast<size_t>(index)].url);
}

int64_t ul_release_source_size(const ul_release* r, int index) {
  if (!r || index < 0 || index >= static_cast<int>(r->sources.size())) return -1;
  return r->sources[static_cast<size_t>(index)].size;
}

int ul_release_add_pointer_text(ul_release* r, const char* text, size_t length) {
  if (!r || !text) return 0;
  return ul::AddPointerText(r, std::string(text, length));
}

int ul_release_add_updates(ul_release* r, const char* json, size_t length) {
  if (!r || !json) return UL_ERR_PARSE;
  return ul::AddUpdates(r, std::string(json, length));
}

int ul_release_note_count(const ul_release* r) {
  return r ? static_cast<int>(r->notes.size()) : 0;
}

const char* ul_release_note_version(const ul_release* r, int index) {
  if (!r || index < 0 || index >= static_cast<int>(r->notes.size())) return "";
  return Borrow(r->notes[static_cast<size_t>(index)].version);
}

const char* ul_release_note_text(const ul_release* r, int index) {
  if (!r || index < 0 || index >= static_cast<int>(r->notes.size())) return "";
  return Borrow(r->notes[static_cast<size_t>(index)].text);
}

int64_t ul_release_note_date(const ul_release* r, int index) {
  if (!r || index < 0 || index >= static_cast<int>(r->notes.size())) return 0;
  return r->notes[static_cast<size_t>(index)].date;
}

char* ul_release_notes_since(const ul_release* r, const char* installed) {
  if (!r) return nullptr;
  return ul::Duplicate(ul::NotesSince(r, installed ? installed : ""));
}

char* ul_oembed_thumbnail_url(const char* json, size_t length) {
  if (!json) return nullptr;
  ul::Json document;
  if (!ul::Json::Parse(json, length, document)) return nullptr;
  const std::string url = document.Str("thumbnail_url");
  return url.empty() ? nullptr : ul::Duplicate(url);
}

}  // extern "C"

#include "util.hpp"

#include <algorithm>
#include <utility>
#include <cstring>

namespace ul {
namespace {

char LowerAscii(char c) {
  return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

/// ASCII only, deliberately. The comparisons this feeds are against literals
/// the code owns — "Plugins", ".w2p", "plugin.txt" — and a locale-aware fold
/// would make the same input behave differently on a Turkish machine, where
/// lowercasing 'I' does not give 'i'. Folder names in the package are the
/// author's and are matched exactly elsewhere.
const char* kBlockTags[] = {"p",  "br", "div", "li", "tr", "h1", "h2",
                            "h3", "h4", "h5",  "h6", "ul", "ol", "blockquote"};

bool IsBlockTag(const std::string& name) {
  for (const char* tag : kBlockTags) {
    if (name == tag) return true;
  }
  return false;
}

}  // namespace

std::string Lower(std::string s) {
  for (char& c : s) c = LowerAscii(c);
  return s;
}

bool EqualsNoCase(const std::string& a, const std::string& b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (LowerAscii(a[i]) != LowerAscii(b[i])) return false;
  }
  return true;
}

bool StartsWithNoCase(const std::string& s, const std::string& prefix) {
  if (s.size() < prefix.size()) return false;
  for (size_t i = 0; i < prefix.size(); ++i) {
    if (LowerAscii(s[i]) != LowerAscii(prefix[i])) return false;
  }
  return true;
}

bool EndsWithNoCase(const std::string& s, const std::string& suffix) {
  if (s.size() < suffix.size()) return false;
  const size_t offset = s.size() - suffix.size();
  for (size_t i = 0; i < suffix.size(); ++i) {
    if (LowerAscii(s[offset + i]) != LowerAscii(suffix[i])) return false;
  }
  return true;
}

bool ContainsNoCase(const std::string& haystack, const std::string& needle) {
  if (needle.empty()) return true;
  if (haystack.size() < needle.size()) return false;
  const std::string lowered = Lower(haystack);
  return lowered.find(Lower(needle)) != std::string::npos;
}

std::string Trim(const std::string& s) {
  size_t first = 0;
  size_t last = s.size();
  auto space = [](char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
  };
  while (first < last && space(s[first])) ++first;
  while (last > first && space(s[last - 1])) --last;
  return s.substr(first, last - first);
}

std::string HtmlToText(const std::string& html) {
  std::string out;
  out.reserve(html.size());
  size_t i = 0;
  while (i < html.size()) {
    if (html[i] == '<') {
      const size_t close = html.find('>', i);
      if (close == std::string::npos) break;   // unterminated tag: drop the rest
      std::string name;
      size_t j = i + 1;
      if (j < html.size() && html[j] == '/') ++j;
      while (j < html.size() && html[j] != '>' && html[j] != ' ' && html[j] != '/') {
        name.push_back(LowerAscii(html[j]));
        ++j;
      }
      if (IsBlockTag(name) && !out.empty() && out.back() != '\n') out.push_back('\n');
      i = close + 1;
      continue;
    }
    if (html[i] == '&') {
      const size_t semi = html.find(';', i);
      // Bounded: an unescaped '&' in prose would otherwise swallow everything
      // up to the next one, which in a changelog is most of a paragraph.
      if (semi != std::string::npos && semi - i <= 10) {
        const std::string entity = Lower(html.substr(i + 1, semi - i - 1));
        const char* replacement = nullptr;
        if (entity == "amp") replacement = "&";
        else if (entity == "lt") replacement = "<";
        else if (entity == "gt") replacement = ">";
        else if (entity == "quot") replacement = "\"";
        else if (entity == "apos" || entity == "#39") replacement = "'";
        else if (entity == "nbsp") replacement = " ";
        if (replacement) {
          out += replacement;
          i = semi + 1;
          continue;
        }
      }
    }
    out.push_back(html[i]);
    ++i;
  }
  // Runs of blank lines collapse to one: the notes are pasted from a text
  // editor into a rich field, and come back with a <p> around every line.
  std::string tidy;
  tidy.reserve(out.size());
  int newlines = 0;
  for (const char c : out) {
    if (c == '\r') continue;
    if (c == '\n') {
      if (++newlines > 2) continue;
    } else {
      newlines = 0;
    }
    tidy.push_back(c);
  }
  return Trim(tidy);
}

std::vector<std::string> FindUrls(const std::string& text) {
  std::vector<std::string> urls;
  const std::string lowered = Lower(text);
  size_t i = 0;
  while (i < text.size()) {
    const size_t at = lowered.find("http", i);
    if (at == std::string::npos) break;
    size_t end = at;
    while (end < text.size()) {
      const char c = text[end];
      if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '"' || c == '\'' ||
          c == '<' || c == '>') {
        break;
      }
      ++end;
    }
    std::string url = text.substr(at, end - at);
    // Trailing punctuation belongs to the sentence, not the link — except ')',
    // which OneDrive share links do not contain but Markdown wrappers do.
    while (!url.empty() && (url.back() == '.' || url.back() == ',' ||
                            url.back() == ')' || url.back() == ']')) {
      url.pop_back();
    }
    if (StartsWithNoCase(url, "http://") || StartsWithNoCase(url, "https://")) {
      urls.push_back(url);
    }
    i = end > at ? end : at + 1;
  }
  return urls;
}

namespace {

/// A YouTube id is eleven characters of base64url. Checked rather than assumed,
/// so that "youtube.com/user/dannyldd" does not become a video whose thumbnail
/// is a 404 and whose link goes nowhere in particular.
bool IsVideoId(const std::string& id) {
  if (id.size() != 11) return false;
  for (const char c : id) {
    const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                    (c >= '0' && c <= '9') || c == '-' || c == '_';
    if (!ok) return false;
  }
  return true;
}

/// The eleven characters starting at `at`, or "" if there are not eleven of
/// them before something that cannot be part of an id.
std::string IdAt(const std::string& text, size_t at) {
  std::string id;
  while (at < text.size() && id.size() < 11) {
    const char c = text[at];
    const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                    (c >= '0' && c <= '9') || c == '-' || c == '_';
    if (!ok) break;
    id.push_back(c);
    ++at;
  }
  return IsVideoId(id) ? id : std::string();
}

/// The playlist id starting at `at`, or "". Unlike a video id these vary in
/// length — "PL" plus 32, a mix's "RD" plus a video id — so the check is the
/// charset and a plausible span, 13 to 64 characters.
std::string ListIdAt(const std::string& text, size_t at) {
  std::string id;
  while (at < text.size() && id.size() <= 64) {
    const char c = text[at];
    const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                    (c >= '0' && c <= '9') || c == '-' || c == '_';
    if (!ok) break;
    id.push_back(c);
    ++at;
  }
  return (id.size() >= 13 && id.size() <= 64) ? id : std::string();
}

}  // namespace

std::vector<YouTubeItem> FindYouTubeItems(const std::string& text) {
  const std::string lowered = Lower(text);
  std::vector<YouTubeItem> items;
  const auto put = [&items](const std::string& video, const std::string& list) {
    if (video.empty() && list.empty()) return;
    // Deduplicated: a link written once in prose and again as a bare URL is
    // one video, and two thumbnails of it in a gallery is a bug.
    for (const YouTubeItem& existing : items) {
      if (existing.video == video && existing.list == list) return;
    }
    items.push_back({video, list});
  };
  const auto add = [&put](const std::string& video) { put(video, ""); };

  // One left-to-right scan for "youtu", then the shapes that can follow it —
  // which keeps the links in the order the author wrote them. Matching the
  // host rather than a fixed list of URLs is what accepts m.youtube.com,
  // music.youtube.com, youtube-nocookie.com and the country domains
  // (youtube.de, youtube.co.uk) without accepting every "/embed/" on the
  // internet as a video.
  for (size_t at = lowered.find("youtu"); at != std::string::npos;
       at = lowered.find("youtu", at + 5)) {
    size_t rest = at + 5;
    if (lowered.compare(rest, 4, ".be/") == 0) {   // the shortener
      add(IdAt(text, rest + 4));
      continue;
    }
    if (lowered.compare(rest, 2, "be") != 0) continue;   // not "youtube…"
    rest += 2;
    if (lowered.compare(rest, 9, "-nocookie") == 0) rest += 9;
    if (rest >= lowered.size() || lowered[rest] != '.') continue;
    // The rest of the host: letters and dots up to the path — "com", "de",
    // "co.uk". Anything else is just a word that starts with "youtube".
    ++rest;
    while (rest < lowered.size() &&
           ((lowered[rest] >= 'a' && lowered[rest] <= 'z') || lowered[rest] == '.')) {
      ++rest;
    }
    if (rest >= lowered.size() || lowered[rest] != '/') continue;
    ++rest;

    const auto after = [&lowered, rest](const char* prefix) -> size_t {
      const size_t width = std::strlen(prefix);
      return lowered.compare(rest, width, prefix) == 0 ? rest + width : 0;
    };
    if (const size_t id_at = after("embed/")) {
      add(IdAt(text, id_at));
    } else if (const size_t id_at = after("shorts/")) {
      add(IdAt(text, id_at));
    } else if (const size_t id_at = after("live/")) {
      add(IdAt(text, id_at));
    } else if (const size_t id_at = after("v/")) {
      add(IdAt(text, id_at));
    } else if (const size_t query_at =
                   after("watch") ? after("watch") : after("playlist")) {
      // The parameters, wherever they sit in the query: "watch?v=ID",
      // "watch?t=90&v=ID" and "playlist?list=ID" are all written by people.
      // A link naming both a video and its playlist counts as the video —
      // it is the video the author pointed at, the list came along for the
      // ride — and a playlist alone is the playlist.
      std::string video;
      std::string list;
      for (size_t q = query_at; q < lowered.size(); ++q) {
        const char c = lowered[q];
        if (c == '?' || c == '&') {
          if (video.empty() && lowered.compare(q + 1, 2, "v=") == 0) {
            video = IdAt(text, q + 3);
          } else if (list.empty() && lowered.compare(q + 1, 5, "list=") == 0) {
            list = ListIdAt(text, q + 6);
          }
        } else if (c <= ' ' || c == '"' || c == '<' || c == ')') {
          break;   // the URL ended
        }
      }
      if (!video.empty()) {
        add(video);
      } else {
        put("", list);
      }
    }
  }
  return items;
}

std::string NormaliseSlashes(std::string path) {
  for (char& c : path) {
    if (c == '\\') c = '/';
  }
  return path;
}

std::string TrimTrailingSlash(std::string path) {
  while (!path.empty() && (path.back() == '/' || path.back() == '\\')) path.pop_back();
  return path;
}

std::string JoinPath(const std::string& a, const std::string& b) {
  if (a.empty()) return b;
  if (b.empty()) return a;
  std::string joined = TrimTrailingSlash(a);
  joined.push_back('/');
  size_t start = 0;
  while (start < b.size() && (b[start] == '/' || b[start] == '\\')) ++start;
  joined.append(b, start, std::string::npos);
  return joined;
}

std::string BaseName(const std::string& path) {
  const size_t at = path.find_last_of("/\\");
  return at == std::string::npos ? path : path.substr(at + 1);
}

std::string DirName(const std::string& path) {
  const size_t at = path.find_last_of("/\\");
  return at == std::string::npos ? std::string() : path.substr(0, at);
}

std::string Extension(const std::string& path) {
  const std::string name = BaseName(path);
  const size_t dot = name.find_last_of('.');
  if (dot == std::string::npos || dot + 1 >= name.size()) return {};
  return Lower(name.substr(dot + 1));
}

bool IsSafeRelativePath(const std::string& path) {
  if (path.empty()) return false;
  const std::string p = NormaliseSlashes(path);
  if (p[0] == '/') return false;                       // absolute, or a UNC root
  if (p.size() >= 2 && p[1] == ':') return false;      // C:\...
  size_t start = 0;
  while (start <= p.size()) {
    const size_t slash = p.find('/', start);
    const std::string segment =
        p.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
    if (segment == "..") return false;
    // A trailing space or dot on a Windows path names a *different* file to the
    // one it looks like — "plugin " resolves to "plugin". An archive that used
    // that to land a file somewhere unexpected is refused with the rest.
    if (!segment.empty() && (segment.back() == ' ' || segment.back() == '.') &&
        segment != ".") {
      return false;
    }
    if (slash == std::string::npos) break;
    start = slash + 1;
  }
  return true;
}

bool HasSegment(const std::string& path, const std::string& name) {
  const std::string p = NormaliseSlashes(path);
  size_t start = 0;
  for (;;) {
    const size_t slash = p.find('/', start);
    const std::string segment =
        p.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
    if (EqualsNoCase(segment, name)) return true;
    if (slash == std::string::npos) return false;
    start = slash + 1;
  }
}

std::string Segment(const std::string& path, size_t index) {
  const std::string p = NormaliseSlashes(path);
  size_t start = 0;
  for (size_t i = 0;; ++i) {
    const size_t slash = p.find('/', start);
    const std::string segment =
        p.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
    if (i == index) return segment;
    if (slash == std::string::npos) return {};
    start = slash + 1;
  }
}

std::vector<std::string> SplitNulList(const char* list) {
  std::vector<std::string> items;
  if (!list) return items;
  while (*list) {
    items.emplace_back(list);
    list += items.back().size() + 1;
  }
  return items;
}

std::string JoinNulList(const std::vector<std::string>& items) {
  std::string joined;
  for (const std::string& item : items) {
    joined.append(item);
    joined.push_back('\0');
  }
  joined.push_back('\0');
  return joined;
}

std::string JsonEscaped(const std::string& value) {
  std::string out;
  out.reserve(value.size() + 8);
  for (const char c : value) {
    switch (c) {
      case '"':  out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          static const char kHex[] = "0123456789abcdef";
          out += "\\u00";
          out.push_back(kHex[(c >> 4) & 0xF]);
          out.push_back(kHex[c & 0xF]);
        } else {
          // UTF-8 bytes pass through unescaped, which is valid JSON and keeps
          // a path with a non-Latin folder in it readable in a text editor.
          out.push_back(c);
        }
    }
  }
  return out;
}

char* Duplicate(const std::string& s) {
  char* copy = static_cast<char*>(std::malloc(s.size() + 1));
  if (!copy) return nullptr;
  std::memcpy(copy, s.c_str(), s.size() + 1);
  return copy;
}

namespace {
// Not thread_local: the client calls the core from its worker thread and reads
// the message from the UI thread, and a per-thread slot would make the message
// vanish exactly when it is wanted. Writes are short, single-producer in
// practice — one background operation at a time is what the UI allows.
std::string& ErrorSlot() {
  static std::string slot;
  return slot;
}
}  // namespace

void SetLastError(const std::string& message) { ErrorSlot() = message; }

const char* LastError() {
  const std::string& slot = ErrorSlot();
  return slot.empty() ? "" : slot.c_str();
}

}  // namespace ul

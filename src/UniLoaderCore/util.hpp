// Small shared pieces: paths, strings, and the way the ABI hands out memory.
//
// Everything here is used by more than one topic file. Anything used by exactly
// one lives in that file instead.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ul {

// ------------------------------------------------------------------ strings

std::string Lower(std::string s);
bool EqualsNoCase(const std::string& a, const std::string& b);
bool StartsWithNoCase(const std::string& s, const std::string& prefix);
bool EndsWithNoCase(const std::string& s, const std::string& suffix);
bool ContainsNoCase(const std::string& haystack, const std::string& needle);
std::string Trim(const std::string& s);

/// Tags stripped, entities resolved, block elements turned into line breaks.
/// GameBanana's release notes are HTML, and the client draws them in an EDIT
/// control that has never heard of a <p>.
std::string HtmlToText(const std::string& html);

/// Every http(s) URL in a run of text, in the order they appear. The pointer
/// file attached to the mod page is 93 bytes with one link in it and no format
/// at all, so this is what reading it amounts to.
std::vector<std::string> FindUrls(const std::string& text);

/// One watchable YouTube thing found in a run of text: a video (the
/// eleven-character id) or a playlist (its list id). Exactly one of the two is
/// set — a link that names both a video and its playlist counts as the video.
struct YouTubeItem {
  std::string video;
  std::string list;
};

/// Every YouTube video and playlist link in a run of text, in order and
/// without repeats. youtu.be short links, watch with v= anywhere in the
/// query, embed/shorts/live/v paths, playlist pages — on any youtube host:
/// www, m, music, -nocookie, country domains. A channel is not watchable and
/// is left alone.
std::vector<YouTubeItem> FindYouTubeItems(const std::string& text);


// -------------------------------------------------------------------- paths
//
// Package-relative paths are held with forward slashes throughout the core, so
// that a rule about "Plugins/" is one comparison rather than two. The host
// converts at the edge, which on Windows means barely at all — every API there
// takes either separator.

std::string NormaliseSlashes(std::string path);
/// Trailing separators removed. A game folder recorded with one turns every
/// path built from it into a double separator, and War2Combat's own uninstall
/// entry in the registry has one.
std::string TrimTrailingSlash(std::string path);
std::string JoinPath(const std::string& a, const std::string& b);
/// The part after the last separator.
std::string BaseName(const std::string& path);
/// The part before the last separator, or "" if there is none.
std::string DirName(const std::string& path);
/// Lowercase, without the dot. "" when there is no extension.
std::string Extension(const std::string& path);

/// Whether a path from inside an archive can be trusted to stay below the
/// directory it is unpacked into. Rejects absolute paths, drive letters, UNC
/// prefixes, and any ".." segment. Rejected rather than sanitised: an archive
/// carrying one is not a mod package.
bool IsSafeRelativePath(const std::string& path);

/// Whether `path`'s first segment, or any segment, equals `name`, ignoring case.
bool HasSegment(const std::string& path, const std::string& name);
/// The segment at `index`, or "".
std::string Segment(const std::string& path, size_t index);

// --------------------------------------------------------- NUL-separated lists
//
// The ABI passes lists of paths as one buffer of NUL-terminated strings ending
// in an empty one. It is the shape a Win32 host already has from a directory
// walk, and it costs no allocation per element to hand across.

std::vector<std::string> SplitNulList(const char* list);
std::string JoinNulList(const std::vector<std::string>& items);

/// A string with the characters JSON forbids escaped, ready to sit between two
/// quotes. Shared because two documents are written by hand here — the receipt
/// and the package stamp — and both hold Windows paths, which is the one input
/// a hand-written JSON writer reliably gets wrong.
std::string JsonEscaped(const std::string& value);

// ------------------------------------------------------------------- memory

/// A copy of `s` in a buffer the caller frees with ul_free. Null on allocation
/// failure, which every ABI function returning one is documented to allow.
char* Duplicate(const std::string& s);

/// Where ul_last_error reads from. Set on the failure path of anything that can
/// return null, so a client has something to show beyond "it did not work".
void SetLastError(const std::string& message);
const char* LastError();

}  // namespace ul

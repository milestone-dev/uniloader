// Fetching, over WinHTTP.
//
// WinHTTP rather than WinINet or a library: it is in every Windows since XP, it
// needs no redistributable, it follows redirects, and it does not share a
// cookie jar or a cache with Internet Explorer — which matters here because
// every fetch this program makes is anonymous and should stay that way.
//
// Two shapes, because there are two jobs. A mod page is 30 KB of JSON and is
// wanted in memory. A package is several hundred megabytes, is wanted on disk,
// and has to show progress and be cancellable while it arrives.

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace ulwin {

struct FetchResult {
  bool ok = false;
  unsigned status = 0;        // the HTTP status, when there was one
  std::string body;           // for ToMemory
  std::wstring error;         // ready to show, when ok is false
  std::wstring final_url;     // after redirects — how a share link is resolved
  std::string sha256;         // for ToFile, hex, computed as the bytes arrive
  int64_t bytes = 0;
};

/// done and total in bytes; total is -1 when the server did not say. Return
/// false to cancel — which aborts the transfer rather than reading it to the
/// end and throwing it away.
using ProgressCallback = std::function<bool(int64_t done, int64_t total)>;

/// What a request needs beyond its URL. Default-constructed it is a plain GET,
/// which is all but three of the requests this program makes; the three that
/// list a OneDrive folder are POSTs carrying an authorisation header and a
/// `Prefer` header, and neither of those is optional there.
struct FetchOptions {
  std::wstring method = L"GET";
  /// One per line, "Name: value". Sent verbatim.
  std::vector<std::wstring> headers;
  std::string body;
};

/// A small document into memory. `limit` refuses anything bigger, so a mistyped
/// URL that happens to point at the package does not fill memory before failing.
FetchResult FetchToMemory(const std::wstring& url, size_t limit = 8u * 1024 * 1024);
FetchResult FetchToMemory(const std::wstring& url, const FetchOptions& options,
                          size_t limit = 8u * 1024 * 1024);

/// A large file to disk, hashed on the way. Writes to `path` and replaces
/// whatever is there. On cancellation or failure the partial file is deleted:
/// half a package left on disk is something a later run would have to be clever
/// about, and being clever about it is how a resumed download installs a mix of
/// two versions.
FetchResult FetchToFile(const std::wstring& url, const std::wstring& path,
                        const ProgressCallback& progress);

/// Opens a URL in the user's browser. Used for the two links out — the mod page
/// and dannyldd's profile.
void OpenInBrowser(const std::wstring& url);

}  // namespace ulwin

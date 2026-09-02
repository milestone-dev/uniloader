// Turning the links people publish into links a GET can fetch, and reading a
// shared OneDrive folder.
//
// Everything here was tested against the live services on 2026-08-31, against
// dannyldd's own mod page and share folder.
//
// What the published route looks like:
//
//   gamebanana.com/mods/644456          the post, version "v6.6"
//     -> its attached file, 197 bytes   a rar holding one text file
//        -> 1drv.ms/f/c/.../Ig...       a link to a OneDrive *folder*
//           -> the newest .rar in it    which a person used to pick by eye
//
// The last hop lists the shared folder the way OneDrive's own web client
// does, in three requests:
//
//   1. An anonymous token: `POST api-badgerp.svc.ms/v1.0/token` with an appId,
//      answered with a JWT, sent as `Authorization: Badger <jwt>`.
//
//   2. Redeeming the share: a POST to the shares endpoint carrying
//      `Prefer: autoredeem`, which resolves the link to a drive and item id.
//
//   3. The children listing, which names every file with its size, date and
//      download URL — the download URL serves the raw bytes and honours a
//      Range request.
//
// Services change, so if the listing stops answering, the client falls back to
// what it did before — say what the problem is and send the user to the mod
// page — and the durable fix is a direct link to the package file added to
// the mod page, which the core prefers over all of this.

#include "uniloader/uniloader.h"

#include "json.hpp"
#include "util.hpp"
#include "version.hpp"

#include <string>
#include <vector>

namespace ul {
namespace {

const char kBase64Url[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

/// The `u!` sharing token: base64url of the share URL, unpadded — Microsoft's
/// own documented encoding for referring to a share by its link.
std::string SharingToken(const std::string& url) {
  std::string encoded = "u!";
  size_t i = 0;
  while (i + 2 < url.size()) {
    const unsigned n = (static_cast<unsigned char>(url[i]) << 16) |
                       (static_cast<unsigned char>(url[i + 1]) << 8) |
                       static_cast<unsigned char>(url[i + 2]);
    encoded.push_back(kBase64Url[(n >> 18) & 63]);
    encoded.push_back(kBase64Url[(n >> 12) & 63]);
    encoded.push_back(kBase64Url[(n >> 6) & 63]);
    encoded.push_back(kBase64Url[n & 63]);
    i += 3;
  }
  if (i + 1 == url.size()) {
    const unsigned n = static_cast<unsigned char>(url[i]) << 16;
    encoded.push_back(kBase64Url[(n >> 18) & 63]);
    encoded.push_back(kBase64Url[(n >> 12) & 63]);
  } else if (i + 2 == url.size()) {
    const unsigned n = (static_cast<unsigned char>(url[i]) << 16) |
                       (static_cast<unsigned char>(url[i + 1]) << 8);
    encoded.push_back(kBase64Url[(n >> 18) & 63]);
    encoded.push_back(kBase64Url[(n >> 12) & 63]);
    encoded.push_back(kBase64Url[(n >> 6) & 63]);
  }
  return encoded;
}

bool IsOneDrive(const std::string& url) {
  return ContainsNoCase(url, "1drv.ms") || ContainsNoCase(url, "onedrive.live.com") ||
         ContainsNoCase(url, "sharepoint.com") ||
         ContainsNoCase(url, "microsoftpersonalcontent.com");
}

std::string WithParameter(const std::string& url, const std::string& parameter) {
  if (ContainsNoCase(url, parameter)) return url;
  return url + (url.find('?') == std::string::npos ? "?" : "&") + parameter;
}

/// An ISO-8601 timestamp as a number that sorts the same way. Not a date type:
/// nothing here does arithmetic on it, it only ever breaks a tie between two
/// files whose names carry the same version — or none at all.
int64_t IsoToSortable(const std::string& text) {
  int64_t value = 0;
  int digits = 0;
  for (const char c : text) {
    if (c < '0' || c > '9') continue;
    value = value * 10 + (c - '0');
    if (++digits == 14) break;   // yyyymmddhhmmss, and no further
  }
  return value;
}

}  // namespace

bool UrlIsFolder(const std::string& url) {
  const std::string lowered = Lower(url);
  // 1drv.ms encodes the kind in the path: /f/ is a folder, /u/ and /i/ are a
  // file and an image. Getting this right is the difference between listing a
  // folder and starting a download that was never going to work.
  if (lowered.find("1drv.ms/f/") != std::string::npos) return true;
  if (ContainsNoCase(url, "ithint=folder")) return true;
  if (ContainsNoCase(url, "onedrive.live.com/redir") && !ContainsNoCase(url, "ithint=file")) {
    return true;
  }
  return false;
}

std::string DirectDownloadUrl(const std::string& share_url) {
  const std::string url = Trim(share_url);
  if (url.empty()) return url;
  if (!IsOneDrive(url)) return url;   // GameBanana's own /dl/ links are direct
  if (UrlIsFolder(url)) return url;   // a folder is listed, not fetched

  // `download=1` is what the web client itself appends to a file link, and it
  // survives the redirect chain to the storage host.
  return WithParameter(url, "download=1");
}

std::string PickArchiveFromListing(const std::string& json,
                                   const std::string& expected_version,
                                   std::string* picked_name) {
  if (picked_name) picked_name->clear();
  Json document;
  if (!Json::Parse(json.c_str(), json.size(), document)) {
    SetLastError("The folder listing was not readable.");
    return {};
  }
  if (const Json* error = document.Find("error")) {
    SetLastError("OneDrive refused the listing: " + error->Str("message"));
    return {};
  }

  struct Candidate {
    std::string name;
    std::string url;
    int64_t date = 0;
    bool matches_expected = false;
  };
  std::vector<Candidate> candidates;

  for (const Json& child : document.Array("value")) {
    // The share holds subfolders as well — "1_war2_unification_older_versions",
    // "2_Co-ops_daife_insane_customs", "3_others" — and the package is a file
    // at the top of it. Skipping folders is what keeps the old versions out.
    if (child.Find("folder")) continue;
    const std::string name = child.Str("name");
    const std::string extension = Extension(name);
    if (extension != "rar" && extension != "zip") continue;
    // A multi-volume set's later parts are not the archive to open; the first
    // one is, and it is the one without a partNN suffix.
    if (ContainsNoCase(name, ".part") && !ContainsNoCase(name, ".part1.")) continue;

    Candidate candidate;
    candidate.name = name;
    candidate.url = child.Str("@content.downloadUrl");
    if (candidate.url.empty()) candidate.url = child.Str("@microsoft.graph.downloadUrl");
    if (candidate.url.empty()) continue;
    candidate.date = IsoToSortable(child.Str("lastModifiedDateTime"));
    const std::string version = VersionFromFilename(name);
    candidate.matches_expected = !expected_version.empty() && !version.empty() &&
                                 CompareVersions(version, expected_version) == 0;
    candidates.push_back(std::move(candidate));
  }

  if (candidates.empty()) {
    SetLastError("That folder holds no .rar or .zip package.");
    return {};
  }

  // The mod page already said which release this is, so a file whose name
  // agrees with it is the one being looked for. This is a filter and not the
  // ordering: among the files that agree — or among all of them when none does
  // — the newest still wins.
  bool any_match = false;
  for (const Candidate& candidate : candidates) {
    if (candidate.matches_expected) { any_match = true; break; }
  }

  const Candidate* best = nullptr;
  for (const Candidate& candidate : candidates) {
    if (any_match && !candidate.matches_expected) continue;
    // Newest by date, and only by date. A date is always present and always
    // means what it says; a version in a file name is a convention that holds
    // until the day a package is called something else, and the folder this
    // reads has three subfolders of old packages in it precisely because
    // dannyldd's naming has changed before.
    if (!best || candidate.date > best->date) best = &candidate;
  }
  if (!best) return {};
  if (picked_name) *picked_name = best->name;
  return best->url;
}

}  // namespace ul

// ------------------------------------------------------------------- the ABI

namespace {

/// Where the anonymous token comes from. The request names an application and
/// nothing else — no user, no share, no state — and the token is fetched
/// fresh each run rather than cached, because a cache would be one more thing
/// that can be stale in a way nobody can see.
constexpr char kBadgerUrl[] = "https://api-badgerp.svc.ms/v1.0/token";
/// The application ids the token can be asked for under. The first is the one
/// verified against the live service; the second is a fallback, because an
/// application id is exactly the kind of thing that gets retired.
const char* const kBadgerBodies[] = {
    "{\"appId\":\"00000000-0000-0000-0000-0000481710a4\"}",
    "{\"appId\":\"5cbed6ac-a083-4e14-b191-b4ba07653de2\"}",
};

constexpr char kPersonalHost[] = "https://my.microsoftpersonalcontent.com";

}  // namespace

extern "C" {

char* ul_direct_download_url(const char* share_url) {
  if (!share_url) return nullptr;
  return ul::Duplicate(ul::DirectDownloadUrl(share_url));
}

int ul_url_is_folder(const char* url) {
  return (url && ul::UrlIsFolder(url)) ? 1 : 0;
}

const char* ul_onedrive_badger_url(void) { return kBadgerUrl; }

const char* ul_onedrive_badger_body(int attempt) {
  const int count = static_cast<int>(sizeof(kBadgerBodies) / sizeof(kBadgerBodies[0]));
  if (attempt < 0 || attempt >= count) return "";
  return kBadgerBodies[attempt];
}

char* ul_onedrive_read_badger(const char* json, size_t length) {
  if (!json) return nullptr;
  ul::Json document;
  if (!ul::Json::Parse(json, length, document)) {
    ul::SetLastError("The sign-in service answered with something unreadable.");
    return nullptr;
  }
  const std::string token = document.Str("token");
  if (token.empty()) {
    ul::SetLastError("The sign-in service returned no token.");
    return nullptr;
  }
  return ul::Duplicate(token);
}

char* ul_onedrive_item_url(const char* share_url) {
  if (!share_url || !*share_url) return nullptr;
  // $select kept to what is actually read. The web client asks for more, and
  // every extra field is another shape that can change under this.
  const std::string url = std::string(kPersonalHost) + "/_api/v2.0/shares/" +
                          ul::SharingToken(ul::Trim(share_url)) +
                          "/driveitem?$select=id,parentReference,folder,name";
  return ul::Duplicate(url);
}

int ul_onedrive_read_item(const char* json, size_t length, char** drive_id,
                          char** item_id) {
  if (drive_id) *drive_id = nullptr;
  if (item_id) *item_id = nullptr;
  if (!json) return UL_ERR_PARSE;
  ul::Json document;
  if (!ul::Json::Parse(json, length, document)) {
    ul::SetLastError("OneDrive answered with something unreadable.");
    return UL_ERR_PARSE;
  }
  if (const ul::Json* error = document.Find("error")) {
    // 403 accessDenied here almost certainly means the `Prefer: autoredeem`
    // header did not reach the server. Said plainly, because the alternative is
    // an hour spent believing the link is private.
    ul::SetLastError("OneDrive refused the link: " + error->Str("message"));
    return UL_ERR_PARSE;
  }
  const std::string item = document.Str("id");
  std::string drive;
  if (const ul::Json* parent = document.Find("parentReference")) {
    drive = parent->Str("driveId");
  }
  if (item.empty() || drive.empty()) {
    ul::SetLastError("OneDrive did not say where that folder is.");
    return UL_ERR_PARSE;
  }
  if (!document.Find("folder")) {
    // A file share, not a folder. The caller should have downloaded it.
    ul::SetLastError("That link points at a file, not a folder.");
    return UL_ERR_PARSE;
  }
  if (drive_id) *drive_id = ul::Duplicate(drive);
  if (item_id) *item_id = ul::Duplicate(item);
  return UL_OK;
}

char* ul_onedrive_children_url(const char* drive_id, const char* item_id) {
  if (!drive_id || !item_id || !*drive_id || !*item_id) return nullptr;
  // $top well above the five things in the folder today, so a growing share
  // does not start needing a second page nobody wrote the code for.
  const std::string url = std::string(kPersonalHost) + "/_api/v2.0/drives/" + drive_id +
                          "/items/" + item_id +
                          "/children?$top=500&$select=name,size,file,folder,"
                          "lastModifiedDateTime,@content.downloadUrl";
  return ul::Duplicate(url);
}

char* ul_folder_pick_archive(const char* json, size_t length,
                             const char* expected_version, char** picked_name) {
  if (picked_name) *picked_name = nullptr;
  if (!json) return nullptr;
  std::string name;
  const std::string picked = ul::PickArchiveFromListing(
      std::string(json, length), expected_version ? expected_version : "", &name);
  if (picked.empty()) return nullptr;
  if (picked_name && !name.empty()) *picked_name = ul::Duplicate(name);
  return ul::Duplicate(picked);
}

}  // extern "C"

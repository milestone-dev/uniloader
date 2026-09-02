// Comparing releases, and finding the version in a file name.
//
// dannyldd's releases are "v6.6", "v6.5", "v6.4"; the packages before that were
// dated, and the archives in the OneDrive folder have carried both. Nothing
// here assumes semver, because none of it is.

#include "uniloader/uniloader.h"

#include "util.hpp"
#include "version.hpp"

#include <cctype>
#include <cstdlib>
#include <string>
#include <vector>

namespace ul {
namespace {

/// Every run of digits in a string, as numbers.
///
/// Numeric rather than lexicographic, which is the whole point: the mod is at
/// v6.6 and shipping regularly, so v6.10 is coming, and a string compare puts
/// it *before* v6.9 — an update that silently never offers itself. Leading
/// zeroes fall out for free, so "v06.6" and "v6.6" are the same release.
std::vector<long long> DigitRuns(const std::string& s) {
  std::vector<long long> runs;
  size_t i = 0;
  while (i < s.size()) {
    if (s[i] < '0' || s[i] > '9') { ++i; continue; }
    long long value = 0;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
      // Saturate rather than overflow. A version is not a date stamp with
      // nineteen digits in it, but a file name in a shared folder can be.
      if (value < 1000000000000LL) value = value * 10 + (s[i] - '0');
      ++i;
    }
    runs.push_back(value);
  }
  return runs;
}

}  // namespace

int CompareVersions(const std::string& a, const std::string& b) {
  const std::vector<long long> left = DigitRuns(a);
  const std::vector<long long> right = DigitRuns(b);
  const size_t count = left.size() > right.size() ? left.size() : right.size();
  for (size_t i = 0; i < count; ++i) {
    // A missing component is zero, so "6.6" and "6.6.0" are one release rather
    // than two, and a user on "6.6" is not offered "6.6.0" every time it polls.
    const long long l = i < left.size() ? left[i] : 0;
    const long long r = i < right.size() ? right[i] : 0;
    if (l != r) return l < r ? -1 : 1;
  }
  // Same numbers, same release — whatever else the two strings say. "v6.6",
  // "6.6" and "Unification v6.6" are one release named three ways, and they are
  // all three in use: the mod page says "v6.6", the archive is "war2_unif_v6_6"
  // and a receipt written by an older build may hold either. Falling back to a
  // text compare here would make one of those look newer than another and offer
  // an update that reinstalls what is already there.
  return 0;
}

std::string VersionFromFilename(const std::string& filename) {
  // The name without its extension: ".rar" would otherwise contribute no
  // digits, but "part2.rar" and "v6.6.r00" would contribute the wrong ones.
  std::string stem = BaseName(filename);
  const size_t dot = stem.find_last_of('.');
  if (dot != std::string::npos) stem = stem.substr(0, dot);

  // A 'v' immediately before digits is the strongest signal there is, and it is
  // what every one of dannyldd's names uses: "war2_unif_v6_6", "Unification
  // v3.4.1". Taken first so that a name carrying another number as well —
  // "war2_unif_v6_6" has a 2 in "war2" — does not pick up the wrong one.
  const std::string lowered = Lower(stem);
  for (size_t i = 0; i < lowered.size(); ++i) {
    if (lowered[i] != 'v') continue;
    if (i > 0 && (std::isalnum(static_cast<unsigned char>(lowered[i - 1])) != 0)) continue;
    size_t j = i + 1;
    if (j >= lowered.size() || lowered[j] < '0' || lowered[j] > '9') continue;
    std::string version;
    while (j < lowered.size()) {
      const char c = lowered[j];
      if (c >= '0' && c <= '9') {
        version.push_back(c);
      } else if ((c == '.' || c == '_' || c == '-') && j + 1 < lowered.size() &&
                 lowered[j + 1] >= '0' && lowered[j + 1] <= '9') {
        // Separators inside a version are interchangeable in the wild:
        // "v6_6" and "v6.6" are the same release, named by different tools.
        version.push_back('.');
      } else {
        break;
      }
      ++j;
    }
    if (!version.empty()) return version;
  }

  // No 'v'. Take the longest dotted run, which is what a bare "3.4.1" is.
  std::string best;
  size_t i = 0;
  while (i < stem.size()) {
    if (stem[i] < '0' || stem[i] > '9') { ++i; continue; }
    std::string run;
    size_t j = i;
    while (j < stem.size()) {
      const char c = stem[j];
      if (c >= '0' && c <= '9') {
        run.push_back(c);
      } else if (c == '.' && j + 1 < stem.size() && stem[j + 1] >= '0' &&
                 stem[j + 1] <= '9') {
        run.push_back('.');
      } else {
        break;
      }
      ++j;
    }
    if (run.size() > best.size()) best = run;
    i = j > i ? j : i + 1;
  }
  return best;
}

}  // namespace ul

// ------------------------------------------------------------------- the ABI

extern "C" {

int ul_version_compare(const char* a, const char* b) {
  return ul::CompareVersions(a ? a : "", b ? b : "");
}

char* ul_version_from_filename(const char* filename) {
  if (!filename) return nullptr;
  const std::string version = ul::VersionFromFilename(filename);
  if (version.empty()) return nullptr;
  return ul::Duplicate(version);
}

int ul_update_available(const char* installed, const char* latest) {
  if (!latest || !*latest) return 0;          // nothing offered is not an update
  if (!installed || !*installed) return 1;    // nothing installed always is
  return ul::CompareVersions(installed, latest) < 0 ? 1 : 0;
}

const char* ul_last_error(void) { return ul::LastError(); }

void ul_free(void* p) { std::free(p); }

const char* ul_version(void) { return UL_CORE_VERSION; }

const char* ul_error_text(int code) {
  switch (code) {
    case UL_OK:              return "OK";
    case UL_ERR_OPEN:        return "The file could not be opened, or is not an archive.";
    case UL_ERR_FORMAT:      return "The archive is damaged, or is in a format this version cannot read.";
    case UL_ERR_WRITE:       return "A file could not be written. Check free space and permissions.";
    case UL_ERR_CANCELLED:   return "Cancelled.";
    case UL_ERR_UNSAFE_PATH: return "The archive contains a file path that points outside the install folder, and was not unpacked.";
    case UL_ERR_ENCRYPTED:   return "The archive is password-protected.";
    case UL_ERR_PARSE:       return "The server replied, but not with anything this version understands.";
    case UL_ERR_NO_ARCHIVE:  return "A release was found, but no download link for it.";
    default:                 return "Unknown error.";
  }
}

}  // extern "C"

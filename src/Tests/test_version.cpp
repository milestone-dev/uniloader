// Ordering releases, and reading a version out of a file name.
//
// The cases here are not invented: they are the names dannyldd's packages have
// actually carried, plus the one that has not happened yet and will — v6.10.

#include "harness.hpp"
#include "uniloader/uniloader.h"

#include <string>

namespace {

std::string VersionOf(const char* filename) {
  char* version = ul_version_from_filename(filename);
  const std::string result = version ? version : "";
  ul_free(version);
  return result;
}

}  // namespace

TEST(version, orders_numerically_not_lexically) {
  // The whole reason this is not strcmp. The mod is at v6.6 and shipping every
  // few weeks; the first time it reaches v6.10 a string compare would call it
  // older than v6.9 and never offer the update again.
  CHECK(ul_version_compare("v6.9", "v6.10") < 0);
  CHECK(ul_version_compare("v6.10", "v6.9") > 0);
  CHECK(ul_version_compare("v6.6", "v6.6") == 0);
  CHECK(ul_version_compare("v5.9", "v6.0") < 0);
}

TEST(version, ignores_decoration) {
  CHECK(ul_version_compare("v6.6", "6.6") == 0);
  CHECK(ul_version_compare("V6.6", "v6.6") == 0);
  CHECK(ul_version_compare("Unification v6.6", "v6.6") == 0);
  // Trailing zeroes are not a new release. Someone on "6.6" being offered
  // "6.6.0" on every poll would learn to ignore the notice.
  CHECK(ul_version_compare("6.6", "6.6.0") == 0);
}

TEST(version, update_available) {
  CHECK(ul_update_available("v6.5", "v6.6") == 1);
  CHECK(ul_update_available("v6.6", "v6.6") == 0);
  CHECK(ul_update_available("v6.7", "v6.6") == 0);   // a local build is not behind
  CHECK(ul_update_available("", "v6.6") == 1);       // nothing installed
  CHECK(ul_update_available(nullptr, "v6.6") == 1);
  // Nothing offered is not an update, however little is installed. A failed
  // fetch must not read as "you are up to date" *or* as "there is an update".
  CHECK(ul_update_available("v6.6", "") == 0);
  CHECK(ul_update_available("", "") == 0);
}

TEST(version, from_filename) {
  // The real name of the file attached to the mod page.
  CHECK_EQ(VersionOf("war2_unif_v1_1.rar"), std::string("1.1"));
  CHECK_EQ(VersionOf("war2_unif_v6_6.rar"), std::string("6.6"));
  CHECK_EQ(VersionOf("Unification mod v3.4.1.rar"), std::string("3.4.1"));
  CHECK_EQ(VersionOf("Unification v6.6.rar"), std::string("6.6"));
  // "war2" has a digit in it and must not win. This is why a 'v' prefix is
  // looked for before any bare run of digits.
  CHECK_EQ(VersionOf("war2_unif_v2_0.rar"), std::string("2.0"));
  CHECK_EQ(VersionOf("Unification 3.4.rar"), std::string("3.4"));
  CHECK_EQ(VersionOf("readme.txt"), std::string());
}

TEST(version, filename_ignores_the_extension) {
  // ".rar" contributes no digits, but a multi-volume ".part2.rar" does, and it
  // is not the version.
  CHECK_EQ(VersionOf("Unification v6.6.part2.rar"), std::string("6.6"));
}

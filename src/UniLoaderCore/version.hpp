// Ordering releases. The rules, and why they are these rules, are in version.cpp.

#pragma once

#include <string>

namespace ul {

/// <0, 0 or >0 as `a` orders before, with, or after `b`. Numeric per component,
/// so v6.10 is after v6.9.
int CompareVersions(const std::string& a, const std::string& b);

/// The version a file name carries, or "" when it carries none.
std::string VersionFromFilename(const std::string& filename);

}  // namespace ul

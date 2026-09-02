// Text, and the two conversions between the core's world and Win32's.
//
// The core speaks UTF-8 everywhere; the client is a Unicode application. These
// are the only two places that difference is handled, and every string that
// crosses goes through one of them.

#pragma once

#include <windows.h>

#include <string>

namespace ulwin {

std::wstring FromUtf8(const std::string& utf8);
std::string ToUtf8(const std::wstring& wide);

/// A string from Strings.rc. Never fails: an id with no entry comes back as
/// "!<id>", which is visible in a screenshot rather than an empty label nobody
/// notices until a user asks what the blank space is for.
std::wstring Text(UINT id);

/// Text(id) with the arguments substituted, the way wsprintf would.
std::wstring Format(UINT id, ...);

/// A byte count as a person would say it: "412 MB", "1.2 GB". Used in the
/// download line, where the number moves every few hundred milliseconds and
/// exact bytes would be unreadable.
std::wstring Bytes(int64_t count);

}  // namespace ulwin

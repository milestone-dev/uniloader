#include "Strings.hpp"

#include <cstdarg>
#include <cstdio>
#include <vector>

namespace ulwin {
namespace {

/// The module the strings live in. Cached rather than asked for each time,
/// which matters only because Text() is called from paint handlers.
HINSTANCE Module() {
  static HINSTANCE module = GetModuleHandleW(nullptr);
  return module;
}

}  // namespace

std::wstring FromUtf8(const std::string& utf8) {
  if (utf8.empty()) return {};
  const int needed = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(),
                                         static_cast<int>(utf8.size()), nullptr, 0);
  if (needed <= 0) return {};
  std::wstring wide(static_cast<size_t>(needed), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()),
                      &wide[0], needed);
  return wide;
}

std::string ToUtf8(const std::wstring& wide) {
  if (wide.empty()) return {};
  const int needed = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(),
                                         static_cast<int>(wide.size()), nullptr, 0,
                                         nullptr, nullptr);
  if (needed <= 0) return {};
  std::string utf8(static_cast<size_t>(needed), '\0');
  WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()),
                      &utf8[0], needed, nullptr, nullptr);
  return utf8;
}

std::wstring Text(UINT id) {
  // The pointer form of LoadStringW: it hands back the resource itself rather
  // than copying into a buffer somebody had to size, and the length with it.
  const wchar_t* found = nullptr;
  const int length = LoadStringW(Module(), id, reinterpret_cast<LPWSTR>(&found), 0);
  if (length <= 0 || !found) return L"!" + std::to_wstring(id);
  return std::wstring(found, static_cast<size_t>(length));
}

std::wstring Format(UINT id, ...) {
  const std::wstring pattern = Text(id);
  va_list arguments;
  va_start(arguments, id);
  // Sized rather than assumed: one of these carries a file path and another a
  // changelog, and a fixed buffer would truncate exactly the messages that
  // matter most. _vscwprintf asks how long the answer is before writing it.
  const int needed = _vscwprintf(pattern.c_str(), arguments);
  va_end(arguments);
  if (needed < 0) return pattern;

  std::vector<wchar_t> buffer(static_cast<size_t>(needed) + 1);
  va_start(arguments, id);
  vswprintf_s(buffer.data(), buffer.size(), pattern.c_str(), arguments);
  va_end(arguments);
  return std::wstring(buffer.data());
}

std::wstring Bytes(int64_t count) {
  if (count < 0) return L"?";
  const wchar_t* units[] = {L"bytes", L"KB", L"MB", L"GB"};
  double value = static_cast<double>(count);
  int unit = 0;
  while (value >= 1024.0 && unit + 1 < 4) {
    value /= 1024.0;
    ++unit;
  }
  wchar_t buffer[64];
  // No decimals below a megabyte: a download line that reads "412.0 MB" is
  // fine and one that reads "17.0 bytes" is not.
  if (unit == 0) {
    swprintf_s(buffer, L"%lld %s", static_cast<long long>(count), units[unit]);
  } else {
    swprintf_s(buffer, L"%.1f %s", value, units[unit]);
  }
  return buffer;
}

}  // namespace ulwin

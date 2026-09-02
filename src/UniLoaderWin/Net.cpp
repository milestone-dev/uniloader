#include "Net.hpp"

#include "Strings.hpp"

#include <windows.h>

#include <shellapi.h>
#include <winhttp.h>

#include <uniloader/uniloader.h>

#include <vector>

namespace ulwin {
namespace {

/// A plain, honest user agent. GameBanana's API answers anonymously and has no
/// reason to care, but a request with no agent at all is the kind of thing a
/// CDN blocks on a bad day, and naming the program means an operator who sees
/// the traffic can find out what it is.
constexpr wchar_t kUserAgent[] = L"UniLoader/1.0 (+https://gamebanana.com/mods/644456)";

struct Handle {
  HINTERNET value = nullptr;
  ~Handle() {
    if (value) WinHttpCloseHandle(value);
  }
  Handle() = default;
  explicit Handle(HINTERNET handle) : value(handle) {}
  Handle(const Handle&) = delete;
  Handle& operator=(const Handle&) = delete;
  explicit operator bool() const { return value != nullptr; }
};

std::wstring SystemError(DWORD code) {
  // WinHTTP's own errors are not in the default message table; its module has
  // to be named or every network failure reads "The operation completed
  // successfully", which is worse than a number.
  wchar_t* text = nullptr;
  const HMODULE winhttp = GetModuleHandleW(L"winhttp.dll");
  const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                      FORMAT_MESSAGE_IGNORE_INSERTS |
                      (winhttp ? FORMAT_MESSAGE_FROM_HMODULE : 0u);
  const DWORD length = FormatMessageW(flags, winhttp, code, 0,
                                      reinterpret_cast<LPWSTR>(&text), 0, nullptr);
  std::wstring message = length && text ? std::wstring(text, length) : L"";
  if (text) LocalFree(text);
  while (!message.empty() && (message.back() == L'\n' || message.back() == L'\r' ||
                              message.back() == L' ')) {
    message.pop_back();
  }
  if (message.empty()) message = L"error " + std::to_wstring(code);
  return message;
}

struct Url {
  std::wstring host;
  std::wstring path;
  INTERNET_PORT port = INTERNET_DEFAULT_HTTPS_PORT;
  bool secure = true;
  bool ok = false;
};

Url Split(const std::wstring& url) {
  Url parts;
  URL_COMPONENTS components = {};
  components.dwStructSize = sizeof(components);
  components.dwHostNameLength = 1;
  components.dwUrlPathLength = 1;
  components.dwExtraInfoLength = 1;
  components.dwSchemeLength = 1;
  if (!WinHttpCrackUrl(url.c_str(), static_cast<DWORD>(url.size()), 0, &components)) {
    return parts;
  }
  parts.host.assign(components.lpszHostName, components.dwHostNameLength);
  parts.path.assign(components.lpszUrlPath, components.dwUrlPathLength);
  // The query string is part of what is requested. Dropping it is how
  // "?download=1" — the whole of a OneDrive rewrite — goes missing.
  parts.path.append(components.lpszExtraInfo, components.dwExtraInfoLength);
  parts.port = components.nPort;
  parts.secure = components.nScheme == INTERNET_SCHEME_HTTPS;
  parts.ok = !parts.host.empty();
  if (parts.path.empty()) parts.path = L"/";
  return parts;
}

/// Opens a request and reads its headers. The three handles are returned by
/// reference because they have to outlive this call — the body is read from
/// `request`, and closing `session` first would close it underneath.
bool Begin(const std::wstring& url, Handle& session, Handle& connection,
           Handle& request, FetchResult& result, const FetchOptions* options) {
  const Url parts = Split(url);
  if (!parts.ok) {
    result.error = L"That does not look like a web address: " + url;
    return false;
  }

  session.value = WinHttpOpen(kUserAgent, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                              WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!session) {
    // Automatic proxy detection needs Windows 8.1. Falling back rather than
    // failing keeps this working on the older machines a good share of the
    // people still playing this game are on.
    session.value = WinHttpOpen(kUserAgent, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  }
  if (!session) {
    result.error = SystemError(GetLastError());
    return false;
  }
  // Generous, and deliberately so: this runs against a CDN over whatever
  // connection the user has, and the failure mode of a short timeout is a
  // download that never completes on a slow line.
  WinHttpSetTimeouts(session.value, 15000, 15000, 30000, 30000);

  connection.value = WinHttpConnect(session.value, parts.host.c_str(), parts.port, 0);
  if (!connection) {
    result.error = SystemError(GetLastError());
    return false;
  }

  const wchar_t* method = (options && !options->method.empty())
                              ? options->method.c_str() : L"GET";
  request.value = WinHttpOpenRequest(
      connection.value, method, parts.path.c_str(), nullptr, WINHTTP_NO_REFERER,
      WINHTTP_DEFAULT_ACCEPT_TYPES, parts.secure ? WINHTTP_FLAG_SECURE : 0);
  if (!request) {
    result.error = SystemError(GetLastError());
    return false;
  }
  // Redirects are the mechanism, not an edge case: a GameBanana /dl/ link
  // redirects to a filecache host, and a OneDrive link redirects twice more.
  DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
  WinHttpSetOption(request.value, WINHTTP_OPTION_REDIRECT_POLICY, &policy,
                   sizeof(policy));

  std::wstring extra;
  if (options) {
    for (const std::wstring& header : options->headers) {
      if (header.empty()) continue;
      extra += header;
      extra += L"\r\n";
    }
  }
  const std::string* body = options ? &options->body : nullptr;
  if (!WinHttpSendRequest(
          request.value,
          extra.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : extra.c_str(),
          extra.empty() ? 0 : static_cast<DWORD>(-1),
          (body && !body->empty()) ? const_cast<char*>(body->data())
                                   : WINHTTP_NO_REQUEST_DATA,
          body ? static_cast<DWORD>(body->size()) : 0,
          body ? static_cast<DWORD>(body->size()) : 0, 0) ||
      !WinHttpReceiveResponse(request.value, nullptr)) {
    result.error = SystemError(GetLastError());
    return false;
  }

  DWORD status = 0, size = sizeof(status);
  WinHttpQueryHeaders(request.value,
                      WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                      WINHTTP_HEADER_NAME_BY_INDEX, &status, &size,
                      WINHTTP_NO_HEADER_INDEX);
  result.status = status;
  if (status < 200 || status >= 300) {
    // The body of a 4xx from these services names the reason — "accessDenied",
    // "unauthenticated" — and reading it is the difference between a number and
    // a diagnosis. Read here rather than by the caller, which has already given
    // up by the time it sees the result.
    std::string detail;
    std::vector<char> buffer(4096);
    DWORD read = 0;
    if (WinHttpReadData(request.value, buffer.data(),
                        static_cast<DWORD>(buffer.size()), &read) && read > 0) {
      detail.assign(buffer.data(), read);
    }
    result.error = L"The server answered " + std::to_wstring(status) + L".";
    if (!detail.empty()) {
      result.body = detail;
      result.error += L" " + FromUtf8(detail.substr(0, 300));
    }
    return false;
  }

  DWORD url_size = 0;
  WinHttpQueryOption(request.value, WINHTTP_OPTION_URL, nullptr, &url_size);
  if (url_size > 0) {
    std::vector<wchar_t> buffer(url_size / sizeof(wchar_t) + 1);
    if (WinHttpQueryOption(request.value, WINHTTP_OPTION_URL, buffer.data(), &url_size)) {
      result.final_url = buffer.data();
    }
  }
  return true;
}

int64_t ContentLength(HINTERNET request) {
  wchar_t buffer[64] = {};
  DWORD size = sizeof(buffer);
  if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH,
                           WINHTTP_HEADER_NAME_BY_INDEX, buffer, &size,
                           WINHTTP_NO_HEADER_INDEX)) {
    return -1;   // chunked, or the server did not say
  }
  return _wtoi64(buffer);
}

}  // namespace

FetchResult FetchToMemory(const std::wstring& url, size_t limit) {
  return FetchToMemory(url, FetchOptions{}, limit);
}

FetchResult FetchToMemory(const std::wstring& url, const FetchOptions& options,
                          size_t limit) {
  FetchResult result;
  Handle session, connection, request;
  if (!Begin(url, session, connection, request, result, &options)) return result;

  std::string body;
  std::vector<char> buffer(64 * 1024);
  for (;;) {
    DWORD read = 0;
    if (!WinHttpReadData(request.value, buffer.data(),
                         static_cast<DWORD>(buffer.size()), &read)) {
      result.error = SystemError(GetLastError());
      return result;
    }
    if (read == 0) break;
    if (body.size() + read > limit) {
      result.error = L"That link answered with far more data than a mod page. "
                     L"It was not read.";
      return result;
    }
    body.append(buffer.data(), read);
  }
  result.body = std::move(body);
  result.bytes = static_cast<int64_t>(result.body.size());
  result.ok = true;
  return result;
}

FetchResult FetchToFile(const std::wstring& url, const std::wstring& path,
                        const ProgressCallback& progress) {
  FetchResult result;
  Handle session, connection, request;
  if (!Begin(url, session, connection, request, result, nullptr)) return result;

  const int64_t total = ContentLength(request.value);
  HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    result.error = L"Could not write to " + path + L": " + SystemError(GetLastError());
    return result;
  }

  ul_sha256* hash = ul_sha256_create();
  std::vector<char> buffer(256 * 1024);
  int64_t done = 0;
  bool failed = false;
  bool cancelled = false;
  for (;;) {
    DWORD read = 0;
    if (!WinHttpReadData(request.value, buffer.data(),
                         static_cast<DWORD>(buffer.size()), &read)) {
      result.error = SystemError(GetLastError());
      failed = true;
      break;
    }
    if (read == 0) break;
    DWORD written = 0;
    if (!WriteFile(file, buffer.data(), read, &written, nullptr) || written != read) {
      result.error = L"Could not write to " + path + L": " +
                     SystemError(GetLastError());
      failed = true;
      break;
    }
    ul_sha256_update(hash, buffer.data(), read);
    done += read;
    if (progress && !progress(done, total)) {
      cancelled = true;
      break;
    }
  }

  char digest[65] = {};
  ul_sha256_finish(hash, digest);
  CloseHandle(file);

  if (failed || cancelled) {
    // Deleted rather than kept. Half a package on disk is something a later run
    // would have to be clever about, and being clever about it is exactly how a
    // resumed download ends up installing a mixture of two versions.
    DeleteFileW(path.c_str());
    if (cancelled) result.error.clear();
    return result;
  }
  // A server that closed early leaves a file that is short and otherwise
  // perfectly valid — the failure has to be caught here or it is caught by the
  // extractor, as a corrupt archive, which reads like a bad upload.
  if (total >= 0 && done != total) {
    DeleteFileW(path.c_str());
    result.error = L"The download stopped early: " + Bytes(done) + L" of " +
                   Bytes(total) + L" arrived.";
    return result;
  }
  result.sha256 = digest;
  result.bytes = done;
  result.ok = true;
  return result;
}

void OpenInBrowser(const std::wstring& url) {
  // Only ever http(s), and only ever a URL that came out of the core's own
  // parsing of the mod page. ShellExecute on an arbitrary string is a way to
  // run a program, so the scheme is checked here rather than assumed upstream.
  if (url.rfind(L"https://", 0) != 0 && url.rfind(L"http://", 0) != 0) return;
  ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

}  // namespace ulwin

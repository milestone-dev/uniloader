#include "Files.hpp"

#include "Strings.hpp"

#include <shellapi.h>   // ShellExecuteEx, for Play and the elevated relaunch
#include <shlobj.h>
#include <tlhelp32.h>

#include <uniloader/uniloader.h>

#include <algorithm>

namespace ulwin {
namespace {

std::wstring Widen(const char* utf8) { return FromUtf8(utf8 ? utf8 : ""); }

/// Backslashes, for the API. The core hands out forward slashes and Windows
/// accepts either, but an error message is read by a person and a path with
/// both in it looks broken.
std::wstring Native(std::wstring path) {
  for (wchar_t& c : path) {
    if (c == L'/') c = L'\\';
  }
  return path;
}

std::wstring SystemError(DWORD code) {
  wchar_t* text = nullptr;
  const DWORD length = FormatMessageW(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
          FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr, code, 0, reinterpret_cast<LPWSTR>(&text), 0, nullptr);
  std::wstring message = length && text ? std::wstring(text, length) : L"";
  if (text) LocalFree(text);
  while (!message.empty() && (message.back() == L'\n' || message.back() == L'\r' ||
                              message.back() == L' ')) {
    message.pop_back();
  }
  if (message.empty()) message = L"error " + std::to_wstring(code);
  return message;
}

void WalkInto(const std::wstring& root, const std::wstring& relative,
              std::vector<std::string>& out) {
  const std::wstring pattern =
      Native(root) + (relative.empty() ? L"\\*" : L"\\" + relative + L"\\*");
  WIN32_FIND_DATAW found = {};
  HANDLE handle = FindFirstFileW(pattern.c_str(), &found);
  if (handle == INVALID_HANDLE_VALUE) return;
  do {
    const std::wstring name = found.cFileName;
    if (name == L"." || name == L"..") continue;
    const std::wstring child = relative.empty() ? name : relative + L"\\" + name;
    if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
      // Reparse points are not followed. A junction inside a package would let
      // an extraction write outside the folder it was unpacked into, which is
      // the same hole the core refuses ".." for.
      if ((found.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) continue;
      WalkInto(root, child, out);
    } else {
      std::wstring forward = child;
      for (wchar_t& c : forward) {
        if (c == L'\\') c = L'/';
      }
      out.push_back(ToUtf8(forward));
    }
  } while (FindNextFileW(handle, &found));
  FindClose(handle);
}

}  // namespace

std::vector<std::string> WalkFiles(const std::wstring& root) {
  std::vector<std::string> files;
  if (!root.empty()) WalkInto(root, L"", files);
  // Sorted so that a plan built from two walks of the same tree is the same
  // plan. FindFirstFile's order is the filesystem's, not a promise.
  std::sort(files.begin(), files.end());
  return files;
}

bool FileExists(const std::wstring& path) {
  const DWORD attributes = GetFileAttributesW(Native(path).c_str());
  return attributes != INVALID_FILE_ATTRIBUTES &&
         (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool FolderExists(const std::wstring& path) {
  const DWORD attributes = GetFileAttributesW(Native(path).c_str());
  return attributes != INVALID_FILE_ATTRIBUTES &&
         (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool EnsureFolder(const std::wstring& path) {
  const std::wstring native = Native(path);
  if (native.empty()) return false;
  if (FolderExists(native)) return true;
  // Parents first. SHCreateDirectoryExW does this in one call, but it is in
  // shell32 and refuses a relative path, and the recursion is three lines.
  const size_t slash = native.find_last_of(L'\\');
  if (slash != std::wstring::npos && slash > 2) {
    EnsureFolder(native.substr(0, slash));
  }
  if (CreateDirectoryW(native.c_str(), nullptr)) return true;
  return GetLastError() == ERROR_ALREADY_EXISTS;
}

bool RemoveTree(const std::wstring& path) {
  const std::wstring native = Native(path);
  if (native.empty() || !FolderExists(native)) return true;
  WIN32_FIND_DATAW found = {};
  HANDLE handle = FindFirstFileW((native + L"\\*").c_str(), &found);
  if (handle != INVALID_HANDLE_VALUE) {
    do {
      const std::wstring name = found.cFileName;
      if (name == L"." || name == L"..") continue;
      const std::wstring child = native + L"\\" + name;
      if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
          (found.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0) {
        RemoveTree(child);
      } else {
        SetFileAttributesW(child.c_str(), FILE_ATTRIBUTE_NORMAL);
        DeleteFileW(child.c_str());
      }
    } while (FindNextFileW(handle, &found));
    FindClose(handle);
  }
  return RemoveDirectoryW(native.c_str()) != 0;
}

bool IsWritable(const std::wstring& folder) {
  if (!FolderExists(folder)) return false;
  // By trying. The answer depends on the folder's ACL, on whether the process
  // is elevated, on installer virtualisation and on whether the volume is
  // read-only, and no reading of the first of those predicts the rest.
  const std::wstring probe = Native(folder) + L"\\uniloader-write-test.tmp";
  HANDLE file = CreateFileW(probe.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE,
                            nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  CloseHandle(file);
  return true;
}

std::wstring StoreFolder() {
  wchar_t* local = nullptr;
  std::wstring base;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local)) &&
      local) {
    base = local;
  }
  CoTaskMemFree(local);
  if (base.empty()) base = ModuleFolder();

  const std::wstring folder = base + L"\\UnificationMod";
  // The store used to be called UniLoader. An existing one is renamed rather
  // than abandoned: it holds the 800 MB package, the plugin library and the
  // receipt, and starting over because the folder changed its name would be
  // exactly the re-download and the orphaned install this store exists to
  // prevent. A rename on the same volume is instant — but it can be refused,
  // most plainly by an Explorer window sitting open somewhere inside, so a
  // refusal means the old folder stays the store for this run and the rename
  // is simply tried again next start.
  const std::wstring old = base + L"\\UniLoader";
  if (!FolderExists(old)) return folder;
  if (!FolderExists(folder)) {
    return MoveFileExW(old.c_str(), folder.c_str(), 0) ? folder : old;
  }
  // Both exist — a half-state an older build could leave behind. The store is
  // wherever the receipt is; a new folder holding no receipt is a shell, not
  // the store.
  if (FileExists(old + L"\\install.json") && !FileExists(folder + L"\\install.json")) {
    return old;
  }
  return folder;
}

bool GameIsRunning() {
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE) return false;
  PROCESSENTRY32W entry = {};
  entry.dwSize = sizeof(entry);
  bool running = false;
  if (Process32FirstW(snapshot, &entry)) {
    do {
      std::wstring name = entry.szExeFile;
      CharLowerW(&name[0]);
      // Every name the game runs under: the War2Combat launcher, the retail
      // executable, and the mod's own front end.
      if (name == L"war2.exe" || name == L"warcraft ii bne.exe" ||
          name == L"unification.exe" || name == L"war2ploader.exe") {
        running = true;
        break;
      }
    } while (Process32NextW(snapshot, &entry));
  }
  CloseHandle(snapshot);
  return running;
}

std::wstring PlanBlockedBy(const ul_plan* plan) {
  const int steps = ul_plan_count(plan);
  for (int i = 0; i < steps; ++i) {
    switch (ul_plan_op(plan, i)) {
      case UL_OP_COPY:
      case UL_OP_DELETE:
      case UL_OP_BACKUP:
      case UL_OP_RESTORE:
      case UL_OP_ARCHIVE:
        break;
      default:
        continue;   // MKDIR touches no file anyone can be holding
    }
    const std::wstring dest = Native(Widen(ul_plan_dest(plan, i)));
    // A file that is not there yet cannot be locked, and a copy that creates
    // one is not blocked by anything.
    if (dest.empty() || !FileExists(dest)) continue;
    // Opened for writing with no sharing at all: precisely what a copy over it
    // or a delete of it will need. Closed again at once — this asks whether the
    // handle can be had, it does not keep it.
    HANDLE file = CreateFileW(dest.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
      CloseHandle(file);
      continue;
    }
    const DWORD why = GetLastError();
    // Only the "somebody has it" errors. A missing parent or a bad name is not
    // this function's business and must not be reported as the game running.
    if (why == ERROR_SHARING_VIOLATION || why == ERROR_LOCK_VIOLATION) return dest;
  }
  return {};
}

int RunPlan(const ul_plan* plan, ul_receipt* receipt,
            const std::function<bool(int, int)>& progress, std::wstring& error) {
  const int steps = ul_plan_count(plan);
  for (int i = 0; i < steps; ++i) {
    const int op = ul_plan_op(plan, i);
    const std::wstring src = Native(Widen(ul_plan_src(plan, i)));
    const std::wstring dest = Native(Widen(ul_plan_dest(plan, i)));
    switch (op) {
      case UL_OP_MKDIR:
        if (!EnsureFolder(dest)) {
          error = L"Could not create " + dest + L": " + SystemError(GetLastError());
          return UL_ERR_WRITE;
        }
        break;
      case UL_OP_BACKUP: {
        EnsureFolder(dest.substr(0, dest.find_last_of(L'\\')));
        // Moved, not copied: a move is atomic on the same volume and costs
        // nothing on a 10 MB maindat.war, and the game folder is never briefly
        // holding two copies of a file the size of the game.
        if (!MoveFileExW(src.c_str(), dest.c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
          const DWORD code = GetLastError();
          if (code != ERROR_FILE_NOT_FOUND) {
            error = L"Could not set aside " + src + L": " + SystemError(code);
            return code == ERROR_SHARING_VIOLATION ? UL_ERR_WRITE : UL_ERR_WRITE;
          }
        }
        break;
      }
      case UL_OP_COPY:
        if (!CopyFileW(src.c_str(), dest.c_str(), FALSE)) {
          error = L"Could not copy " + src + L" to " + dest + L": " +
                  SystemError(GetLastError());
          return UL_ERR_WRITE;
        }
        // Written to the receipt as each one lands, not at the end: an install
        // that fails halfway has still changed the game folder, and the record
        // of what it changed is the only way back.
        if (receipt) {
          // The backup for this destination, when the step before was one.
          std::string backup;
          if (i > 0 && ul_plan_op(plan, i - 1) == UL_OP_BACKUP) {
            backup = ul_plan_dest(plan, i - 1);
          }
          ul_receipt_add(receipt, ul_plan_dest(plan, i), backup.c_str());
        }
        break;
      case UL_OP_DELETE:
        SetFileAttributesW(dest.c_str(), FILE_ATTRIBUTE_NORMAL);
        if (!DeleteFileW(dest.c_str())) {
          const DWORD code = GetLastError();
          // Already gone is the outcome asked for. A user who deleted the file
          // by hand should not be told the uninstall failed.
          if (code != ERROR_FILE_NOT_FOUND && code != ERROR_PATH_NOT_FOUND) {
            error = L"Could not delete " + dest + L": " + SystemError(code);
            return UL_ERR_WRITE;
          }
        }
        break;
      case UL_OP_ARCHIVE:
        EnsureFolder(dest.substr(0, dest.find_last_of(L'\\')));
        // Moved, not copied. The whole point is that the package exists once:
        // a copy here would put 1.2 GB in the store while the same 1.2 GB was
        // still sitting in the game folder waiting to be deleted.
        if (!MoveFileExW(src.c_str(), dest.c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
          const DWORD code = GetLastError();
          // Already gone is the outcome asked for: somebody deleted the file
          // by hand, and the game folder is that much closer to clean.
          if (code != ERROR_FILE_NOT_FOUND && code != ERROR_PATH_NOT_FOUND) {
            error = L"Could not move " + src + L" back: " + SystemError(code);
            return UL_ERR_WRITE;
          }
        }
        break;
      case UL_OP_RESTORE:
        EnsureFolder(dest.substr(0, dest.find_last_of(L'\\')));
        if (!MoveFileExW(src.c_str(), dest.c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
          const DWORD code = GetLastError();
          if (code != ERROR_FILE_NOT_FOUND) {
            error = L"Could not put " + dest + L" back: " + SystemError(code);
            return UL_ERR_WRITE;
          }
        }
        break;
      default:
        break;
    }
    if (progress && !progress(i + 1, steps)) return UL_ERR_CANCELLED;
  }
  return UL_OK;
}

namespace {

/// Depth-first, so a folder is only considered once everything inside it has
/// been. Returns whether `folder` ended up empty and was removed.
bool PruneEmpty(const std::wstring& folder) {
  WIN32_FIND_DATAW found = {};
  HANDLE handle = FindFirstFileW((folder + L"\\*").c_str(), &found);
  if (handle == INVALID_HANDLE_VALUE) return false;
  bool empty = true;
  std::vector<std::wstring> children;
  do {
    const std::wstring name = found.cFileName;
    if (name == L".") continue;
    if (name == L"..") continue;
    if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
        (found.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0) {
      children.push_back(folder + L"\\" + name);
    } else {
      empty = false;   // a file, or a junction this must not follow
    }
  } while (FindNextFileW(handle, &found));
  FindClose(handle);

  for (const std::wstring& child : children) {
    if (!PruneEmpty(child)) empty = false;
  }
  if (!empty) return false;
  return RemoveDirectoryW(folder.c_str()) != 0;
}

}  // namespace

void RemoveEmptyFolders(const std::wstring& root) {
  std::wstring native = Native(root);
  while (!native.empty() && native.back() == L'\\') native.pop_back();
  if (native.empty() || !FolderExists(native)) return;
  // The root is walked but never removed: it is the game folder, and it was
  // there before any of this.
  WIN32_FIND_DATAW found = {};
  HANDLE handle = FindFirstFileW((native + L"\\*").c_str(), &found);
  if (handle == INVALID_HANDLE_VALUE) return;
  std::vector<std::wstring> children;
  do {
    const std::wstring name = found.cFileName;
    if (name == L"." || name == L"..") continue;
    if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) continue;
    if ((found.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) continue;
    children.push_back(native + L"\\" + name);
  } while (FindNextFileW(handle, &found));
  FindClose(handle);
  for (const std::wstring& child : children) PruneEmpty(child);
}

bool IsElevated() {
  HANDLE token = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
  TOKEN_ELEVATION elevation = {};
  DWORD size = sizeof(elevation);
  const bool ok = GetTokenInformation(token, TokenElevation, &elevation, size, &size) != 0;
  CloseHandle(token);
  return ok && elevation.TokenIsElevated != 0;
}

bool RelaunchElevated(const std::wstring& arguments) {
  wchar_t path[MAX_PATH] = {};
  if (GetModuleFileNameW(nullptr, path, MAX_PATH) == 0) return false;
  SHELLEXECUTEINFOW info = {};
  info.cbSize = sizeof(info);
  info.fMask = SEE_MASK_NOCLOSEPROCESS;
  info.lpVerb = L"runas";
  info.lpFile = path;
  info.lpParameters = arguments.empty() ? nullptr : arguments.c_str();
  info.nShow = SW_SHOWNORMAL;
  if (!ShellExecuteExW(&info)) {
    // ERROR_CANCELLED is the user saying no at the UAC prompt. That is a
    // decision, not a failure, and it must not become an error message.
    return false;
  }
  if (info.hProcess) CloseHandle(info.hProcess);
  return true;
}

bool Launch(const std::wstring& executable, const std::wstring& working_folder) {
  SHELLEXECUTEINFOW info = {};
  info.cbSize = sizeof(info);
  info.fMask = SEE_MASK_NOASYNC;
  info.lpVerb = L"open";
  info.lpFile = executable.c_str();
  info.lpDirectory = working_folder.c_str();
  info.nShow = SW_SHOWNORMAL;
  return ShellExecuteExW(&info) != 0;
}

std::wstring ReadTextFile(const std::wstring& path) {
  HANDLE file = CreateFileW(Native(path).c_str(), GENERIC_READ, FILE_SHARE_READ,
                            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return {};
  LARGE_INTEGER size = {};
  GetFileSizeEx(file, &size);
  std::string bytes;
  if (size.QuadPart > 0 && size.QuadPart < 16 * 1024 * 1024) {
    bytes.resize(static_cast<size_t>(size.QuadPart));
    DWORD read = 0;
    ReadFile(file, &bytes[0], static_cast<DWORD>(bytes.size()), &read, nullptr);
    bytes.resize(read);
  }
  CloseHandle(file);
  // A BOM is not text. plugin.txt is written in Notepad as often as not, and
  // Notepad still writes one — left in, it becomes three stray characters at
  // the start of every plugin's name.
  if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xEF &&
      static_cast<unsigned char>(bytes[1]) == 0xBB &&
      static_cast<unsigned char>(bytes[2]) == 0xBF) {
    bytes.erase(0, 3);
  }
  return FromUtf8(bytes);
}

bool WriteTextFile(const std::wstring& path, const std::string& contents) {
  const std::wstring native = Native(path);
  const size_t slash = native.find_last_of(L'\\');
  if (slash != std::wstring::npos) EnsureFolder(native.substr(0, slash));
  HANDLE file = CreateFileW(native.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  DWORD written = 0;
  const bool ok = WriteFile(file, contents.data(),
                            static_cast<DWORD>(contents.size()), &written, nullptr) &&
                  written == contents.size();
  CloseHandle(file);
  return ok;
}

std::wstring ModuleFolder() {
  wchar_t path[MAX_PATH] = {};
  if (GetModuleFileNameW(nullptr, path, MAX_PATH) == 0) return L".";
  std::wstring folder = path;
  const size_t slash = folder.find_last_of(L'\\');
  return slash == std::wstring::npos ? L"." : folder.substr(0, slash);
}

}  // namespace ulwin

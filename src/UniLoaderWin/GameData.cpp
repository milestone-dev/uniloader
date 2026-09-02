#include "GameData.hpp"

#include "Files.hpp"
#include "Strings.hpp"
#include "strings.h"

#include <windows.h>

#include <shlobj.h>

#include <algorithm>

namespace ulwin {
namespace {

constexpr wchar_t kRegKey[] = L"Software\\UniLoader";
constexpr wchar_t kRegValue[] = L"GamePath";

/// Where the installers put it, tried in order when nothing is remembered — so
/// the common case never sees a dialog at all.
const wchar_t* kGuesses[] = {
    L"C:\\Program Files (x86)\\War2Combat",
    L"C:\\War2Combat",
    L"C:\\Games\\War2Combat",
    L"C:\\Program Files\\War2Combat",
};

/// Under Uninstall, which is where War2Combat's own installer records itself.
/// Matched on the display name because the key name is an installer string that
/// is not worth hard-coding.
const wchar_t* kUninstallRoots[] = {
    L"SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
    L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
};

std::wstring RegString(HKEY root, const wchar_t* path, const wchar_t* value) {
  HKEY key = nullptr;
  if (RegOpenKeyExW(root, path, 0, KEY_READ, &key) != ERROR_SUCCESS) return {};
  wchar_t buffer[MAX_PATH] = {};
  DWORD size = sizeof(buffer), type = 0;
  const LSTATUS status = RegQueryValueExW(key, value, nullptr, &type,
                                          reinterpret_cast<BYTE*>(buffer), &size);
  RegCloseKey(key);
  if (status != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) return {};
  return buffer;
}

/// Trailing separators, which War2Combat's uninstall entry has. Left on, they
/// turn every path built from one into a double backslash.
std::wstring Trimmed(std::wstring path) {
  while (!path.empty() && (path.back() == L'\\' || path.back() == L'/')) path.pop_back();
  return path;
}

bool ContainsNoCase(std::wstring haystack, std::wstring needle) {
  if (haystack.empty() || needle.empty()) return false;
  CharLowerW(&haystack[0]);
  CharLowerW(&needle[0]);
  return haystack.find(needle) != std::wstring::npos;
}

void AddIfGame(std::vector<std::wstring>& found, const std::wstring& folder) {
  if (folder.empty()) return;
  const std::wstring trimmed = Trimmed(folder);
  if (!IsGameFolder(trimmed)) return;
  for (const std::wstring& existing : found) {
    if (_wcsicmp(existing.c_str(), trimmed.c_str()) == 0) return;
  }
  found.push_back(trimmed);
}

void SearchUninstallKeys(std::vector<std::wstring>& found) {
  for (const wchar_t* root : kUninstallRoots) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, root, 0, KEY_READ, &key) != ERROR_SUCCESS) {
      continue;
    }
    for (DWORD index = 0;; ++index) {
      wchar_t name[512] = {};
      DWORD length = 512;
      if (RegEnumKeyExW(key, index, name, &length, nullptr, nullptr, nullptr,
                        nullptr) != ERROR_SUCCESS) {
        break;
      }
      const std::wstring path = std::wstring(root) + L"\\" + name;
      const std::wstring display = RegString(HKEY_LOCAL_MACHINE, path.c_str(),
                                             L"DisplayName");
      if (!ContainsNoCase(display, L"war2") && !ContainsNoCase(display, L"warcraft")) {
        continue;
      }
      AddIfGame(found, RegString(HKEY_LOCAL_MACHINE, path.c_str(), L"InstallLocation"));
    }
    RegCloseKey(key);
  }
}

}  // namespace

bool IsGameFolder(const std::wstring& folder) {
  if (folder.empty()) return false;
  // war2.exe is War2Combat's; the other is the retail Battle.net edition, which
  // a War2Combat install sits on top of. Either one means this is the place.
  return FileExists(folder + L"\\war2.exe") ||
         FileExists(folder + L"\\Warcraft II BNE.exe");
}

std::vector<std::wstring> FindGameFolders() {
  std::vector<std::wstring> found;
  // The remembered one first: it is the user's own answer, and re-asking a
  // question that has been answered is the thing this search exists to avoid.
  AddIfGame(found, RememberedGameFolder());
  // Then the folder this executable is sitting in, and its parent — dropping
  // the loader's exe into the game folder should be enough on its own.
  const std::wstring here = ModuleFolder();
  AddIfGame(found, here);
  const size_t slash = here.find_last_of(L'\\');
  if (slash != std::wstring::npos) AddIfGame(found, here.substr(0, slash));

  SearchUninstallKeys(found);
  for (const wchar_t* guess : kGuesses) AddIfGame(found, guess);
  return found;
}

std::wstring RememberedGameFolder() {
  return Trimmed(RegString(HKEY_CURRENT_USER, kRegKey, kRegValue));
}

void RememberGameFolder(const std::wstring& folder) {
  HKEY key = nullptr;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegKey, 0, nullptr, 0, KEY_WRITE, nullptr,
                      &key, nullptr) != ERROR_SUCCESS) {
    return;
  }
  RegSetValueExW(key, kRegValue, 0, REG_SZ,
                 reinterpret_cast<const BYTE*>(folder.c_str()),
                 static_cast<DWORD>((folder.size() + 1) * sizeof(wchar_t)));
  RegCloseKey(key);
}

std::wstring AskForGameFolder(HWND owner) {
  // The Vista-era picker rather than SHBrowseForFolder: it can be typed into,
  // it can be pasted into, and it looks like the rest of the system. The old
  // one is a tree with no address bar, and a user who knows exactly where their
  // game is cannot tell it so.
  IFileDialog* dialog = nullptr;
  if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                              IID_PPV_ARGS(&dialog)))) {
    return {};
  }
  std::wstring chosen;
  DWORD options = 0;
  if (SUCCEEDED(dialog->GetOptions(&options))) {
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM |
                       FOS_PATHMUSTEXIST);
  }
  const std::wstring title = Text(IDS_FIND_GAME);
  dialog->SetTitle(title.c_str());
  if (SUCCEEDED(dialog->Show(owner))) {
    IShellItem* item = nullptr;
    if (SUCCEEDED(dialog->GetResult(&item)) && item) {
      wchar_t* path = nullptr;
      if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
        chosen = path;
        CoTaskMemFree(path);
      }
      item->Release();
    }
  }
  dialog->Release();
  return Trimmed(chosen);
}

}  // namespace ulwin

#include "Display.hpp"

#include "Files.hpp"
#include "Strings.hpp"

#include <uniloader/uniloader.h>

#include <algorithm>
#include <vector>

namespace ulwin {
namespace {

/// Whether the DLL at `path` is cnc-ddraw.
///
/// Read out of the binary rather than inferred from a version number or a file
/// date. War2Combat's configuration tool swaps whole wrappers in and out and
/// leaves both `.ini` files behind, so the only thing that reliably says which
/// config is live is the DLL that will be doing the reading.
bool IsCncDdraw(const std::wstring& path) {
  HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  // A megabyte covers the whole of it; the DLL is about 260 KB.
  std::string bytes(1024 * 1024, '\0');
  DWORD read = 0;
  ReadFile(file, &bytes[0], static_cast<DWORD>(bytes.size()), &read, nullptr);
  CloseHandle(file);
  bytes.resize(read);
  return bytes.find("cnc-ddraw") != std::string::npos;
}

/// The scaling filters the game folder actually holds, with "" first for none.
///
/// Listed from the folder rather than from a table of known names, so a shader
/// added to War2Combat next year appears here without a code change. Shown by
/// file name, which is not pretty but is the author's and is never wrong.
std::vector<std::wstring> ShaderFiles(const std::wstring& game_folder) {
  std::vector<std::wstring> found;
  found.push_back(L"");
  WIN32_FIND_DATAW entry = {};
  HANDLE handle =
      FindFirstFileW((game_folder + L"\\Shaders\\*.glsl").c_str(), &entry);
  if (handle == INVALID_HANDLE_VALUE) return found;
  do {
    found.push_back(entry.cFileName);
  } while (FindNextFileW(handle, &entry));
  FindClose(handle);
  std::sort(found.begin() + 1, found.end());
  return found;
}

}  // namespace

std::wstring DisplayConfigPath(const std::wstring& game_folder) {
  if (game_folder.empty()) return {};
  const std::wstring dll = game_folder + L"\\ddraw.dll";
  const std::wstring ini = game_folder + L"\\ddraw.ini";
  if (!FileExists(dll) || !FileExists(ini)) return {};
  return IsCncDdraw(dll) ? ini : std::wstring();
}

DisplaySettings ReadDisplaySettings(const std::wstring& game_folder) {
  DisplaySettings display;
  display.shaders = ShaderFiles(game_folder);

  const std::wstring path = DisplayConfigPath(game_folder);
  if (path.empty()) return display;

  const std::string text = ToUtf8(ReadTextFile(path));
  ul_ini* ini = ul_ini_parse(text.data(), text.size());
  if (!ini) return display;

  display.available = true;
  display.mode = ul_display_mode(ini);
  display.keep_aspect = ul_display_keep_aspect(ini) != 0;

  // The file stores a path and the list holds bare names, because a name is
  // what a person reads. One that is not in the folder lands on "none" rather
  // than quietly selecting whatever happened to be first.
  const std::wstring current = FromUtf8(ul_display_shader(ini));
  const size_t slash = current.find_last_of(L"\\/");
  const std::wstring name =
      slash == std::wstring::npos ? current : current.substr(slash + 1);
  for (size_t i = 1; i < display.shaders.size(); ++i) {
    if (_wcsicmp(display.shaders[i].c_str(), name.c_str()) == 0) {
      display.shader = static_cast<int>(i);
      break;
    }
  }
  ul_ini_free(ini);
  return display;
}

void SaveDisplaySettings(const std::wstring& game_folder,
                         const std::wstring& store_folder,
                         const DisplaySettings& display) {
  const std::wstring path = DisplayConfigPath(game_folder);
  if (path.empty() || !display.available) return;

  const std::string text = ToUtf8(ReadTextFile(path));
  // A copy of the file as it was found, kept once. Not in the receipt: these
  // are the game's settings, they were here first, and an uninstall has no
  // business putting them back.
  const std::wstring first = store_folder + L"\\ddraw.ini.first";
  if (!FileExists(first)) WriteTextFile(first, text);

  ul_ini* ini = ul_ini_parse(text.data(), text.size());
  if (!ini) return;
  ul_display_set_mode(ini, display.mode);
  ul_display_set_keep_aspect(ini, display.keep_aspect ? 1 : 0);

  const bool has_shader =
      display.shader > 0 &&
      display.shader < static_cast<int>(display.shaders.size());
  const std::wstring chosen =
      has_shader ? L"Shaders\\" + display.shaders[static_cast<size_t>(display.shader)]
                 : L"";
  ul_display_set_shader(ini, ToUtf8(chosen).c_str());

  size_t length = 0;
  char* out = ul_ini_write(ini, &length);
  if (out) {
    WriteTextFile(path, std::string(out, length));
    ul_free(out);
  }
  ul_ini_free(ini);
}

}  // namespace ulwin

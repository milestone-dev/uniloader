// The two windows that are not the main one.
//
// Both are plain popup windows built by hand, the same way the main window is,
// rather than dialog templates in the .rc. The client has no other dialogs, the
// layout of these two is a handful of MoveWindow calls, and a template would
// put half of each window's description in a resource file and half in code.
//
// Neither of them decides anything. ShowSettings reports which button was
// pressed and main.cpp acts on it, so nothing here touches app state, starts a
// job, or knows what a receipt is.

#pragma once

#include <windows.h>

#include <string>
#include <vector>

struct ul_release;

namespace ulwin {

/// The whole mod's release notes, as dannyldd wrote them on the mod page —
/// every version, not only the ones since the installed one. Modal.
void ShowChangelog(HWND owner, const ul_release* release);

/// What the user pressed. One enum for both windows, because the caller answers
/// all of these the same way — by doing the thing — and two enums would only
/// make the switch in main.cpp longer.
enum class DialogAction {
  None,          // closed it
  ChangeFolder,  // wants to pick a different War2Combat
  Uninstall,     // wants the mod taken back out
  ClearCache,    // wants the kept download deleted
};

/// The game's own display options, as the settings window shows them. The host
/// reads and writes the INI; this only carries the answers to and fro.
struct DisplaySettings {
  bool available = false;      // false when no config file was found
  int mode = 0;                // UL_DISPLAY_*
  bool keep_aspect = true;
  /// Every shader in the game's Shaders folder, as file names, plus the empty
  /// string at the front for "no filter". The order is what the list shows.
  std::vector<std::wstring> shaders;
  int shader = 0;              // an index into `shaders`
  bool changed = false;        // set by the window when the user changed one
};

/// The things that are not part of playing: where the game is, where UniLoader
/// keeps its files, and how to remove it. Modal; returns once it is closed.
DialogAction ShowSettings(HWND owner, const std::wstring& game_folder,
                          const std::wstring& store_folder,
                          const std::wstring& installed_version,
                          const std::wstring& cache_size, bool busy,
                          DisplaySettings& display);

}  // namespace ulwin

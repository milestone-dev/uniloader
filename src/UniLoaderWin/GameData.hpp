// Finding War2Combat, and remembering where it was.
//
// The same search PUDForge does, for the same reason: the common case should
// never see a dialog. Dropping UniLoader.exe into the game folder is enough,
// and so is having installed War2Combat the ordinary way.

#pragma once

#include <windows.h>

#include <string>
#include <vector>

namespace ulwin {

/// Every installation found, best guess first. Empty when there is none.
std::vector<std::wstring> FindGameFolders();

/// Whether this folder really is one — it holds the game's executable.
bool IsGameFolder(const std::wstring& folder);

/// The remembered choice, or "" if there is none.
std::wstring RememberedGameFolder();
void RememberGameFolder(const std::wstring& folder);

/// Whether the Warcraft theme is wanted. On unless it was deliberately turned
/// off, so a first run is themed and an absent or unreadable value cannot lose
/// the look. Kept beside the game folder, under HKCU\Software\UniLoader.
bool ThemeWanted();
void RememberThemeWanted(bool wanted);

/// A folder-picking dialog. Empty when the user cancelled.
std::wstring AskForGameFolder(HWND owner);

}  // namespace ulwin

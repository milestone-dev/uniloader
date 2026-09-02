// War2Combat's own display settings.
//
// Not the mod's and not UniLoader's: they belong to the game, they were there
// before any of this, and they outlive an install and an uninstall both. All
// this does is find the file that is actually live, read three values out of
// it, and put three values back.

#pragma once

#include "Dialogs.hpp"

#include <string>

namespace ulwin {

/// The display config the installed ddraw.dll actually reads, or "" when there
/// is not one.
///
/// War2Combat ships two, and only one is live at a time — the wrappers are
/// swapped by its own configuration tool and both `.ini` files are left lying
/// around either way. They use different key names, so writing the wrong one
/// changes nothing at all while looking exactly like success.
std::wstring DisplayConfigPath(const std::wstring& game_folder);

/// Reads the live config, and lists the scaling filters the game folder holds.
/// `available` comes back false when there is no config to read, which is a
/// normal state and not an error.
DisplaySettings ReadDisplaySettings(const std::wstring& game_folder);

/// Writes those three values back and nothing else — every comment, blank line
/// and other section survives. The first time UniLoader touches the file, a
/// copy of it as found goes into `store_folder`.
void SaveDisplaySettings(const std::wstring& game_folder,
                         const std::wstring& store_folder,
                         const DisplaySettings& display);

}  // namespace ulwin

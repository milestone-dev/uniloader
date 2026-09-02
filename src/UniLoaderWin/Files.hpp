// The filesystem half of the split: the core builds a plan, this runs it.
//
// Nothing here decides anything. If a question has an answer that could be
// wrong — which file to back up, what to delete on uninstall, which plugin's
// files to copy — the answer comes from the core and arrives here as a plan.

#pragma once

#include <windows.h>

#include <functional>
#include <string>
#include <vector>

struct ul_plan;
struct ul_receipt;

namespace ulwin {

/// Every file below `root`, as paths relative to it, with forward slashes —
/// which is what the core's path rules are written against. Directories are not
/// returned; a package is its files.
std::vector<std::string> WalkFiles(const std::wstring& root);

bool FileExists(const std::wstring& path);
bool FolderExists(const std::wstring& path);
bool EnsureFolder(const std::wstring& path);
/// A whole tree, gone. Used on the unpack scratch folder and on a store folder
/// for a version that has been replaced.
bool RemoveTree(const std::wstring& path);

/// Whether a new file can be created in `folder`. Asked by *trying*, because
/// the answer on Windows depends on the folder's ACL, on virtualisation, on
/// whether the process is elevated and on whether the volume is read-only — and
/// no combination of those is reliably readable in advance.
bool IsWritable(const std::wstring& folder);

/// Where the program keeps its own things: %LOCALAPPDATA%\UnificationMod. The
/// unpacked package, the backups and the receipt live here, not in the game
/// folder — so that reinstalling the game does not silently discard the record
/// of what was done to it. A store under the old name, UniLoader, is renamed
/// into place on first sight.
std::wstring StoreFolder();

/// Whether Warcraft II is running. Its files cannot be replaced while it has
/// them open, and the error Windows gives for that names a sharing violation
/// rather than the game, which sends people looking in the wrong place.
bool GameIsRunning();

/// The first file a plan would write or delete that something else holds open,
/// or "" when the plan can run.
///
/// Asked before the plan starts rather than discovered in the middle of it. A
/// plugin switch that fails halfway has already deleted the old .w2p, so the
/// game is left with none — and the game is exactly what holds them open.
/// GameIsRunning() catches the usual case by process name; this catches it by
/// the thing that actually matters, and covers a launcher nobody thought of.
std::wstring PlanBlockedBy(const ul_plan* plan);

/// Runs a plan. `progress` is called with the step index; returning false
/// cancels, and what has been done so far is reported through `done`, which the
/// caller writes into the receipt so that even a cancelled install can be
/// undone. Returns a UL_* code.
int RunPlan(const ul_plan* plan, ul_receipt* receipt,
            const std::function<bool(int step, int steps)>& progress,
            std::wstring& error);

/// Removes any folder under `root` that is now empty, deepest first.
///
/// An uninstall moves files out and leaves the directories they were in
/// standing. Empty or not, they are the mod's, and a game folder left with an
/// empty UniFiles\ in it has not been put back the way it was found. `root`
/// itself is never removed.
void RemoveEmptyFolders(const std::wstring& root);

/// Relaunches this executable elevated and asks it to carry on. Returns false
/// if the user declined the prompt, which is a decision and not an error.
bool RelaunchElevated(const std::wstring& arguments);

/// Whether this process is already elevated.
bool IsElevated();

/// Starts a program in a folder, without waiting. Used for Play.
bool Launch(const std::wstring& executable, const std::wstring& working_folder);

std::wstring ReadTextFile(const std::wstring& path);
bool WriteTextFile(const std::wstring& path, const std::string& contents);

/// The folder this executable is in.
std::wstring ModuleFolder();

}  // namespace ulwin

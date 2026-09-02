// UniLoader — a manager for dannyldd's War2 Unification mod.
//
// One window and five things it does: check whether there is a new release,
// download and install it, choose which plugin is loaded, start the game, and
// take it all back out again.
//
// The shape is PUDForge's. Everything that needs judgement lives in the core
// behind a C ABI and is tested without a window; this file is the part that
// cannot be — the layout, the threads, the message loop, and the decision about
// when to ask for administrator rights.
//
// Long work runs on one worker thread at a time and reports back by posting
// WM_UL_PROGRESS and WM_UL_FINISHED. Nothing is read across the threads except
// through `status_lock`, and nothing that a control owns is touched off the UI
// thread at all.

#include <windows.h>

#include <commctrl.h>
#include <objbase.h>
#include <objidl.h>
#include <richedit.h>   // the description: a RichEdit so its links are clickable

#include <gdiplus.h>

#include <uniloader/uniloader.h>

#include "Dialogs.hpp"
#include "Display.hpp"
#include "Files.hpp"
#include "GameData.hpp"
#include "Net.hpp"
#include "Slideshow.hpp"
#include "Theme.hpp"
#include "Video.hpp"
#include "Strings.hpp"
#include "resource.h"
#include "strings.h"
#include "version.h"

#include <atomic>
#include <cstdio>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace ulwin {
namespace {

/// dannyldd's Unification mod on GameBanana. The one number this program is
/// pointed at; everything else about the release is read from the page.
constexpr int64_t kModId = 644456;
constexpr wchar_t kProfileUrl[] =
    L"https://gamebanana.com/apiv11/Mod/644456/ProfilePage";
constexpr wchar_t kUpdatesUrl[] =
    L"https://gamebanana.com/apiv11/Mod/644456/Updates?_nPage=1&_nPerpage=25";

enum class Job { None, Check, Install, SetPlugin, Uninstall };

struct App {
  HWND window = nullptr;
  HWND status = nullptr;
  HWND progress = nullptr;
  int progress_permille = 0;   // what the owner-drawn bar shows
  HWND action = nullptr;
  HWND play = nullptr;
  HWND plugins = nullptr;
  HWND variants = nullptr;
  HWND description = nullptr;
  HWND shot = nullptr;
  HWND shot_prev = nullptr;
  HWND shot_next = nullptr;
  bool has_shots = false;      // whether the gallery has anything in it at all
  HWND latest = nullptr;        // the version on offer, beside the button
  HWND changelog = nullptr;
  HWND settings = nullptr;
  HWND modpage = nullptr;
  HFONT font = nullptr;
  HFONT bold_font = nullptr;   // the activated row in the plugin list

  std::wstring game_folder;
  std::wstring store;

  ul_release* release = nullptr;
  ul_catalogue* catalogue = nullptr;

  std::string installed_version;   // "" when nothing is installed
  std::string latest_version;      // what the mod page says
  /// What the downloaded file's own name says it is, when it says anything.
  /// Recorded in preference to `latest_version`, because the receipt describes
  /// the bytes on disk and not the page they were found through.
  std::string downloaded_version;
  std::string active_plugin;       // "" is the "No plugin" choice
  int active_variant = 0;
  std::wstring package_folder;

  // --- the worker -----------------------------------------------------------
  std::thread worker;
  std::atomic<bool> busy{false};
  std::atomic<bool> cancel{false};
  Job job = Job::None;
  std::mutex status_lock;
  std::wstring status_text;
  std::wstring error_text;

  /// One shot, at startup and after an install: fetches the YouTube thumbnails
  /// the catalogue's videos need. Its own thread rather than the job worker,
  /// because it is not a job — nothing waits for it, nothing is cancelled by
  /// it, and a gallery that fills in a second late is fine.
  std::thread thumbs;
  std::atomic<bool> thumbs_running{false};

  /// Selecting a plugin loads it. While one load is running the newest
  /// selection is remembered here and applied when that finishes, so arrowing
  /// down the list ends on the row the user stopped at rather than on whichever
  /// row happened to win a race.
  bool catch_up = false;

  /// Set when Play had to load a plugin first. The launch waits for the copy to
  /// finish, because starting the game while its files are being replaced is
  /// the one thing this program must never do.
  bool play_when_ready = false;

  /// What the user asked for while the elevated copy was starting. Carried
  /// across the relaunch so that saying yes to the UAC prompt finishes the job
  /// rather than dropping the user back on the same button.
  Job resume = Job::None;
};

App g;

// ---------------------------------------------------------------- plumbing

void SetStatus(const std::wstring& text) {
  {
    std::lock_guard<std::mutex> held(g.status_lock);
    g.status_text = text;
  }
  // Posted, not sent: this is called from the worker, and a send would block it
  // behind whatever the UI thread is doing — including a modal dialog.
  PostMessageW(g.window, WM_UL_PROGRESS, static_cast<WPARAM>(-1), 0);
}

void SetProgress(int permille) {
  PostMessageW(g.window, WM_UL_PROGRESS, static_cast<WPARAM>(permille), 0);
}

void Finish(int code, Job job) {
  PostMessageW(g.window, WM_UL_FINISHED, static_cast<WPARAM>(code),
               static_cast<LPARAM>(job));
}

std::wstring Utf8(const char* text) { return FromUtf8(text ? text : ""); }

/// CR alone, which is RichEdit's own paragraph mark. The description arrives
/// from the core with the LF breaks its author typed; handed to the Msftedit
/// control as CRLF through WM_SETTEXT, the breaks are quietly dropped and the
/// whole file runs together as one paragraph.
std::wstring ForRichEdit(const std::wstring& text) {
  std::wstring out;
  out.reserve(text.size());
  for (const wchar_t c : text) {
    out.push_back(c == L'\n' ? L'\r' : c);
  }
  return out;
}

/// Marks the URLs in the description as clickable links, in the theme's red.
///
/// EM_AUTOURLDETECT can find them, but it draws them the system blue and runs
/// its scan on its own schedule — asking the control which runs were links
/// straight after setting the text found none, because it had not looked yet.
/// Marking them here means the range that is clickable and the range that is
/// red are the same range by construction. `text` must be the string the
/// control was given: RichEdit counts the CR-only breaks the way this does.
void MarkLinks(HWND control, const std::wstring& text) {
  // Without the theme the control's own detector is on and colours them the
  // system blue, which is the look being asked for.
  if (!ThemeEnabled()) return;

  CHARFORMAT2W link = {};
  link.cbSize = sizeof(link);
  link.dwMask = CFM_LINK | CFM_COLOR | CFM_UNDERLINE;
  link.dwEffects = CFE_LINK | CFE_UNDERLINE;
  link.crTextColor = ThemeLink();   // and no CFE_AUTOCOLOR, which would win

  // Found first, applied second: most descriptions carry no link at all, and
  // they should not pay the repaint the marking below costs.
  std::vector<CHARRANGE> links;
  size_t at = 0;
  while (at < text.size()) {
    size_t start = std::wstring::npos;
    for (const wchar_t* prefix : {L"https://", L"http://", L"www."}) {
      const size_t found = text.find(prefix, at);
      if (found < start) start = found;
    }
    if (start == std::wstring::npos) break;
    size_t end = start;
    while (end < text.size() && !iswspace(text[end])) ++end;
    // A URL that ends a sentence does not own the full stop.
    while (end > start && wcschr(L".,;:!?)]}>\"'", text[end - 1])) --end;
    if (end > start) {
      links.push_back({static_cast<LONG>(start), static_cast<LONG>(end)});
    }
    at = end > start ? end : start + 1;
  }
  if (links.empty()) return;

  SendMessageW(control, WM_SETREDRAW, FALSE, 0);
  for (const CHARRANGE& range : links) {
    SendMessageW(control, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&range));
    SendMessageW(control, EM_SETCHARFORMAT, SCF_SELECTION,
                 reinterpret_cast<LPARAM>(&link));
  }
  // Back to the top: setting the ranges dragged the selection, and the view
  // with it. Invalidated without an erase, which would flash the whole box.
  const CHARRANGE none = {0, 0};
  SendMessageW(control, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&none));
  SendMessageW(control, WM_SETREDRAW, TRUE, 0);
  InvalidateRect(control, nullptr, FALSE);
}

/// Declared early because showing a plugin can change whether the variant
/// dropdown exists, and that changes where everything under it sits.
void Layout();
void ShowPluginDetails();
void UpdatePlayButton();
/// Whether the selection differs from what the game has loaded. Declared here
/// because selecting a plugin loads it, and that happens well above the code
/// that knows what "loaded" means.
bool SelectionNeedsLoading(std::string* id, int* variant);
/// Starts one background job. Declared here because ticking a checkbox in the
/// list starts one, and the list is drawn well before the jobs are defined.
void Start(Job job, std::string plugin_id = {}, int variant = 0);
/// Declared here because the install worker is written above it and every
/// worker asks the same question before its first step.
bool PlanBlocked(const ul_plan* plan, Job job);

// -------------------------------------------------------------- the list
//
// Owner-drawn, for two things a plain listbox cannot say.
//
// The activated plugin — the one the game is actually loading — is marked with
// an accent bar down its edge and its name in bold. Not a checkbox: a column of
// tick boxes turns a list of things to read about into a form to fill in, and
// only one of them can ever be on anyway, which is not what a checkbox means.
//
constexpr int kAccentWidth = 4;    // the bar on the activated row
constexpr int kRowIndent = 12;     // where every name starts

// The bottom-right block: Play, a gap, then the difficulty. Named because the
// panel above has to stop short of all three, and the slot for the dropdown is
// reserved whether or not there is one to show.
constexpr int kPlayHeight = 46;   // the primary action, a size up from the rest
constexpr int kPlayWidth = 260;    // fixed: the label carries a name of any length
constexpr int kVariantHeight = 26;
constexpr int kActionGap = 10;
constexpr int kActionHeight = kPlayHeight + kActionGap + kVariantHeight;

/// The activated row: 0 is "No plugin", and a plugin is one past its catalogue
/// index.
int ActiveRow() {
  if (g.active_plugin.empty() || !g.catalogue) return 0;
  const int index = ul_catalogue_find(g.catalogue, g.active_plugin.c_str());
  return index < 0 ? 0 : index + 1;
}

void DrawPluginRow(const DRAWITEMSTRUCT* item) {
  if (!item || item->itemID == static_cast<UINT>(-1)) return;
  HDC dc = item->hDC;
  const int row = static_cast<int>(item->itemID);
  const bool selected = (item->itemState & ODS_SELECTED) != 0;
  const bool active = row == ActiveRow();

  // The game's own pairing: parchment rows, and the chosen one in the deep
  // maroon its buttons wear, lettered in gold.
  HBRUSH brush =
      selected ? CreateSolidBrush(ThemeMaroon()) : CreateSolidBrush(ThemePanel());
  FillRect(dc, &item->rcItem, brush);
  DeleteObject(brush);

  if (active) {
    RECT accent = item->rcItem;
    accent.right = accent.left + kAccentWidth;
    // Gold means "this one is loaded" — and it reads over both row colours.
    HBRUSH bar = CreateSolidBrush(ThemeGold());
    FillRect(dc, &accent, bar);
    DeleteObject(bar);
  }

  wchar_t text[256] = {};
  SendMessageW(item->hwndItem, LB_GETTEXT, item->itemID,
               reinterpret_cast<LPARAM>(text));

  SetBkMode(dc, TRANSPARENT);
  HGDIOBJ previous = SelectObject(dc, active ? g.bold_font : g.font);

  RECT label = item->rcItem;
  label.left += kRowIndent;
  label.right -= 8;
  SetTextColor(dc, selected ? ThemeGold() : ThemeInk());
  DrawTextW(dc, text, -1, &label,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
  SelectObject(dc, previous);
}

void Say(const std::wstring& message, UINT flags = MB_OK | MB_ICONINFORMATION) {
  MessageBoxW(g.window, message.c_str(), Text(IDS_APP_TITLE).c_str(), flags);
}

/// A path with no trailing separator. One left on turns every path built from
/// it into a double backslash, which Windows accepts and a person reads as a bug.
std::wstring TrimTrailing(std::wstring path) {
  while (!path.empty() && (path.back() == L'\\' || path.back() == L'/')) path.pop_back();
  return path;
}

std::wstring PackageFolderFor(const std::string& version) {
  // Named by the bare number. The mod page says "v6.6" and the package file
  // says "6_6", and the cache has to be found under one name whichever of
  // them the version came from — a lookup under "v6.6" of a folder written as
  // "6.6" is a miss that costs an 800 MB re-download and looks exactly like a
  // working program.
  std::string bare = version;
  if (bare.size() > 1 && (bare[0] == 'v' || bare[0] == 'V') && bare[1] >= '0' &&
      bare[1] <= '9') {
    bare.erase(0, 1);
  }
  return g.store + L"\\package\\" + FromUtf8(bare.empty() ? "current" : bare);
}

/// Where a video's thumbnail is cached once it has been fetched. The id names
/// the file because the id is what names it at img.youtube.com too.
std::wstring ThumbnailPath(const std::string& video_id) {
  return g.store + L"\\thumbs\\" + FromUtf8(video_id) + L".jpg";
}

/// The stamp beside a cached package, naming what it is and how complete it is.
std::wstring StampPath(const std::wstring& unpacked) {
  return unpacked + L"\\uniloader-package.json";
}

/// The root of an already-unpacked copy of `version`, or "" if there is not a
/// trustworthy one.
///
/// A release is 800 MB over somebody's home line and the same 800 MB every
/// time. Keeping the unpacked copy is what makes a reinstall — or an uninstall
/// followed by a change of mind — take a second instead of an evening.
///
/// Trustworthy is the whole question. A folder full of files is not evidence of
/// a complete extraction: a cancelled one leaves a folder that looks exactly the
/// same, and installing from it would install half a mod. The stamp records the
/// version and the file count, and both have to still agree.
std::wstring CachedPackage(const std::string& version) {
  const std::wstring unpacked = PackageFolderFor(version);
  if (!FolderExists(unpacked)) return {};
  const std::string stamp = ToUtf8(ReadTextFile(StampPath(unpacked)));
  if (stamp.empty()) return {};
  const int count = static_cast<int>(WalkFiles(unpacked).size());
  // The stamp counts itself out: it is written after the walk that produced
  // the number, so the folder now holds one more file than it did then.
  if (!ul_package_stamp_ok(stamp.data(), stamp.size(), version.c_str(), count - 1)) {
    return {};
  }
  if (!ul_package_stamp_complete(stamp.data(), stamp.size())) return {};
  char* root = ul_package_stamp_root(stamp.data(), stamp.size());
  const std::wstring inside = Utf8(root);
  ul_free(root);
  const std::wstring package = inside.empty() ? unpacked : unpacked + L"\\" + inside;
  return TrimTrailing(package);
}

/// Writes the stamp for the copy in the store, counting what is there now.
///
/// `complete` says whether an install could be run from it. After an install
/// it cannot: the bulk of the package has moved into the game folder and only
/// Plugins/ is left, which is enough to switch plugins and not enough to
/// install from.
void WriteStamp(const std::wstring& unpacked, const std::string& version,
                bool complete) {
  // Counted before the stamp is written, and read back the same way, so the
  // number means the same thing on both sides.
  const std::vector<std::string> paths = WalkFiles(unpacked);
  int count = static_cast<int>(paths.size());
  if (FileExists(StampPath(unpacked))) --count;

  // The root is worked out from the files that are actually there, never
  // carried over from a previous stamp. Trusting a stale one is how a package
  // wrapped in a folder came to be installed *as* that folder, one level deep
  // inside the game.
  std::string joined;
  for (const std::string& path : paths) {
    if (path == "uniloader-package.json") continue;
    joined.append(path);
    joined.push_back(0);
  }
  joined.push_back(0);
  char* found = ul_package_root(joined.c_str());
  const std::string root = found ? found : "";
  ul_free(found);

  size_t length = 0;
  char* stamp = ul_package_stamp(version.c_str(), root.c_str(), count,
                                 complete ? 1 : 0, &length);
  if (stamp) {
    WriteTextFile(StampPath(unpacked), std::string(stamp, length));
    ul_free(stamp);
  }
}

/// Everything in the store copy that the game folder now holds as well.
///
/// The package is 1.2 GB and the install just wrote all of it into War2Combat.
/// Keeping a second copy in the store would double that for nothing, so what
/// stays is exactly what the install plan left behind — Plugins/, base/, the
/// press-kit — asked of the core, because a second list here once drifted and
/// pruned a folder the install had never copied, destroying it.
void PruneInstalledFiles(const std::wstring& unpacked, const std::string& root) {
  const std::wstring package =
      root.empty() ? unpacked : TrimTrailing(unpacked + L"\\" + FromUtf8(root));
  WIN32_FIND_DATAW found = {};
  HANDLE handle = FindFirstFileW((package + L"\\*").c_str(), &found);
  if (handle == INVALID_HANDLE_VALUE) return;
  std::vector<std::wstring> files;
  std::vector<std::wstring> folders;
  do {
    const std::wstring name = found.cFileName;
    if (name == L"." || name == L"..") continue;
    if (ul_path_stays_in_store(ToUtf8(name).c_str())) continue;
    const std::wstring path = package + L"\\" + name;
    if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
      folders.push_back(path);
    } else {
      files.push_back(path);
    }
  } while (FindNextFileW(handle, &found));
  FindClose(handle);
  for (const std::wstring& path : folders) RemoveTree(path);
  for (const std::wstring& path : files) DeleteFileW(path.c_str());
}

/// Throws away every cached package except `keep`. One release is 800 MB and
/// they come every few weeks; without this the store grows by that much per
/// update, quietly, in a folder nobody looks in.
void PruneCache(const std::string& keep) {
  const std::wstring root = g.store + L"\\package";
  if (!FolderExists(root)) return;
  const std::wstring wanted = PackageFolderFor(keep);
  WIN32_FIND_DATAW found = {};
  HANDLE handle = FindFirstFileW((root + L"\\*").c_str(), &found);
  if (handle == INVALID_HANDLE_VALUE) return;
  std::vector<std::wstring> stale;
  do {
    const std::wstring name = found.cFileName;
    if (name == L".." || name == L"." ) continue;
    if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) continue;
    const std::wstring path = root + L"\\" + name;
    if (_wcsicmp(path.c_str(), wanted.c_str()) != 0) stale.push_back(path);
  } while (FindNextFileW(handle, &found));
  FindClose(handle);
  for (const std::wstring& path : stale) RemoveTree(path);
}

/// Downloaded packages that are *not* the installed one, and what they weigh.
///
/// The installed version's folder is never counted, because after an install it
/// is not a spare copy of anything: `PruneInstalledFiles` has already deleted
/// everything the game folder holds, and what is left is `Plugins/` — the
/// nineteen folders the list is built from and the `.w2p` files are copied out
/// of. Offering to delete those as "downloaded files" would empty the plugin
/// list, leave the mod installed and unswitchable, and cost an 800 MB download
/// to undo. So it is a spare only when nothing is installed, or when it is a
/// release that has been superseded.
int64_t SpareCacheBytes() {
  const std::wstring root = g.store + L"\\package";
  const std::wstring installed =
      g.installed_version.empty() ? std::wstring() : PackageFolderFor(g.installed_version);
  int64_t total = 0;
  for (const std::string& relative : WalkFiles(root)) {
    const std::wstring path = root + L"\\" + FromUtf8(relative);
    if (!installed.empty() &&
        _wcsnicmp(path.c_str(), installed.c_str(), installed.size()) == 0) {
      continue;
    }
    WIN32_FILE_ATTRIBUTE_DATA info = {};
    if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &info)) {
      total += (static_cast<int64_t>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
    }
  }
  return total;
}
std::wstring ReceiptPath() { return g.store + L"\\install.json"; }
std::wstring BackupFolder() { return g.store + L"\\backup"; }

// ------------------------------------------------------------ the catalogue

/// Builds the catalogue from whatever is in the package folder. Also the
/// function that decides a package is present at all: no folder, no catalogue,
/// and the client shows nothing installed.
void ReadCatalogue() {
  if (g.catalogue) {
    ul_catalogue_free(g.catalogue);
    g.catalogue = nullptr;
  }
  if (g.package_folder.empty() || !FolderExists(g.package_folder)) return;

  const std::vector<std::string> paths = WalkFiles(g.package_folder);
  g.catalogue = ul_catalogue_create();
  for (const std::string& path : paths) {
    ul_catalogue_add_path(g.catalogue, path.c_str());
  }
  // The info.txt files are read by the host and handed in as text: the core
  // decides what the prose *means*, the host decides what a file *is*. Only a
  // folder's own top-level info.txt counts — one nested deeper is the author's
  // note about something inside it, not the folder describing itself.
  for (const std::string& path : paths) {
    std::string id;
    if (_strnicmp(path.c_str(), "Plugins/", 8) == 0) {
      const size_t second = path.find('/', 8);
      if (second == std::string::npos) continue;
      if (_stricmp(path.c_str() + second + 1, "info.txt") != 0) continue;
      id = path.substr(8, second - 8);
    } else if (_stricmp(path.c_str(), "base/info.txt") != 0) {
      continue;   // the empty id is base/'s: the mod describing itself
    }
    const std::wstring text = ReadTextFile(g.package_folder + L"\\" + FromUtf8(path));
    const std::string utf8 = ToUtf8(text);
    ul_catalogue_add_info(g.catalogue, id.c_str(), utf8.data(), utf8.size());
  }
  ul_catalogue_finish(g.catalogue);
}

/// Downloads any video thumbnail the catalogue needs and does not have.
///
/// The ids are copied out of the catalogue on the caller's thread and the
/// thread that runs afterwards touches nothing but the network and the store —
/// the catalogue can be freed and rebuilt by an install while this is still
/// going, and this must not be looking at it when that happens.
void FetchThumbnails() {
  if (!g.catalogue || g.thumbs_running) return;
  // A video's thumbnail file is named by its video id, a playlist's by its
  // list id — which is also what the gallery looks the file up by.
  struct WantedThumb {
    std::string id;
    bool playlist;
  };
  std::vector<WantedThumb> wanted;
  const auto want = [&wanted](const std::string& id, const std::string& list) {
    if (!id.empty() && !FileExists(ThumbnailPath(id))) wanted.push_back({id, false});
    if (id.empty() && !list.empty() && !FileExists(ThumbnailPath(list))) {
      wanted.push_back({list, true});
    }
  };
  for (int i = 0; i < ul_catalogue_count(g.catalogue); ++i) {
    for (int v = 0; v < ul_plugin_video_count(g.catalogue, i); ++v) {
      want(ul_plugin_video_id(g.catalogue, i, v),
           ul_plugin_video_list_id(g.catalogue, i, v));
    }
  }
  // And the mod's own, from base/, shown for "No plugin".
  for (int v = 0; v < ul_base_video_count(g.catalogue); ++v) {
    want(ul_base_video_id(g.catalogue, v), ul_base_video_list_id(g.catalogue, v));
  }
  if (wanted.empty()) return;

  if (g.thumbs.joinable()) g.thumbs.join();
  g.thumbs_running = true;
  const std::wstring folder = g.store + L"\\thumbs";
  g.thumbs = std::thread([wanted, folder]() {
    EnsureFolder(folder);
    for (const WantedThumb& item : wanted) {
      const std::wstring path = folder + L"\\" + FromUtf8(item.id) + L".jpg";
      bool ok = false;
      if (!item.playlist) {
        // hqdefault, which every video has and which needs no key and no API.
        ok = FetchToFile(L"https://img.youtube.com/vi/" + FromUtf8(item.id) +
                             L"/hqdefault.jpg",
                         path, nullptr)
                 .ok;
      } else {
        // A playlist has no thumbnail endpoint of its own. Its public oEmbed
        // record names its cover — the first video's — and the core reads the
        // answer; see ul_oembed_thumbnail_url.
        const std::wstring meta = path + L".oembed";
        const std::wstring ask =
            L"https://www.youtube.com/oembed?format=json&url="
            L"https%3A%2F%2Fwww.youtube.com%2Fplaylist%3Flist%3D" +
            FromUtf8(item.id);
        if (FetchToFile(ask, meta, nullptr).ok) {
          const std::string answer = ToUtf8(ReadTextFile(meta));
          char* cover = ul_oembed_thumbnail_url(answer.data(), answer.size());
          if (cover) {
            ok = FetchToFile(Utf8(cover), path, nullptr).ok;
            ul_free(cover);
          }
        }
        DeleteFileW(meta.c_str());
      }
      // A thumbnail that will not come is not worth reporting: the gallery
      // still shows the item, with the play badge over the placeholder.
      if (ok) PostMessageW(g.window, WM_UL_THUMBS, 0, 0);
    }
    g.thumbs_running = false;
  });
}

void LoadReceipt() {
  const std::wstring text = ReadTextFile(ReceiptPath());
  if (text.empty()) return;
  const std::string utf8 = ToUtf8(text);
  ul_receipt* receipt = ul_receipt_parse(utf8.data(), utf8.size());
  if (!receipt) return;
  g.installed_version = ul_receipt_mod_version(receipt);
  g.active_plugin = ul_receipt_plugin(receipt);
  g.active_variant = ul_receipt_variant(receipt);
  g.package_folder = FromUtf8(ul_receipt_package_dir(receipt));
  const std::wstring recorded = FromUtf8(ul_receipt_game_dir(receipt));
  if (!recorded.empty() && g.game_folder.empty()) g.game_folder = recorded;
  ul_receipt_free(receipt);
}

/// Rewrites the receipt's plugin choice without touching its file list. Called
/// after a plugin switch, which changes what is loaded but not what is
/// installed — the file list is the install's, and an uninstall still has to
/// undo all of it.
void SaveActivePlugin() {
  const std::wstring text = ReadTextFile(ReceiptPath());
  if (text.empty()) return;
  const std::string utf8 = ToUtf8(text);
  ul_receipt* receipt = ul_receipt_parse(utf8.data(), utf8.size());
  if (!receipt) return;
  ul_receipt_set_plugin(receipt, g.active_plugin.c_str());
  ul_receipt_set_variant(receipt, g.active_variant);
  size_t length = 0;
  char* json = ul_receipt_to_json(receipt, &length);
  if (json) {
    WriteTextFile(ReceiptPath(), std::string(json, length));
    ul_free(json);
  }
  ul_receipt_free(receipt);
}

// ---------------------------------------------------------------- the view

int SelectedPlugin() {
  // Row 0 is "No plugin", so the catalogue index is one less. Keeping "none" in
  // the list rather than beside it is deliberate: it is a choice like any other
  // and belongs where the eye already is.
  const LRESULT row = SendMessageW(g.plugins, LB_GETCURSEL, 0, 0);
  return row == LB_ERR ? -1 : static_cast<int>(row) - 1;
}


/// Where the gallery panel sits in the window's own coordinates.
///
/// The player is a child of the window rather than of the panel — WebView2
/// wants a plain HWND to fill and the panel is owner-drawn — so it is
/// positioned against the window, over exactly the rectangle the picture uses.
RECT ShotRect() {
  RECT box = {};
  if (!g.shot || !g.window) return box;
  GetWindowRect(g.shot, &box);
  MapWindowPoints(nullptr, g.window, reinterpret_cast<POINT*>(&box), 2);
  return box;
}

/// Starts the video and takes the panel out from under it.
///
/// They occupy the same rectangle and only one of them can be the thing on
/// screen. Hiding the panel rather than trusting z-order: the panel is moved on
/// every layout pass and a MoveWindow is entitled to raise it, which would put
/// a still picture over a playing video.
void ShowVideo(const std::string& video_id, const std::string& list_id) {
  ShowWindow(g.shot, SW_HIDE);
  // The arrows stay: they sit under the player's rectangle, not beneath it,
  // and stepping off the video is exactly what they are for — the step
  // handlers stop the playback themselves.
  PlayVideo(video_id, list_id, ShotRect());
}

/// Stops it and puts the gallery back. Safe to call when nothing is playing.
void HideVideo() {
  if (!VideoPlaying()) return;
  StopVideo();
  ShowWindow(g.shot, g.has_shots ? SW_SHOW : SW_HIDE);
  ShowWindow(g.shot_prev, g.has_shots ? SW_SHOW : SW_HIDE);
  ShowWindow(g.shot_next, g.has_shots ? SW_SHOW : SW_HIDE);
}

/// Warms the hidden player with whatever the gallery is showing, so a click on
/// its play badge starts at once instead of after a full embed boot.
void PrimeCurrentVideo() {
  if (VideoPlaying() || VideoPlayerFailed()) return;
  PrimeVideo(SlideshowVideoId(g.shot), SlideshowVideoListId(g.shot));
}

void ShowPluginDetails() {
  const int index = SelectedPlugin();
  std::vector<GalleryItem> shots;

  if (index < 0 || !g.catalogue) {
    // "No plugin" is a row in the list and describing it belongs here:
    // the mod's own words and pictures when the package carries a base/
    // folder, a stock sentence until one ships. With nothing installed there
    // is no list and no such row, so there is nothing to describe either.
    const bool installed = !g.installed_version.empty();
    std::wstring about;
    if (installed) {
      if (g.catalogue) about = Utf8(ul_base_description(g.catalogue));
      if (about.empty()) about = Text(IDS_NO_PLUGIN_ABOUT);
    }
    const std::wstring shown = ForRichEdit(about);
    SetWindowTextW(g.description, shown.c_str());
    MarkLinks(g.description, shown);
    SendMessageW(g.variants, CB_RESETCONTENT, 0, 0);
    // Hidden, not merely disabled. "No plugin" has no variants to choose
    // between, and an empty greyed dropdown sitting under the list reads as a
    // control that failed to fill rather than one that does not apply.
    EnableWindow(g.variants, FALSE);
    ShowWindow(g.variants, SW_HIDE);
    if (g.catalogue) {
      for (int i = 0; i < ul_base_video_count(g.catalogue); ++i) {
        GalleryItem item;
        item.video_url = Utf8(ul_base_video_url(g.catalogue, i));
        item.video_id = ul_base_video_id(g.catalogue, i);
        item.list_id = ul_base_video_list_id(g.catalogue, i);
        const std::string named =
            !item.video_id.empty() ? item.video_id : item.list_id;
        const std::wstring thumb = ThumbnailPath(named);
        if (FileExists(thumb)) item.image = thumb;
        shots.push_back(item);
      }
      for (int i = 0; i < ul_base_image_count(g.catalogue); ++i) {
        GalleryItem item;
        item.image = g.package_folder + L"\\" + Utf8(ul_base_image(g.catalogue, i));
        shots.push_back(item);
      }
    }
  } else {
    // Whatever the plugin says about itself, and nothing when it says nothing.
    // An empty box is the plainest way to show there is no description; a
    // sentence explaining the absence is longer than the absence and reads as
    // though something went wrong.
    const std::wstring about = Utf8(ul_plugin_description(g.catalogue, index));
    const std::wstring shown = ForRichEdit(about);
    SetWindowTextW(g.description, shown.c_str());
    MarkLinks(g.description, shown);

    SendMessageW(g.variants, CB_RESETCONTENT, 0, 0);
    const int variants = ul_plugin_variant_count(g.catalogue, index);
    for (int i = 0; i < variants; ++i) {
      SendMessageW(g.variants, CB_ADDSTRING, 0,
                   reinterpret_cast<LPARAM>(
                       Utf8(ul_plugin_variant_name(g.catalogue, index, i)).c_str()));
    }
    // Shown only when there is a choice to make. Most plugins hold one .w2p,
    // and a dropdown with one row in it is a control that asks a question with
    // no second answer.
    const bool choose = variants > 1;
    EnableWindow(g.variants, choose ? TRUE : FALSE);
    ShowWindow(g.variants, choose ? SW_SHOW : SW_HIDE);
    const bool active = g.active_plugin == ul_plugin_id(g.catalogue, index);
    SendMessageW(g.variants, CB_SETCURSEL,
                 static_cast<WPARAM>(active ? g.active_variant : 0), 0);

    // Videos first: a plugin that has one has it because the author thought it
    // was the best thing to look at, and a screenshot is still one click away.
    for (int i = 0; i < ul_plugin_video_count(g.catalogue, index); ++i) {
      GalleryItem item;
      item.video_url = Utf8(ul_plugin_video_url(g.catalogue, index, i));
      item.video_id = ul_plugin_video_id(g.catalogue, index, i);
      item.list_id = ul_plugin_video_list_id(g.catalogue, index, i);
      // Only when it has arrived — a video's file named by its id, a
      // playlist's by its list id. A missing one still shows as a video: the
      // play badge is drawn over the placeholder, and it fills in by itself
      // once the fetch finishes.
      const std::string named = !item.video_id.empty() ? item.video_id : item.list_id;
      const std::wstring thumb = ThumbnailPath(named);
      if (FileExists(thumb)) item.image = thumb;
      shots.push_back(item);
    }
    for (int i = 0; i < ul_plugin_image_count(g.catalogue, index); ++i) {
      GalleryItem item;
      item.image = g.package_folder + L"\\" + Utf8(ul_plugin_image(g.catalogue, index, i));
      shots.push_back(item);
    }
  }
  // A different plugin is a different gallery, so whatever was playing is no
  // longer what is being looked at.
  HideVideo();
  // A different plugin is a different gallery: the player's kept embeds
  // describe items no longer on offer.
  ResetVideoPlayer();
  SetSlideshowItems(g.shot, shots);
  // The first item is on screen now; if it is a video, load it behind the
  // scenes so the play badge answers a click without a boot-up wait.
  PrimeCurrentVideo();
  // Up for every row, even one with nothing of its own: the panel draws its
  // fallback screen then. A gallery that disappears tells a reader nothing —
  // not about the plugin and not about what that half of the window is for,
  // and most plugins have nothing until dannyldd adds some.
  g.has_shots = g.catalogue != nullptr;
  const bool many = shots.size() > 1;
  EnableWindow(g.shot_prev, many ? TRUE : FALSE);
  EnableWindow(g.shot_next, many ? TRUE : FALSE);

  UpdatePlayButton();
  // The dropdown appearing or going takes a row with it, so the column is laid
  // out again rather than left with a hole or an overlap.
  Layout();
}

/// The button says what it will play, which is whatever is selected. There is
/// no separate "activate": selecting a plugin and pressing Play is the whole
/// interaction, and a second button asking the user to confirm the selection
/// they just made was a step that existed only because the code needed it.
/// How wide `text` draws in `font`.
int TextWidth(const std::wstring& text, HFONT font) {
  HDC dc = GetDC(g.window);
  HGDIOBJ previous = SelectObject(dc, font);
  RECT box = {0, 0, 0, 0};
  DrawTextW(dc, text.c_str(), -1, &box, DT_CALCRECT | DT_SINGLELINE);
  SelectObject(dc, previous);
  ReleaseDC(g.window, dc);
  return box.right - box.left;
}

/// `text` cut down with an ellipsis until it draws inside `room`.
std::wstring Ellipsised(const std::wstring& text, int room, HFONT font) {
  if (room <= 0 || TextWidth(text, font) <= room) return text;
  std::wstring cut = text;
  while (!cut.empty() && TextWidth(cut + L"…", font) > room) cut.pop_back();
  return cut + L"…";
}

void UpdatePlayButton() {
  std::wstring label = Text(IDS_PLAY);
  if (g.catalogue) {
    const LRESULT row = SendMessageW(g.plugins, LB_GETCURSEL, 0, 0);
    // Row 0 keeps the plain "Play": "Play No plugin" is a sentence nobody
    // should have to parse, and playing with no plugin is just playing.
    if (row != LB_ERR && row != 0) {
      wchar_t name[256] = {};
      SendMessageW(g.plugins, LB_GETTEXT, static_cast<WPARAM>(row),
                   reinterpret_cast<LPARAM>(name));
      // The button is a fixed size and the name is cut to fit it. Sizing the
      // button to its label instead put "Play *war2 remaster Rebalance from
      // december 2024, available for War2Combat*" across half the window and
      // dragged the dropdown under it out to match, so the primary action moved
      // every time the selection did.
      if (*name) {
        // Measured in Play's own large serif, which is what draws the label.
        const int room =
            kPlayWidth - 40 - TextWidth(Format(IDS_PLAY_NAMED, L""), ThemePlayFont());
        label = Format(IDS_PLAY_NAMED, Ellipsised(name, room, ThemePlayFont()).c_str());
      }
    }
  }
  SetWindowTextW(g.play, label.c_str());
}

void FillPluginList() {
  SendMessageW(g.plugins, LB_RESETCONTENT, 0, 0);
  // Nothing installed, nothing to list. Not even the "No plugin" row:
  // there is no Unification to run only, and a list with one unusable choice
  // in it invites a click that cannot do anything.
  if (!g.catalogue || g.installed_version.empty()) {
    ShowPluginDetails();
    return;
  }
  SendMessageW(g.plugins, LB_ADDSTRING, 0,
               reinterpret_cast<LPARAM>(Text(IDS_NO_PLUGIN).c_str()));
  int select = 0;
  {
    for (int i = 0; i < ul_catalogue_count(g.catalogue); ++i) {
      SendMessageW(g.plugins, LB_ADDSTRING, 0,
                   reinterpret_cast<LPARAM>(Utf8(ul_plugin_name(g.catalogue, i)).c_str()));
      if (g.active_plugin == ul_plugin_id(g.catalogue, i)) select = i + 1;
    }
  }
  SendMessageW(g.plugins, LB_SETCURSEL, static_cast<WPARAM>(select), 0);
  ShowPluginDetails();
}

/// The version on offer at the end of the status sentence, so "Update" and
/// "Install Unification Mod" say which version they are talking about.
void UpdateLatestVersion() {
  SetWindowTextW(g.latest, FromUtf8(g.latest_version).c_str());
}

void UpdateActionButton() {
  UINT label = IDS_ACTION_CHECK;
  bool enabled = true;
  if (g.busy) {
    label = IDS_ACTION_CANCEL;
    // Except an uninstall, which is not cancellable: it is a few seconds of
    // putting files back, and stopping it halfway would leave the game folder
    // half-restored — the one state this program exists to make impossible.
    enabled = g.job != Job::Uninstall;
  } else if (!g.latest_version.empty()) {
    if (g.installed_version.empty()) {
      label = IDS_ACTION_INSTALL;
    } else if (ul_update_available(g.installed_version.c_str(),
                                  g.latest_version.c_str())) {
      label = IDS_ACTION_UPDATE;
    }
  }
  SetWindowTextW(g.action, Text(label).c_str());
  EnableWindow(g.action, enabled ? TRUE : FALSE);
  // The button is sized to its words, and the words just changed.
  Layout();
}

/// Puts the status sentence on screen and lays the header out again. The
/// version label sits at the end of this text, so a new sentence is a new
/// place for it. UI thread only — the worker goes through SetStatus, which
/// posts here.
void ShowStatus(const std::wstring& text) {
  SetWindowTextW(g.status, text.c_str());
  Layout();
  // A shorter sentence leaves the tail of the old one on screen: the labels
  // shrank and moved, and nothing else repaints the strip they vacated. The
  // children clip out of the erase, so this costs one wipe of bare background.
  RECT client;
  GetClientRect(g.window, &client);
  RECT strip = {0, 0, client.right, 12 + 28};
  InvalidateRect(g.window, &strip, TRUE);
  // And the labels themselves, erased in full. A STATIC with SS_CENTERIMAGE
  // does not erase its own background on a text change, so a new sentence can
  // land on the old one's leftovers whenever the box happens not to move.
  InvalidateRect(g.status, nullptr, TRUE);
  InvalidateRect(g.latest, nullptr, TRUE);
}

void UpdateStatusLine() {
  // The published version is in the title and again beside the button. Saying
  // it a third time in the sentence between them was three versions on one
  // screen for a program that only ever has one. The only number this line
  // carries now is the *installed* one, and only when it differs.
  std::wstring text;
  if (!g.latest_version.empty()) {
    if (g.installed_version.empty()) {
      text = Text(IDS_NOT_INSTALLED);
    } else if (ul_update_available(g.installed_version.c_str(),
                                   g.latest_version.c_str())) {
      text = Format(IDS_UPDATE_AVAILABLE, FromUtf8(g.installed_version).c_str());
    } else {
      text = Text(IDS_UP_TO_DATE);
    }
  } else if (!g.installed_version.empty()) {
    text = Text(IDS_INSTALLED);
  }
  if (!text.empty()) ShowStatus(text);
}

void Refresh() {
  UpdateLatestVersion();
  UpdateStatusLine();
  UpdateActionButton();
  const bool installed = !g.installed_version.empty();
  // The changelog is worth reading before installing anything, so it is offered
  // as soon as there is one — which is after the first check, not after an
  // install.
  EnableWindow(g.changelog, g.release && ul_release_note_count(g.release) > 0);
  // Nothing to play until the mod is there. Play launches the package's own
  // Unification.exe, which does not exist before an install.
  EnableWindow(g.play, installed && !g.game_folder.empty() && !g.busy);
  EnableWindow(g.plugins, installed && !g.busy);
  // Which row is ticked is read at paint time, so a job that changed it has to
  // ask for a repaint rather than assume one.
  InvalidateRect(g.plugins, nullptr, FALSE);
  ShowWindow(g.progress, g.busy ? SW_SHOW : SW_HIDE);
  ShowPluginDetails();
}

// ------------------------------------------------------------- the workers

/// Lists a shared OneDrive folder and answers with the newest package in it,
/// or "" with `problem` set.
///
/// Three requests, all of them anonymous. Every decision here belongs to the
/// core — the URLs, the body, what the answers mean — and the only thing this
/// function knows is how to send them and in what order.
std::wstring ListOneDriveFolder(const std::wstring& share_url, std::wstring& problem,
                                std::string& picked_name) {
  picked_name.clear();

  // The token, trying each application id the core knows until one is accepted.
  std::wstring authorisation;
  for (int attempt = 0; *ul_onedrive_badger_body(attempt); ++attempt) {
    FetchOptions token_request;
    token_request.method = L"POST";
    token_request.headers = {L"Content-Type: application/json",
                             L"Accept: application/json"};
    token_request.body = ul_onedrive_badger_body(attempt);
    const FetchResult reply =
        FetchToMemory(FromUtf8(ul_onedrive_badger_url()), token_request);
    if (!reply.ok) {
      problem = reply.error;
      continue;
    }
    char* token = ul_onedrive_read_badger(reply.body.data(), reply.body.size());
    if (!token) {
      problem = Utf8(ul_last_error());
      continue;
    }
    authorisation = L"Authorization: Badger " + Utf8(token);
    ul_free(token);
    break;
  }
  if (authorisation.empty()) return {};

  char* item_url = ul_onedrive_item_url(ToUtf8(share_url).c_str());
  if (!item_url) {
    problem = Text(IDS_ERR_FOLDER_LINK);
    return {};
  }
  FetchOptions redeem;
  redeem.method = L"POST";
  // `Prefer: autoredeem` is what turns an authenticated request into an
  // authorised one. Without it the same URL answers 403 accessDenied, which
  // reads exactly like a private link and is not one.
  redeem.headers = {authorisation, L"Prefer: autoredeem", L"Accept: application/json",
                    L"Content-Type: text/plain;charset=UTF-8"};
  const FetchResult item_reply = FetchToMemory(Utf8(item_url), redeem);
  ul_free(item_url);
  if (!item_reply.ok && item_reply.body.empty()) {
    problem = item_reply.error;
    return {};
  }
  // A 4xx still carries a body that says which step failed, and the core reads
  // it into a sentence rather than leaving the user with a number.
  char* drive_id = nullptr;
  char* item_id = nullptr;
  if (ul_onedrive_read_item(item_reply.body.data(), item_reply.body.size(), &drive_id,
                            &item_id) != UL_OK) {
    problem = Utf8(ul_last_error());
    ul_free(drive_id);
    ul_free(item_id);
    return {};
  }

  char* children_url = ul_onedrive_children_url(drive_id, item_id);
  ul_free(drive_id);
  ul_free(item_id);
  if (!children_url) {
    problem = Text(IDS_ERR_FOLDER_LINK);
    return {};
  }
  FetchOptions listing;
  listing.headers = {authorisation, L"Accept: application/json"};
  const FetchResult children = FetchToMemory(Utf8(children_url), listing);
  ul_free(children_url);
  if (!children.ok && children.body.empty()) {
    problem = children.error;
    return {};
  }
  // The version the mod page states narrows the field; the newest of what is
  // left is taken. The name comes back because the download URL has none in it.
  char* name = nullptr;
  char* picked = ul_folder_pick_archive(children.body.data(), children.body.size(),
                                        g.latest_version.c_str(), &name);
  if (name) {
    picked_name = name;
    ul_free(name);
  }
  if (!picked) {
    problem = Utf8(ul_last_error());
    return {};
  }
  const std::wstring package_url = Utf8(picked);
  ul_free(picked);
  return package_url;
}

/// Walks the release's candidate sources until one yields an archive on disk.
/// The chain is the core's ranking; the fetching, and the following of a
/// pointer, is the host's.
bool DownloadPackage(std::wstring& archive_path) {
  std::wstring last_problem;
  // Said before the first request, not after: the first hop can be a slow
  // server and thirty seconds of a frozen status line reads as a hang.
  SetStatus(Text(IDS_LISTING));

  // The count is re-read every time round, not captured: following a pointer
  // *adds* sources, and the whole reason a pointer is followed is to reach the
  // one it names. Capturing the count first meant the list on the mod page was
  // walked, the pointer was fetched and read, and the link it produced was then
  // never visited — which failed in under a second and looked like "no links".
  //
  // New sources are ranked below everything already queued and the sort is
  // stable, so nothing already passed can move behind the cursor.
  int followed = 0;
  for (int i = 0; i < ul_release_source_count(g.release) && !g.cancel; ++i) {
    const int kind = ul_release_source_kind(g.release, i);
    char* direct = ul_direct_download_url(ul_release_source_url(g.release, i));
    const std::wstring url = Utf8(direct);
    ul_free(direct);
    if (url.empty()) continue;

    if (kind == UL_SOURCE_POINTER) {
      // A few hundred bytes whose contents are a link. Fetched to a file
      // because it is a rar and the decoder reads files, then read out of it,
      // and whatever it names is added to the end of the queue.
      //
      // Bounded: a pointer that names another pointer is a chain, and a pointer
      // that names itself is a loop. Three is more hops than the real route has.
      if (++followed > 3) continue;
      const std::wstring pointer_file = g.store + L"\\pointer.rar";
      const FetchResult fetched = FetchToFile(url, pointer_file, nullptr);
      if (!fetched.ok) {
        last_problem = fetched.error;
        continue;
      }
      ul_archive* archive = ul_archive_open(ToUtf8(pointer_file).c_str());
      if (archive) {
        for (int entry = 0; entry < ul_archive_count(archive); ++entry) {
          size_t length = 0;
          char* text = ul_archive_read_entry(archive, entry, &length);
          if (!text) continue;
          ul_release_add_pointer_text(g.release, text, length);
          ul_free(text);
        }
        ul_archive_close(archive);
      }
      DeleteFileW(pointer_file.c_str());
      continue;
    }

    if (ul_url_is_folder(ToUtf8(url).c_str())) {
      // A folder cannot be downloaded; it has to be listed first, which takes
      // three requests. The core builds each and reads each answer — see the
      // long note at the top of url.cpp for what each one is and how the shape
      // of them was found.
      SetStatus(Text(IDS_LISTING));
      std::string picked_name;
      const std::wstring package_url = ListOneDriveFolder(url, last_problem, picked_name);
      if (package_url.empty()) continue;
      // What the file itself claims to be, which is not always what the mod page
      // says. dannyldd bumps the page and uploads the package separately, so
      // there is a window where the page has moved on and the folder has not —
      // and recording the page's version for the older bytes would leave the
      // machine believing it has a release it does not, and never offering the
      // real one again.
      char* from_name = ul_version_from_filename(picked_name.c_str());
      g.downloaded_version = from_name ? from_name : "";
      ul_free(from_name);
      SetStatus(Format(IDS_DOWNLOADING, FromUtf8(g.latest_version).c_str(),
                       Bytes(0).c_str(), Bytes(-1).c_str()));
      const std::wstring target = g.store + L"\\download.rar";
      const FetchResult got = FetchToFile(
          package_url, target, [](int64_t done, int64_t total) {
            SetStatus(Format(IDS_DOWNLOADING, FromUtf8(g.latest_version).c_str(),
                             Bytes(done).c_str(), Bytes(total).c_str()));
            SetProgress(total > 0 ? static_cast<int>(done * 1000 / total) : -1);
            return !g.cancel;
          });
      if (got.ok) {
        archive_path = target;
        return true;
      }
      last_problem = got.error;
      continue;
    }

    SetStatus(Format(IDS_DOWNLOADING, FromUtf8(g.latest_version).c_str(),
                     Bytes(0).c_str(), Bytes(-1).c_str()));
    const std::wstring target = g.store + L"\\download.rar";
    const FetchResult got = FetchToFile(url, target, [](int64_t done, int64_t total) {
      SetStatus(Format(IDS_DOWNLOADING, FromUtf8(g.latest_version).c_str(),
                       Bytes(done).c_str(), Bytes(total).c_str()));
      SetProgress(total > 0 ? static_cast<int>(done * 1000 / total) : -1);
      return !g.cancel;
    });
    if (got.ok) {
      archive_path = target;
      return true;
    }
    last_problem = got.error;
  }

  // Cancelling is not a failure to find anything, and must not be reported as
  // one: the loop above exits on `cancel` exactly like it exits on running out
  // of sources, and without this a user who pressed Cancel was told the mod
  // page has no download links on it.
  if (g.cancel) return false;
  {
    std::lock_guard<std::mutex> held(g.status_lock);
    g.error_text = last_problem.empty()
                       ? Format(IDS_ERR_NO_ARCHIVE_LINK,
                                FromUtf8(g.latest_version).c_str())
                       : last_problem;
  }
  return false;
}

void CheckWorker() {
  SetStatus(Text(IDS_CHECKING));
  const FetchResult profile = FetchToMemory(kProfileUrl);
  if (!profile.ok) {
    std::lock_guard<std::mutex> held(g.status_lock);
    g.error_text = profile.error;
    Finish(UL_ERR_PARSE, Job::Check);
    return;
  }
  ul_release* release =
      ul_release_parse(profile.body.data(), profile.body.size(), kModId);
  if (!release) {
    std::lock_guard<std::mutex> held(g.status_lock);
    g.error_text = Utf8(ul_last_error());
    Finish(UL_ERR_PARSE, Job::Check);
    return;
  }
  // The changelog is a second request and a nicety: a failure here must not
  // stop an update from being offered.
  const FetchResult updates = FetchToMemory(kUpdatesUrl);
  if (updates.ok) {
    ul_release_add_updates(release, updates.body.data(), updates.body.size());
  }

  if (g.release) ul_release_free(g.release);
  g.release = release;
  g.latest_version = ul_release_version(release);
  Finish(UL_OK, Job::Check);
}

/// The half of an install that runs once the package is on disk, whether it
/// was just fetched or was already in the store from last time.
void InstallFromPackage() {
  // The extraction folder, which is the package folder minus whatever wrapper
  // the archive had. Kept so the store can be tidied once the install lands.
  const std::wstring unpacked =
      PackageFolderFor(g.downloaded_version.empty() ? g.latest_version
                                                    : g.downloaded_version);
  SetStatus(Text(IDS_INSTALLING));
  const std::vector<std::string> package_paths = WalkFiles(g.package_folder);

  // What is in the game folder that is *not* already ours.
  //
  // Anything in this list gets moved aside before it is overwritten, so that an
  // uninstall can put it back. Files a previous install of this mod wrote are
  // not that: backing them up would set aside 1.2 GB of the mod's own files as
  // if they were the player's, and an uninstall would then dutifully restore
  // the mod it had just removed. An update from v6.5 to v6.6 is exactly this
  // case, and it is the common one.
  std::set<std::string> ours;
  {
    const std::wstring text = ReadTextFile(ReceiptPath());
    const std::string utf8 = ToUtf8(text);
    if (ul_receipt* previous = ul_receipt_parse(utf8.data(), utf8.size())) {
      const std::wstring game = TrimTrailing(g.game_folder) + L"\\";
      for (int i = 0; i < ul_receipt_count(previous); ++i) {
        std::wstring dest = FromUtf8(ul_receipt_dest(previous, i));
        for (wchar_t& c : dest) {
          if (c == L'/') c = L'\\';
        }
        if (dest.size() > game.size() &&
            _wcsnicmp(dest.c_str(), game.c_str(), game.size()) == 0) {
          std::wstring relative = dest.substr(game.size());
          for (wchar_t& c : relative) {
            if (c == L'\\') c = L'/';
          }
          ours.insert(ToUtf8(relative));
        }
      }
      ul_receipt_free(previous);
    }
  }
  std::vector<std::string> existing_paths;
  for (const std::string& path : WalkFiles(g.game_folder)) {
    if (ours.count(path) == 0) existing_paths.push_back(path);
  }
  std::string package_list, existing_list;
  for (const std::string& path : package_paths) {
    package_list.append(path);
    package_list.push_back('\0');
  }
  package_list.push_back('\0');
  for (const std::string& path : existing_paths) {
    existing_list.append(path);
    existing_list.push_back('\0');
  }
  existing_list.push_back('\0');

  ul_plan* plan = ul_plan_install(ToUtf8(g.package_folder).c_str(),
                                  ToUtf8(g.game_folder).c_str(), package_list.c_str(),
                                  existing_list.c_str(), ToUtf8(BackupFolder()).c_str());
  if (PlanBlocked(plan, Job::Install)) {
    ul_plan_free(plan);
    return;
  }
  const std::string installing =
      g.downloaded_version.empty() ? g.latest_version : g.downloaded_version;
  ul_receipt* receipt = ul_receipt_create(installing.c_str());
  ul_receipt_set_game_dir(receipt, ToUtf8(g.game_folder).c_str());
  ul_receipt_set_package_dir(receipt, ToUtf8(g.package_folder).c_str());
  ul_receipt_set_plugin(receipt, g.active_plugin.c_str());
  ul_receipt_set_variant(receipt, g.active_variant);

  std::wstring error;
  const int ran = RunPlan(plan, receipt, [](int step, int steps) {
    SetProgress(steps > 0 ? step * 1000 / steps : -1);
    return !g.cancel;
  }, error);
  ul_plan_free(plan);

  // Written whatever happened. An install that failed halfway has still changed
  // the game folder, and the record of what it changed is the only way back.
  size_t length = 0;
  char* json = ul_receipt_to_json(receipt, &length);
  if (json) {
    WriteTextFile(ReceiptPath(), std::string(json, length));
    ul_free(json);
  }
  ul_receipt_free(receipt);

  if (ran != UL_OK) {
    std::lock_guard<std::mutex> held(g.status_lock);
    g.error_text = error;
    Finish(ran, Job::Install);
    return;
  }
  g.installed_version =
      g.downloaded_version.empty() ? g.latest_version : g.downloaded_version;

  // The game folder now holds the package. Everything but Plugins/ comes out
  // of the store so the same 1.2 GB is not sitting in two places, and the
  // stamp says so — an install cannot be run from what is left, and the next
  // uninstall will move the rest back.
  char* prefix = nullptr;
  {
    const std::string held = ToUtf8(ReadTextFile(StampPath(unpacked)));
    if (!held.empty()) prefix = ul_package_stamp_root(held.data(), held.size());
  }
  const std::string root = prefix ? prefix : "";
  ul_free(prefix);
  PruneInstalledFiles(unpacked, root);
  WriteStamp(unpacked, g.installed_version, false);
  Finish(UL_OK, Job::Install);
}

void InstallWorker() {
  const std::string version =
      g.latest_version.empty() ? g.installed_version : g.latest_version;

  // Already unpacked and vouched for by its stamp: skip the download and the
  // unpacking entirely. This is the whole point of keeping it — a reinstall, or
  // an uninstall followed by a change of mind, should not cost another 800 MB.
  const std::wstring cached = CachedPackage(version);
  if (!cached.empty()) {
    SetStatus(Text(IDS_USING_CACHE));
    g.downloaded_version = version;
    g.package_folder = cached;
    InstallFromPackage();
    return;
  }

  std::wstring archive_path;
  if (!DownloadPackage(archive_path)) {
    // Cancelling comes back the same way as running out of places to look, so
    // which of the two it was is asked here rather than guessed from the result.
    Finish(g.cancel ? UL_ERR_CANCELLED : UL_ERR_NO_ARCHIVE, Job::Install);
    return;
  }

  SetStatus(Text(IDS_EXTRACTING));
  SetProgress(0);
  const std::wstring unpacked = PackageFolderFor(
      g.downloaded_version.empty() ? g.latest_version : g.downloaded_version);
  RemoveTree(unpacked);
  EnsureFolder(unpacked);

  ul_archive* archive = ul_archive_open(ToUtf8(archive_path).c_str());
  if (!archive) {
    std::lock_guard<std::mutex> held(g.status_lock);
    g.error_text = Utf8(ul_last_error());
    Finish(UL_ERR_OPEN, Job::Install);
    return;
  }
  const int extracted = ul_archive_extract(
      archive, ToUtf8(unpacked).c_str(),
      [](void*, const char*, int64_t done, int64_t total) -> int {
        SetProgress(total > 0 ? static_cast<int>(done * 1000 / total) : -1);
        return g.cancel ? 0 : 1;
      },
      nullptr);
  const int64_t total_size = ul_archive_total_size(archive);
  (void)total_size;
  ul_archive_close(archive);
  DeleteFileW(archive_path.c_str());
  if (extracted != UL_OK) {
    std::lock_guard<std::mutex> held(g.status_lock);
    g.error_text = Utf8(ul_last_error());
    Finish(extracted, Job::Install);
    return;
  }

  // The package may be wrapped in a folder named after the release. Finding the
  // real root is the core's job; the host just walks and hands over the paths.
  std::vector<std::string> unpacked_paths = WalkFiles(unpacked);
  std::string joined;
  for (const std::string& path : unpacked_paths) {
    joined.append(path);
    joined.push_back('\0');
  }
  joined.push_back('\0');
  char* root = ul_package_root(joined.c_str());
  const std::string root_prefix = root ? root : "";
  const std::wstring package =
      root_prefix.empty() ? unpacked : unpacked + L"\\" + FromUtf8(root_prefix);
  ul_free(root);
  const std::string installed_as =
      g.downloaded_version.empty() ? g.latest_version : g.downloaded_version;
  g.package_folder = TrimTrailing(package);

  g.package_folder = TrimTrailing(package);

  // The stamp, written only now — after the extraction returned UL_OK. It is
  // what tells the next run that this folder is a whole package and not the
  // wreckage of a cancelled one.
  WriteStamp(unpacked, installed_as, true);
  PruneCache(installed_as);

  InstallFromPackage();
}

/// Stops a job whose files something else has open, and says which program.
///
/// Windows reports this as a sharing violation naming a path, which sends
/// people looking at permissions. It is almost always the game: it holds every
/// .w2p in plugin\ open for as long as it runs. Checked before the first step,
/// because a plugin switch abandoned halfway has already deleted the old file.
bool PlanBlocked(const ul_plan* plan, Job job) {
  const std::wstring blocked = PlanBlockedBy(plan);
  if (blocked.empty()) return false;
  std::lock_guard<std::mutex> held(g.status_lock);
  g.error_text = Text(IDS_ERR_GAME_RUNNING);
  Finish(UL_ERR_WRITE, job);
  return true;
}

void SetPluginWorker(std::string plugin_id, int variant) {
  SetStatus(Text(IDS_INSTALLING));
  ul_plan* plan = ul_plan_set_plugin(
      g.catalogue, ToUtf8(g.package_folder).c_str(), ToUtf8(g.game_folder).c_str(),
      g.active_plugin.c_str(), plugin_id.c_str(), variant);
  if (PlanBlocked(plan, Job::SetPlugin)) {
    ul_plan_free(plan);
    return;
  }
  std::wstring error;
  const int ran = RunPlan(plan, nullptr, [](int step, int steps) {
    SetProgress(steps > 0 ? step * 1000 / steps : -1);
    return !g.cancel;
  }, error);
  ul_plan_free(plan);
  if (ran != UL_OK) {
    std::lock_guard<std::mutex> held(g.status_lock);
    g.error_text = error;
    Finish(ran, Job::SetPlugin);
    return;
  }
  g.active_plugin = plugin_id;
  g.active_variant = variant;
  SaveActivePlugin();
  Finish(UL_OK, Job::SetPlugin);
}

void UninstallWorker() {
  SetStatus(Text(IDS_REMOVING));
  const std::wstring text = ReadTextFile(ReceiptPath());
  const std::string utf8 = ToUtf8(text);
  ul_plan* plan = ul_plan_uninstall(utf8.data(), utf8.size());
  if (!plan) {
    std::lock_guard<std::mutex> held(g.status_lock);
    g.error_text = Utf8(ul_last_error());
    Finish(UL_ERR_PARSE, Job::Uninstall);
    return;
  }
  if (PlanBlocked(plan, Job::Uninstall)) {
    ul_plan_free(plan);
    return;
  }
  std::wstring error;
  const int ran = RunPlan(plan, nullptr, [](int step, int steps) {
    SetProgress(steps > 0 ? step * 1000 / steps : -1);
    return !g.cancel;
  }, error);
  ul_plan_free(plan);
  if (ran != UL_OK) {
    std::lock_guard<std::mutex> held(g.status_lock);
    g.error_text = error;
    Finish(ran, Job::Uninstall);
    return;
  }
  // Only once the game folder is back the way it was. Deleting the store first
  // would throw away the backups the restore needs.
  DeleteFileW(ReceiptPath().c_str());
  RemoveTree(BackupFolder());
  // The folders the moved files were in are still standing and are now empty.
  // A game folder left with an empty UniFiles\ in it has not been put back the
  // way it was found.
  RemoveEmptyFolders(g.game_folder);

  // The plan moved the package back into the store rather than deleting it, so
  // the copy there is whole again and the next install needs no download. The
  // stamp has to say so, and its file count has just changed by a thousand.
  if (!g.package_folder.empty()) {
    const std::wstring unpacked = PackageFolderFor(g.installed_version);
    std::string root;
    const std::string held = ToUtf8(ReadTextFile(StampPath(unpacked)));
    if (!held.empty()) {
      char* prefix = ul_package_stamp_root(held.data(), held.size());
      if (prefix) root = prefix;
      ul_free(prefix);
    }
    WriteStamp(unpacked, g.installed_version, true);
  }
  g.installed_version.clear();
  g.active_plugin.clear();
  g.active_variant = 0;
  g.package_folder.clear();
  Finish(UL_OK, Job::Uninstall);
}

// --------------------------------------------------------------- commands

/// Whether the game folder can be written to, asking for administrator rights
/// if it cannot. Returns false when the caller should stop — either because the
/// user declined the prompt, or because an elevated copy is now doing the job.
bool EnsureWritable(Job intent) {
  if (g.game_folder.empty()) {
    Say(Text(IDS_ERR_NO_GAME), MB_OK | MB_ICONWARNING);
    return false;
  }
  if (IsWritable(g.game_folder)) return true;
  if (IsElevated()) {
    // Already elevated and still cannot write: not a rights problem. A
    // read-only volume, or something holding the folder.
    Say(Format(IDS_ERR_NEEDS_ADMIN, g.game_folder.c_str()), MB_OK | MB_ICONWARNING);
    return false;
  }
  // A Program Files install needs administrator rights; a portable one does
  // not, and asking every user for them would be asking most of them for
  // nothing. So it is asked for here, when it is actually needed, and only then.
  Say(Format(IDS_ERR_NEEDS_ADMIN, g.game_folder.c_str()));
  std::wstring arguments = L"--game \"" + g.game_folder + L"\"";
  if (intent == Job::Install) arguments += L" --resume install";
  if (intent == Job::Uninstall) arguments += L" --resume uninstall";
  if (RelaunchElevated(arguments)) {
    PostMessageW(g.window, WM_CLOSE, 0, 0);
  }
  return false;
}

void Start(Job job, std::string plugin_id, int variant) {
  if (g.busy) return;
  if (job == Job::Install || job == Job::SetPlugin || job == Job::Uninstall) {
    if (GameIsRunning()) {
      Say(Text(IDS_ERR_GAME_RUNNING), MB_OK | MB_ICONWARNING);
      return;
    }
    if (!EnsureWritable(job)) return;
  }
  if (g.worker.joinable()) g.worker.join();
  g.busy = true;
  g.cancel = false;
  g.job = job;
  {
    std::lock_guard<std::mutex> held(g.status_lock);
    g.error_text.clear();
  }
  g.progress_permille = 0;
  InvalidateRect(g.progress, nullptr, FALSE);
  Refresh();
  switch (job) {
    case Job::Check:     g.worker = std::thread(CheckWorker); break;
    case Job::Install:   g.worker = std::thread(InstallWorker); break;
    case Job::Uninstall: g.worker = std::thread(UninstallWorker); break;
    case Job::SetPlugin:
      g.worker = std::thread(SetPluginWorker, plugin_id, variant);
      break;
    default: g.busy = false; break;
  }
}

void OnAction() {
  if (g.busy) {
    // The button is disabled during an uninstall, but a keyboard can still
    // reach a default button; the guard has to live here too.
    if (g.job == Job::Uninstall) return;
    g.cancel = true;
    ShowStatus(Text(IDS_CANCELLED));
    return;
  }
  if (g.latest_version.empty()) {
    Start(Job::Check);
    return;
  }
  if (g.installed_version.empty() ||
      ul_update_available(g.installed_version.c_str(), g.latest_version.c_str())) {
    Start(Job::Install);
    return;
  }
  Start(Job::Check);
}

/// A different difficulty for the plugin that is already on. Applied as soon as
/// it is chosen: the alternative is a button that exists only for this case,
/// and the change is one file.
/// Loads whatever is selected, if that is not already what is loaded.
///
/// Called every time the selection or the difficulty changes, because choosing
/// a plugin *is* turning it on — waiting for a second button was a step that
/// existed only because the code wanted one.
///
/// While a load is running this only remembers that it needs running again.
/// Selection changes arrive one per arrow key, and starting a file copy for
/// each of them would queue up work for rows the user only passed through.
void ApplySelection() {
  if (!g.catalogue) return;
  if (g.busy) {
    g.catch_up = true;
    return;
  }
  std::string id;
  int variant = 0;
  if (!SelectionNeedsLoading(&id, &variant)) return;
  Start(Job::SetPlugin, id, variant);
}

bool SelectionNeedsLoading(std::string* id, int* variant) {
  const int index = SelectedPlugin();
  const std::string wanted = index < 0 ? std::string() : ul_plugin_id(g.catalogue, index);
  int chosen = 0;
  if (index >= 0) {
    const LRESULT selected = SendMessageW(g.variants, CB_GETCURSEL, 0, 0);
    chosen = selected == CB_ERR ? 0 : static_cast<int>(selected);
  }
  if (id) *id = wanted;
  if (variant) *variant = chosen;
  if (wanted != g.active_plugin) return true;
  return !wanted.empty() && chosen != g.active_variant;
}

/// Starts the game. Separated from OnPlay because a plugin that had to be
/// loaded first gets here later, from OnFinished, once the copying is done.
void LaunchGame() {
  // Unification.exe is the mod's own front end and is what dannyldd's
  // instructions say to start. Without the mod installed there is still a game
  // to play, so the launcher falls back to War2Combat's own.
  const wchar_t* candidates[] = {L"Unification.exe", L"War2Launcher.bat", L"war2.exe"};
  for (const wchar_t* name : candidates) {
    const std::wstring path = g.game_folder + L"\\" + name;
    if (!FileExists(path)) continue;
    if (Launch(path, g.game_folder)) return;
  }
  Say(Text(IDS_ERR_NO_GAME), MB_OK | MB_ICONWARNING);
}

void OnPlay() {
  if (g.busy) return;
  std::string id;
  int variant = 0;
  // Normally nothing to do: selecting the plugin already loaded it. This is the
  // window where a load failed, or was refused because the game was running and
  // has since been closed — and playing without retrying it would start the
  // game on the plugin the user selected away from.
  if (g.catalogue && SelectionNeedsLoading(&id, &variant)) {
    // Loaded first, then played — never at the same time. Windows will not let
    // a file be replaced while the game has it open, and the game reads its
    // plugin at startup, so the order here is the whole of the correctness.
    g.play_when_ready = true;
    Start(Job::SetPlugin, id, variant);
    return;
  }
  LaunchGame();
}

void OnUninstall() {
  const std::wstring question =
      Format(IDS_CONFIRM_UNINSTALL, FromUtf8(g.installed_version).c_str());
  if (MessageBoxW(g.window, question.c_str(), Text(IDS_APP_TITLE).c_str(),
                  MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) {
    return;
  }
  Start(Job::Uninstall);
}

/// The mod page. Known from the release when one has been fetched, and hard
/// wired to the same post otherwise — a user who cannot reach GameBanana's API
/// should still be able to reach GameBanana.
void OnOpenModPage() {
  std::wstring url = L"https://gamebanana.com/mods/644456";
  if (g.release && *ul_release_page_url(g.release)) {
    url = Utf8(ul_release_page_url(g.release));
  }
  OpenInBrowser(url);
}

void OnChangeFolder() {
  const std::wstring chosen = AskForGameFolder(g.window);
  if (chosen.empty()) return;
  if (!IsGameFolder(chosen)) {
    Say(Text(IDS_ERR_NO_GAME), MB_OK | MB_ICONWARNING);
    return;
  }
  g.game_folder = chosen;
  RememberGameFolder(chosen);
  Refresh();
}

void OnChangelog() { ShowChangelog(g.window, g.release); }

/// Throws away downloads that nothing is using.
///
/// Never the installed version's folder: that one is not a download any more,
/// it is where the plugins live. `PruneCache` keeps exactly the one named and
/// deletes the rest, which with nothing installed is all of them.
void OnClearCache() {
  if (g.busy) return;
  if (g.installed_version.empty()) {
    RemoveTree(g.store + L"\\package");
    g.package_folder.clear();
    ReadCatalogue();
    FillPluginList();
  } else {
    PruneCache(g.installed_version);
  }
  Refresh();
}

void OnSettings() {
  // The window reports which button was pressed and this decides what that
  // means, so nothing in Dialogs.cpp knows about jobs, receipts or threads.
  const int64_t cached = SpareCacheBytes();
  DisplaySettings display = ReadDisplaySettings(g.game_folder);
  const DialogAction action =
      ShowSettings(g.window, g.game_folder, g.store, FromUtf8(g.installed_version),
                   cached > 0 ? Bytes(cached) : std::wstring(), g.busy, display);
  // Saved on the way out however the window was closed: the controls *are* the
  // setting, there is no OK to press, and nothing to abandon.
  if (display.changed) SaveDisplaySettings(g.game_folder, g.store, display);
  switch (action) {
    case DialogAction::ChangeFolder: OnChangeFolder(); break;
    case DialogAction::Uninstall:    OnUninstall(); break;
    case DialogAction::ClearCache:   OnClearCache(); break;
    default: break;
  }
}

void OnFinished(int code, Job job) {
  if (g.worker.joinable()) g.worker.join();
  g.busy = false;
  g.job = Job::None;

  std::wstring error;
  {
    std::lock_guard<std::mutex> held(g.status_lock);
    error = g.error_text;
  }

  if (code == UL_ERR_CANCELLED) {
    ShowStatus(Text(IDS_CANCELLED));
  } else if (code != UL_OK) {
    if (error.empty()) error = FromUtf8(ul_error_text(code));
    ShowStatus(error);
    Say(error, MB_OK | MB_ICONWARNING);
  } else {
    ShowStatus(Text(IDS_DONE));
  }

  if (job == Job::Install && code == UL_OK) {
    ReadCatalogue();
    FillPluginList();
    FetchThumbnails();
  }
  if (job == Job::Uninstall && code == UL_OK) {
    ReadCatalogue();
    FillPluginList();
  }
  Refresh();
  if (code == UL_OK && job != Job::Check) UpdateStatusLine();

  if (job == Job::SetPlugin && g.catch_up) {
    g.catch_up = false;
    ApplySelection();
  }
  if (job == Job::SetPlugin && g.play_when_ready) {
    g.play_when_ready = false;
    // Only when the plugin actually landed. Starting the game after a failed
    // copy would launch it with whatever was there before, which is the version
    // the user just asked to change away from.
    if (code == UL_OK) LaunchGame();
  }

  // The one place a resumed job is started: after an elevated relaunch, the
  // check has to finish before there is a version to install.
  if (job == Job::Check && g.resume != Job::None && code == UL_OK) {
    const Job resume = g.resume;
    g.resume = Job::None;
    Start(resume);
  }
}

// ----------------------------------------------------------------- layout

HWND Child(const wchar_t* type, const wchar_t* text, DWORD style, int id) {
  HWND child = CreateWindowExW(0, type, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 10,
                               10, g.window,
                               reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                               GetModuleHandleW(nullptr), nullptr);
  SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(g.font), TRUE);
  return child;
}

void Layout() {
  RECT client;
  GetClientRect(g.window, &client);
  const int width = client.right;
  const int height = client.bottom;
  const int margin = 12;

  // Every control moves in one pass. Fifteen separate MoveWindow calls are
  // fifteen paints, and during a drag-resize that is what the flicker is: each
  // control repainting over a background the one before it had just repainted.
  HDWP batch = BeginDeferWindowPos(20);
  auto move = [&](HWND child, int x, int cy, int cx, int height_of) {
    if (!child) return;
    if (batch) {
      batch = DeferWindowPos(batch, child, nullptr, x, cy, cx, height_of,
                             SWP_NOZORDER | SWP_NOACTIVATE);
    } else {
      MoveWindow(child, x, cy, cx, height_of, TRUE);
    }
  };

  // --- the header ----------------------------------------------------------
  //
  // One line, all of it about the release, reading from the left margin: the
  // state of the install with the version on offer right at the end of the
  // sentence, and the one button that acts on the release at the right edge.
  // Both texts are measured — the status so the version always sits against
  // its last word (ShowStatus lays the row out again whenever the sentence
  // changes), the button because "Install Unification Mod" and "Cancel" are
  // very different widths and the right edge must hold still. The statics
  // carry SS_CENTERIMAGE so their text sits on the button's centre line.
  const int chip = 96;
  const int latest_width = 64;
  const int row_height = 28;
  int y = margin;
  wchar_t verb[64] = {};
  GetWindowTextW(g.action, verb, 64);
  // Measured in the font the themed button actually letters in, or a serif
  // wider than the system face ends in an ellipsis nobody wrote.
  int action_width = TextWidth(verb, ThemeButtonFont()) + 40;
  if (action_width < 130) action_width = 130;
  const int action_left = width - margin - action_width;
  wchar_t state[512] = {};
  GetWindowTextW(g.status, state, 512);
  int status_width = TextWidth(state, g.font) + 4;
  // The version keeps its seat against even a long error message: the sentence
  // gives way first, through its ellipsis.
  const int status_room = action_left - margin - latest_width - 24;
  if (status_width > status_room) status_width = status_room;
  move(g.status, margin, y, status_width, row_height);
  move(g.latest, margin + status_width + 8, y, latest_width, row_height);
  move(g.action, action_left, y + 1, action_width, 26);
  y += row_height;
  move(g.progress, margin, y, action_left - margin - 8, 15);
  y += 18;

  // --- the panel -----------------------------------------------------------
  //
  // The plugin's own words go beside the screenshot rather than under it: they
  // are read while choosing from the list directly above them, and the eye
  // should not have to cross the picture to get from one to the other.
  RECT player_box = {};
  RECT words_box = {};
  const int panel_top = y;
  const int panel_bottom = height - margin - kActionHeight - 12;
  const int panel_height = panel_bottom - panel_top;

  // The gallery is the largest thing on this window and most plugins have
  // nothing to put in it — v6.6 ships no images at all. With nothing to show it
  // goes entirely and the list and the description take the width, rather than
  // half the window standing there saying it is empty.
  int left_width = 320;
  if (g.has_shots) {
    // On a narrow window the left column gives way before the screenshot does,
    // down to the point where a plugin name still fits.
    const int smallest_shot = 420;
    if (width - left_width - 3 * margin < smallest_shot) {
      left_width = width - smallest_shot - 3 * margin;
    }
    if (left_width < 200) left_width = 200;
  } else {
    left_width = width - 2 * margin;
  }

  // The list takes the whole left column. The plugin's own words sit under the
  // screenshot instead, where they are read beside the thing they describe —
  // and the list holds every plugin without scrolling.
  move(g.plugins, margin, panel_top, left_width, panel_height);

  // Left alone while a video is up: the panel is deliberately hidden then, and
  // a layout pass must not put it back over the player.
  if (!VideoPlaying()) {
    for (HWND control : {g.shot, g.shot_prev, g.shot_next}) {
      ShowWindow(control, g.has_shots ? SW_SHOW : SW_HIDE);
    }
  }
  if (!g.has_shots) HideVideo();
  // The description lives under the gallery, so it goes and comes with it.
  ShowWindow(g.description, g.has_shots ? SW_SHOW : SW_HIDE);
  if (g.has_shots) {
    const int right = margin + left_width + margin;
    int shot_width = width - right - margin;
    // Never larger than the size the shots are authored at. Beyond 960x544
    // there is nothing more to see — the panel only upscales.
    if (shot_width > kShotWidth) shot_width = kShotWidth;
    int shot_height = shot_width * kShotHeight / kShotWidth;
    // The description needs a readable strip under the arrows, so the shot
    // gives way past that point rather than squeezing the words out.
    const int shot_room = panel_height - 32 - 96;
    if (shot_height > shot_room) {
      shot_height = shot_room;
      shot_width = shot_height * kShotWidth / kShotHeight;
    }
    const int shot_top = panel_top;
    move(g.shot, right, shot_top, shot_width, shot_height);
    move(g.shot_prev, right, shot_top + shot_height + 4, 60, 24);
    move(g.shot_next, right + 64, shot_top + shot_height + 4, 60, 24);
    // The words, from under the arrows to the bottom of the panel, as wide as
    // the picture they are about.
    const int words_top = shot_top + shot_height + 38;
    move(g.description, right, words_top, shot_width, panel_bottom - words_top);
    words_box = RECT{right, words_top, right + shot_width, panel_bottom};
    // The player sits exactly where the picture does, and follows it on resize.
    // Taken from the numbers rather than from ShotRect(), because the panel has
    // not been moved yet — it is in the batch, and the batch lands at the end.
    player_box = RECT{right, shot_top, right + shot_width, shot_top + shot_height};
  }

  // --- the bottom corners --------------------------------------------------
  //
  // Play is the primary action and sits where a primary action goes: bottom
  // right, on its own, with the difficulty under it. Settings, Changelog and
  // Mod page take the opposite corner — the doors that are not about playing,
  // as far from the thing you *do* as this window can put them.
  const int corner_y = height - margin - 26;
  move(g.settings, margin, corner_y, chip, 26);
  move(g.changelog, margin + chip + 8, corner_y, chip, 26);
  move(g.modpage, margin + 2 * (chip + 8), corner_y, chip, 26);
  //
  // One width, always. UpdatePlayButton cuts the name to fit it, so the button
  // and the dropdown under it stay where they are as the selection moves.
  int play_width = kPlayWidth;
  if (play_width > width / 2) play_width = width / 2;
  const int play_left = width - margin - play_width;
  move(g.play, play_left, height - margin - kActionHeight, play_width, kPlayHeight);
  // Under the button, in a slot that is there whether or not a plugin has more
  // than one version of itself. The control is hidden when there is no choice
  // to make, but its space is not given back — a layout that reflows as the
  // selection moves down the list makes the whole window twitch.
  // The last argument is how far the dropped list may reach, not the closed
  // height, which is why it is so much larger than the row it occupies.
  move(g.variants, play_left, height - margin - kVariantHeight, play_width, 240);

  if (batch) EndDeferWindowPos(batch);
  // After the batch: the player is a separate window that WebView2 owns, and it
  // has to land on the rectangle the panel actually ended up at.
  if (g.has_shots) MoveVideoPlayer(player_box);

  // The window paints ink frames around the panels, so when a panel moves the
  // old frame is stale parchment: repaint, but only when something did move —
  // Layout also runs on every status change, and a wholesale repaint per
  // download tick would flicker.
  static RECT last_list = {};
  static RECT last_words = {};
  const RECT list_box = {margin, panel_top, margin + left_width,
                         panel_top + panel_height};
  if (!EqualRect(&list_box, &last_list) || !EqualRect(&words_box, &last_words)) {
    // Only the ground the panels have just left or just taken, never the whole
    // window: picking a plugin can add or drop the variants row, and erasing
    // everything to redraw two frames repaints the parchment under the entire
    // window — which is the jarring flash on what should be a quiet change.
    RECT stale = list_box;
    UnionRect(&stale, &stale, &last_list);
    UnionRect(&stale, &stale, &words_box);
    UnionRect(&stale, &stale, &last_words);
    InflateRect(&stale, 4, 4);
    last_list = list_box;
    last_words = words_box;
    InvalidateRect(g.window, &stale, TRUE);
  }
}

void Create() {
  NONCLIENTMETRICSW metrics = {};
  metrics.cbSize = sizeof(metrics);
  SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0);
  g.font = CreateFontIndirectW(&metrics.lfMessageFont);
  metrics.lfMessageFont.lfWeight = FW_SEMIBOLD;
  g.bold_font = CreateFontIndirectW(&metrics.lfMessageFont);
  // Owner-drawn, all of them: the menu style is painted in DrawThemedButton.
  // Without the theme the bit goes and they are ordinary Windows buttons.
  const DWORD owner_button = ThemedStyle(BS_OWNERDRAW);
  g.changelog = Child(L"BUTTON", Text(IDS_CHANGELOG).c_str(),
                      owner_button | WS_TABSTOP, IDC_CHANGELOG);
  g.settings = Child(L"BUTTON", Text(IDS_SETTINGS).c_str(),
                     owner_button | WS_TABSTOP, IDC_SETTINGS);
  g.modpage = Child(L"BUTTON", Text(IDS_MOD_PAGE).c_str(),
                    owner_button | WS_TABSTOP, IDC_MOD_LINK);
  g.status = Child(L"STATIC", L"", SS_LEFT | SS_ENDELLIPSIS | SS_CENTERIMAGE,
                   IDC_STATUS);
  g.latest = Child(L"STATIC", L"", SS_LEFT | SS_ENDELLIPSIS | SS_CENTERIMAGE,
                   IDC_LATEST);
  g.action = Child(L"BUTTON", Text(IDS_ACTION_CHECK).c_str(),
                   owner_button | WS_TABSTOP, IDC_ACTION);
  // Owner-drawn rather than the system progress control, which cannot wear
  // the theme — see DrawThemedProgress. Without the theme there is nothing to
  // wear, so it is the real thing, driven by PBM_SETPOS instead of a repaint.
  if (ThemeEnabled()) {
    g.progress = Child(L"STATIC", L"", SS_OWNERDRAW, IDC_PROGRESS);
  } else {
    g.progress = Child(PROGRESS_CLASSW, L"", 0, IDC_PROGRESS);
    SendMessageW(g.progress, PBM_SETRANGE32, 0, 1000);
  }
  ShowWindow(g.progress, SW_HIDE);

  // Owner-drawn for the tick box; LBS_HASSTRINGS so the control still keeps the
  // text and the drawing code can ask it for a row rather than shadowing the
  // list in a second array that could disagree with it.
  // No WS_BORDER: the system's border colour is nobody's parchment, so the
  // window paints its own ink frame around the panel — see WM_PAINT.
  g.plugins = Child(L"LISTBOX", L"",
                    LBS_NOTIFY | ThemedStyle(LBS_OWNERDRAWFIXED) |
                        LBS_HASSTRINGS | WS_VSCROLL | WS_TABSTOP |
                        (ThemeEnabled() ? 0u : WS_BORDER),
                    IDC_PLUGINS);

  // Owner-drawn so the closed control can wear the button plaque; CBS_HASSTRINGS
  // is what keeps CB_GETLBTEXT working once the drawing is ours.
  g.variants = Child(L"COMBOBOX", L"",
                     CBS_DROPDOWNLIST | ThemedStyle(CBS_OWNERDRAWFIXED) |
                         CBS_HASSTRINGS | WS_VSCROLL | WS_TABSTOP,
                     IDC_VARIANTS);
  if (ThemeEnabled()) ThemeDropdown(g.variants);
  RegisterSlideshow();
  g.shot = CreateSlideshow(g.window, IDC_SHOT);
  g.shot_prev = Child(L"BUTTON", L"◀", owner_button | WS_TABSTOP, IDC_SHOT_PREV);
  g.shot_next = Child(L"BUTTON", L"▶", owner_button | WS_TABSTOP, IDC_SHOT_NEXT);

  // A RichEdit rather than an EDIT, for exactly one feature: EM_AUTOURLDETECT,
  // which is what makes the links an author wrote into an info.txt clickable.
  // The library is loaded before the class is asked for, because creating a
  // window of an unregistered class fails silently.
  LoadLibraryW(L"Msftedit.dll");
  g.description = Child(MSFTEDIT_CLASS, L"",
                        ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL |
                            WS_VSCROLL | (ThemeEnabled() ? 0u : WS_BORDER),
                        IDC_DESCRIPTION);
  // Off on purpose: MarkLinks finds the URLs itself, so that the range that is
  // clickable is the range that is red. Left on, the detector re-marks them in
  // the system blue on its own schedule and undoes the colouring. Without the
  // theme there is no red to protect, so the detector does the job instead.
  SendMessageW(g.description, EM_AUTOURLDETECT, ThemeEnabled() ? FALSE : TRUE, 0);
  // EN_LINK arrives as WM_NOTIFY only when asked for.
  SendMessageW(g.description, EM_SETEVENTMASK, 0, ENM_LINK);
  // The author's words on the same parchment as the boxes around them.
  SendMessageW(g.description, EM_SETBKGNDCOLOR, ThemeEnabled() ? 0 : 1,
               static_cast<LPARAM>(ThemePanel()));
  g.play = Child(L"BUTTON", Text(IDS_PLAY).c_str(), owner_button | WS_TABSTOP,
                 IDC_PLAY);

  // Started now so the engine is up by the time anyone clicks a video: the
  // first run of WebView2 on a machine takes a moment, and doing it here costs
  // nothing because it happens on a callback.
  CreateVideoPlayer(g.window, StoreFolder() + L"\\webview");
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM w, LPARAM l) {
  switch (message) {
    case WM_SIZE:
      Layout();
      return 0;
    case WM_NOTIFY: {
      // A link in the description was clicked. The control says which range
      // of its own text the click was in; that range is the URL.
      const NMHDR* header = reinterpret_cast<const NMHDR*>(l);
      if (header->hwndFrom == g.description && header->code == EN_LINK) {
        const ENLINK* link = reinterpret_cast<const ENLINK*>(l);
        if (link->msg == WM_LBUTTONUP) {
          const LONG length = link->chrg.cpMax - link->chrg.cpMin;
          if (length > 0 && length < 2048) {
            std::wstring url(static_cast<size_t>(length) + 1, L'\0');
            TEXTRANGEW range = {};
            range.chrg = link->chrg;
            range.lpstrText = &url[0];
            SendMessageW(g.description, EM_GETTEXTRANGE, 0,
                         reinterpret_cast<LPARAM>(&range));
            url.resize(wcsnlen(url.c_str(), url.size()));
            if (!url.empty()) OpenInBrowser(url);
          }
        }
        return 0;
      }
      return DefWindowProcW(window, message, w, l);
    }
    case WM_PAINT: {
      PAINTSTRUCT paint;
      HDC dc = BeginPaint(window, &paint);
      // The ink frames around the parchment panels, drawn by the window since
      // the controls lost their system borders. Without the theme they keep
      // their own borders and there is nothing for the window to draw.
      if (!ThemeEnabled()) {
        EndPaint(window, &paint);
        return 0;
      }
      HBRUSH ink = CreateSolidBrush(ThemeInk());
      for (HWND child : {g.plugins, g.description}) {
        if (!child || !IsWindowVisible(child)) continue;
        RECT box;
        GetWindowRect(child, &box);
        MapWindowPoints(nullptr, window, reinterpret_cast<POINT*>(&box), 2);
        InflateRect(&box, 1, 1);
        FrameRect(dc, &box, ink);
      }
      DeleteObject(ink);
      EndPaint(window, &paint);
      return 0;
    }
    case WM_CTLCOLORSTATIC: {
      // Ink on parchment for every label; the version, being secondary to the
      // sentence it ends, gets the fainter ink.
      // System colours: let the default handler answer.
      if (!ThemeEnabled()) return DefWindowProcW(window, message, w, l);
      HDC dc = reinterpret_cast<HDC>(w);
      SetTextColor(dc, reinterpret_cast<HWND>(l) == g.latest ? ThemeInkFaint()
                                                             : ThemeInk());
      SetBkMode(dc, TRANSPARENT);
      return reinterpret_cast<LRESULT>(ThemeBackgroundBrush());
    }
    case WM_CTLCOLORLISTBOX: {
      if (!ThemeEnabled()) return DefWindowProcW(window, message, w, l);
      HDC dc = reinterpret_cast<HDC>(w);
      SetTextColor(dc, ThemeInk());
      SetBkColor(dc, ThemePanel());
      return reinterpret_cast<LRESULT>(ThemePanelBrush());
    }
    case WM_GETMINMAXINFO: {
      auto* bounds = reinterpret_cast<MINMAXINFO*>(l);
      // Below this the screenshot stops being worth showing and the list stops
      // fitting a plugin name.
      bounds->ptMinTrackSize.x = 900;
      bounds->ptMinTrackSize.y = 640;
      return 0;
    }
    case WM_MEASUREITEM: {
      auto* measure = reinterpret_cast<MEASUREITEMSTRUCT*>(l);
      if (measure && measure->CtlType == ODT_COMBOBOX) {
        // An owner-drawn combo asks once and uses the answer for the closed
        // control and every row; without it the rows collapse to nothing.
        measure->itemHeight = 20;
        return TRUE;
      }
      if (measure && measure->CtlID == IDC_PLUGINS) {
        // From the font rather than a constant, so the rows are still readable
        // when the system text size is turned up.
        HDC dc = GetDC(g.window);
        HGDIOBJ previous = SelectObject(dc, g.font);
        TEXTMETRICW metrics = {};
        GetTextMetricsW(dc, &metrics);
        SelectObject(dc, previous);
        ReleaseDC(g.window, dc);
        const int text = static_cast<int>(metrics.tmHeight) + 8;
        measure->itemHeight = static_cast<UINT>(text < 20 ? 20 : text);
      }
      return TRUE;
    }
    case WM_DRAWITEM: {
      const auto* item = reinterpret_cast<const DRAWITEMSTRUCT*>(l);
      if (item && item->CtlID == IDC_PLUGINS) {
        DrawPluginRow(item);
      } else if (item && item->CtlID == IDC_PROGRESS) {
        DrawThemedProgress(item->hDC, item->rcItem, g.progress_permille);
      } else if (item && item->CtlType == ODT_COMBOBOX) {
        DrawThemedCombo(item);
      } else if (item && item->CtlType == ODT_BUTTON) {
        DrawThemedButton(item, item->CtlID == IDC_PLAY);
      }
      return TRUE;
    }
    case WM_UL_PROGRESS: {
      const int permille = static_cast<int>(static_cast<intptr_t>(w));
      if (permille >= 0) {
        g.progress_permille = permille;
        if (ThemeEnabled()) {
          InvalidateRect(g.progress, nullptr, FALSE);
        } else {
          SendMessageW(g.progress, PBM_SETPOS, static_cast<WPARAM>(permille), 0);
        }
      }
      std::wstring text;
      {
        std::lock_guard<std::mutex> held(g.status_lock);
        text = g.status_text;
      }
      if (!text.empty()) ShowStatus(text);
      return 0;
    }
    case WM_UL_THUMBS:
      // A thumbnail landed. Rebuilding the details is what picks it up, and it
      // is cheap enough to do per arrival — there are never many.
      ShowPluginDetails();
      return 0;
    case WM_UL_FINISHED:
      OnFinished(static_cast<int>(static_cast<intptr_t>(w)),
                 static_cast<Job>(static_cast<intptr_t>(l)));
      return 0;
    case WM_UL_PLAYER_READY:
      // The engine is up. Whatever video the gallery has been showing since
      // before the boot finished can finally be loaded behind the scenes.
      PrimeCurrentVideo();
      return 0;
    case WM_COMMAND:
      switch (LOWORD(w)) {
        case IDC_ACTION:        OnAction(); return 0;
        case IDC_PLAY:          OnPlay(); return 0;
        case IDC_CHANGELOG:     OnChangelog(); return 0;
        case IDC_SETTINGS:      OnSettings(); return 0;
        case IDC_MOD_LINK:      OnOpenModPage(); return 0;
        case IDC_SHOT:
          // The gallery reports that a video was clicked. It plays where the
          // screenshot is; a browser is the fallback for a machine whose
          // WebView2 runtime is missing, which is what the click used to do
          // always.
          if (HIWORD(w) == kSlideshowPlayVideo) {
            const std::string id = SlideshowVideoId(g.shot);
            const std::string list = SlideshowVideoListId(g.shot);
            if ((!id.empty() || !list.empty()) && !VideoPlayerFailed()) {
              ShowVideo(id, list);
            } else {
              OpenInBrowser(SlideshowVideoUrl(g.shot));
            }
          } else if (HIWORD(w) == kSlideshowStepped) {
            PrimeCurrentVideo();
          }
          return 0;
        // Stepping off the video stops it: the next item is a different thing
        // to look at, and the player would otherwise sit over the top of it.
        case IDC_SHOT_PREV:
          HideVideo(); StepSlideshow(g.shot, -1); PrimeCurrentVideo(); return 0;
        case IDC_SHOT_NEXT:
          HideVideo(); StepSlideshow(g.shot, 1); PrimeCurrentVideo(); return 0;
        case IDC_PLUGINS:
          if (HIWORD(w) == LBN_SELCHANGE) {
            ShowPluginDetails();
            ApplySelection();
          }
          // Double-clicking a plugin plays it, which is the only thing
          // double-clicking one could reasonably mean.
          if (HIWORD(w) == LBN_DBLCLK) OnPlay();
          return 0;
        case IDC_VARIANTS:
          if (HIWORD(w) == CBN_SELCHANGE) ApplySelection();
          return 0;
        default:
          return 0;
      }
    case WM_CLOSE:
      g.cancel = true;
      if (g.worker.joinable()) g.worker.join();
      DestroyWindow(window);
      return 0;
    case WM_DESTROY:
      DestroyVideoPlayer();
      PostQuitMessage(0);
      return 0;
    default:
      return DefWindowProcW(window, message, w, l);
  }
}

/// `--version`, for a machine or a bug report.
///
/// A WIN32-subsystem process has no console of its own, which makes this
/// fiddlier than it looks and is why it is written out rather than left to
/// printf. Two cases, and only handling the second is the usual bug:
///
///   Redirected — `UniLoader.exe --version > file`, or a pipe. The process
///   already has a real standard output handle and the bytes must go there.
///
///   Not redirected — run from a shell. There is no handle, so the parent's
///   console is borrowed and CONOUT$ opened on it. Writing to CONOUT$ in the
///   *first* case would put the text on the screen and nothing in the pipe,
///   which is exactly how this flag comes to look broken to a script.
bool HandleConsoleFlags(const std::wstring& command_line) {
  if (command_line.find(L"--version") == std::wstring::npos) return false;

  char text[256];
  const int length = _snprintf_s(text, sizeof(text), _TRUNCATE,
                                 "UniLoader %s\ncore %s\n", UL_APP_VERSION_STR,
                                 ul_version());
  if (length <= 0) return true;

  HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
  bool borrowed = false;
  if (out == nullptr || out == INVALID_HANDLE_VALUE) {
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) return true;
    borrowed = true;
    out = CreateFileW(L"CONOUT$", GENERIC_WRITE, FILE_SHARE_WRITE, nullptr,
                      OPEN_EXISTING, 0, nullptr);
  }
  if (out != INVALID_HANDLE_VALUE && out != nullptr) {
    DWORD written = 0;
    WriteFile(out, text, static_cast<DWORD>(length), &written, nullptr);
    if (borrowed) CloseHandle(out);
  }
  if (borrowed) FreeConsole();
  return true;
}

std::wstring Argument(const std::wstring& command_line, const std::wstring& name) {
  const size_t at = command_line.find(name);
  if (at == std::wstring::npos) return {};
  size_t start = at + name.size();
  while (start < command_line.size() && command_line[start] == L' ') ++start;
  if (start >= command_line.size()) return {};
  if (command_line[start] == L'"') {
    const size_t end = command_line.find(L'"', start + 1);
    if (end == std::wstring::npos) return {};
    return command_line.substr(start + 1, end - start - 1);
  }
  const size_t end = command_line.find(L' ', start);
  return command_line.substr(start, end == std::wstring::npos ? end : end - start);
}

}  // namespace
}  // namespace ulwin

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR command_line, int show) {
  using namespace ulwin;
  const std::wstring arguments = command_line ? command_line : L"";
  if (HandleConsoleFlags(arguments)) return 0;

  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
  Gdiplus::GdiplusStartupInput gdiplus_input;
  ULONG_PTR gdiplus_token = 0;
  Gdiplus::GdiplusStartup(&gdiplus_token, &gdiplus_input, nullptr);
  INITCOMMONCONTROLSEX controls = {sizeof(controls), ICC_PROGRESS_CLASS |
                                                          ICC_STANDARD_CLASSES};
  InitCommonControlsEx(&controls);

  // Settled before anything is built: owner-draw is a creation-time style, so
  // the answer has to be known before the class is registered and the controls
  // are made. This is why the switch in Settings takes effect on the next run.
  SetThemeEnabled(ThemeWanted());
  // Before the class is registered: the class background brush is the theme's.
  CreateTheme();

  WNDCLASSEXW definition = {};
  definition.cbSize = sizeof(definition);
  definition.lpfnWndProc = WindowProc;
  definition.hInstance = instance;
  definition.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APP));
  definition.hIconSm = definition.hIcon;
  definition.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  definition.hbrBackground = ThemeBackgroundBrush();
  // Repaint the whole client area when the window changes size. Without it the
  // background left behind by a control that moved keeps whatever was drawn
  // there, and a drag-resize smears the old layout across the new one.
  definition.style = CS_HREDRAW | CS_VREDRAW;
  definition.lpszClassName = L"UniLoaderWindow";
  RegisterClassExW(&definition);

  const std::wstring title = Text(IDS_APP_TITLE) + L" " + UL_APP_VERSION_WSTR;
  g.window = CreateWindowExW(0, definition.lpszClassName, title.c_str(),
                             WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT,
                             CW_USEDEFAULT, 1010,
                             874, nullptr, nullptr, instance, nullptr);
  if (!g.window) return 1;

  Create();

  // Where the game is: what the elevated relaunch was told, else what was
  // remembered, else whatever the search finds.
  const std::wstring asked = Argument(arguments, L"--game");
  if (!asked.empty()) {
    // Named explicitly: use it, or use nothing. Falling back to the search when
    // the named folder is not a game folder is how a mistyped path silently
    // becomes a write to whichever War2Combat happens to be installed — which
    // is exactly the wrong folder to be surprising about.
    g.game_folder = IsGameFolder(asked) ? asked : std::wstring();
  } else {
    const std::vector<std::wstring> found = FindGameFolders();
    if (!found.empty()) g.game_folder = found.front();
  }
  if (!g.game_folder.empty()) RememberGameFolder(g.game_folder);

  g.store = StoreFolder();
  EnsureFolder(g.store);
  LoadReceipt();
  ReadCatalogue();
  FillPluginList();
  FetchThumbnails();
  Refresh();
  Layout();

  const std::wstring resume = Argument(arguments, L"--resume");
  if (resume == L"install") g.resume = Job::Install;
  if (resume == L"uninstall") g.resume = Job::Uninstall;

  ShowWindow(g.window, show);
  UpdateWindow(g.window);

  // Checked on startup rather than on a button: the whole point of the program
  // is that a user finds out there is an update without going to look.
  Start(Job::Check);

  MSG message;
  while (GetMessageW(&message, nullptr, 0, 0) > 0) {
    if (IsDialogMessageW(g.window, &message)) continue;
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }

  if (g.thumbs.joinable()) g.thumbs.join();
  if (g.release) ul_release_free(g.release);
  if (g.catalogue) ul_catalogue_free(g.catalogue);
  if (g.font) DeleteObject(g.font);
  if (g.bold_font) DeleteObject(g.bold_font);
  DestroyTheme();
  Gdiplus::GdiplusShutdown(gdiplus_token);
  CoUninitialize();
  return 0;
}

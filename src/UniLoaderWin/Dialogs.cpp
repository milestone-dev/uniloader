#include "Dialogs.hpp"

#include <commctrl.h>   // NM_CUSTOMDRAW, for the themed checkbox

#include "Strings.hpp"
#include "Theme.hpp"
#include "resource.h"
#include "strings.h"
#include "version.h"

#include <uniloader/uniloader.h>

#include <vector>

namespace ulwin {
namespace {

constexpr wchar_t kClassName[] = L"UniLoaderDialog";

/// What a running modal window is doing. One per window, reached through
/// GWLP_USERDATA, and never shared — these windows are modal, so there is only
/// ever one alive.
struct Modal {
  bool finished = false;
  DialogAction action = DialogAction::None;
  HFONT font = nullptr;
  HFONT heading_font = nullptr;
  enum class Kind { Text, Settings } kind = Kind::Text;
  /// The caller's display settings, borrowed for the life of the window. The
  /// window reads them to fill its controls and writes back what was chosen;
  /// the host owns the file and the decision to save.
  DisplaySettings* display = nullptr;
};

Modal* Of(HWND window) {
  return reinterpret_cast<Modal*>(GetWindowLongPtrW(window, GWLP_USERDATA));
}

/// Declared early: the window procedure reacts to the display controls well
/// above where reading them is written.
void ReadDisplay(HWND window);

void Finish(HWND window, DialogAction action) {
  Modal* modal = Of(window);
  if (!modal) return;
  modal->action = action;
  modal->finished = true;
}

/// The settings window's controls, laid out down the page. Rearranged on resize
/// as well as on creation, because the folder paths in it are long and being
/// able to widen the window is the difference between reading one and not.
void LayoutSettings(HWND window) {
  RECT client;
  GetClientRect(window, &client);
  const int margin = 16;
  const int width = client.right - 2 * margin;
  int y = margin;

  auto place = [&](int id, int height, int gap) {
    HWND child = GetDlgItem(window, id);
    if (child) MoveWindow(child, margin, y, width, height, TRUE);
    y += height + gap;
  };

  place(IDC_DLG_DISPLAY, 20, 8);
  HWND mode_label = GetDlgItem(window, IDC_DLG_MODE_LABEL);
  HWND mode = GetDlgItem(window, IDC_DLG_MODE);
  if (mode_label) MoveWindow(mode_label, margin, y + 4, 90, 18, TRUE);
  if (mode) MoveWindow(mode, margin + 96, y, width - 96, 240, TRUE);
  y += 26 + 8;
  HWND shader_label = GetDlgItem(window, IDC_DLG_SHADER_LABEL);
  HWND shader = GetDlgItem(window, IDC_DLG_SHADER);
  if (shader_label) MoveWindow(shader_label, margin, y + 4, 90, 18, TRUE);
  if (shader) MoveWindow(shader, margin + 96, y, width - 96, 240, TRUE);
  y += 26 + 8;
  place(IDC_DLG_ASPECT, 22, 22);

  place(IDC_DLG_FOLDER, 18, 2);
  place(IDC_DLG_FOLDER_PATH, 18, 8);
  HWND change = GetDlgItem(window, IDC_DLG_CHANGE_FOLDER);
  if (change) MoveWindow(change, margin, y, 160, 28, TRUE);
  y += 28 + 20;

  place(IDC_DLG_STORE, 18, 2);
  place(IDC_DLG_STORE_PATH, 18, 16);
  HWND uninstall = GetDlgItem(window, IDC_DLG_UNINSTALL);
  if (uninstall) MoveWindow(uninstall, margin, y, 200, 30, TRUE);
  y += 30 + 18;

  place(IDC_DLG_ABOUT, 22, 16);

  // Usually not there at all, and then it takes no room either — leaving the
  // gap behind would put a hole above Close that nothing explains.
  HWND clear = GetDlgItem(window, IDC_DLG_CLEAR_CACHE);
  if (clear) {
    place(IDC_DLG_CACHE, 18, 8);
    MoveWindow(clear, margin, y, 260, 30, TRUE);
    y += 30 + 8;
  }

  HWND close = GetDlgItem(window, IDC_DLG_CLOSE);
  if (close) {
    MoveWindow(close, client.right - margin - 100, client.bottom - margin - 28, 100, 28,
               TRUE);
  }
}

/// The changelog window: one big read-only box and a Close button.
void LayoutText(HWND window) {
  RECT client;
  GetClientRect(window, &client);
  const int margin = 12;
  const int button = 28;
  HWND text = GetDlgItem(window, IDC_DLG_TEXT);
  if (text) {
    MoveWindow(text, margin, margin, client.right - 2 * margin,
               client.bottom - 2 * margin - button - 10, TRUE);
  }
  HWND close = GetDlgItem(window, IDC_DLG_CLOSE);
  if (close) {
    MoveWindow(close, client.right - margin - 100, client.bottom - margin - button, 100,
               button, TRUE);
  }
}

LRESULT CALLBACK Proc(HWND window, UINT message, WPARAM w, LPARAM l) {
  switch (message) {
    case WM_SIZE: {
      Modal* modal = Of(window);
      if (modal) {
        switch (modal->kind) {
          case Modal::Kind::Settings: LayoutSettings(window); break;
          case Modal::Kind::Text:     LayoutText(window); break;
        }
      }
      return 0;
    }
    case WM_COMMAND:
      switch (LOWORD(w)) {
        case IDC_DLG_CLOSE:          Finish(window, DialogAction::None); return 0;
        case IDC_DLG_CHANGE_FOLDER:  Finish(window, DialogAction::ChangeFolder); return 0;
        case IDC_DLG_UNINSTALL:      Finish(window, DialogAction::Uninstall); return 0;
        case IDC_DLG_CLEAR_CACHE:    Finish(window, DialogAction::ClearCache); return 0;
        case IDC_DLG_MODE:
        case IDC_DLG_SHADER:
          if (HIWORD(w) == CBN_SELCHANGE) ReadDisplay(window);
          return 0;
        case IDC_DLG_ASPECT:
          if (HIWORD(w) == BN_CLICKED) ReadDisplay(window);
          return 0;
        default: return 0;
      }
    case WM_DRAWITEM: {
      const auto* item = reinterpret_cast<const DRAWITEMSTRUCT*>(l);
      if (item && item->CtlType == ODT_BUTTON) DrawThemedButton(item, false);
      return TRUE;
    }
    case WM_CTLCOLORSTATIC: {
      // Ink on parchment for the labels; the changelog's read-only body gets
      // the panel colour, the way the main window's description does.
      HDC dc = reinterpret_cast<HDC>(w);
      SetTextColor(dc, ThemeInk());
      if (GetDlgCtrlID(reinterpret_cast<HWND>(l)) == IDC_DLG_TEXT) {
        SetBkColor(dc, ThemePanel());
        return reinterpret_cast<LRESULT>(ThemePanelBrush());
      }
      SetBkMode(dc, TRANSPARENT);
      return reinterpret_cast<LRESULT>(ThemeBackgroundBrush());
    }
    case WM_NOTIFY: {
      // The one checkbox, custom-drawn in the game's preferences style. The
      // control still owns its check state — this only paints it.
      const auto* header = reinterpret_cast<const NMHDR*>(l);
      if (header && header->idFrom == IDC_DLG_ASPECT &&
          header->code == NM_CUSTOMDRAW) {
        auto* draw = reinterpret_cast<NMCUSTOMDRAW*>(l);
        if (draw->dwDrawStage == CDDS_PREERASE ||
            draw->dwDrawStage == CDDS_PREPAINT) {
          FillRect(draw->hdc, &draw->rc, ThemeBackgroundBrush());
          wchar_t label[256] = {};
          GetWindowTextW(header->hwndFrom, label, 256);
          const bool checked =
              SendMessageW(header->hwndFrom, BM_GETCHECK, 0, 0) == BST_CHECKED;
          Modal* modal = Of(window);
          DrawThemedCheckbox(draw->hdc, draw->rc, label, checked,
                             modal ? modal->font : nullptr);
          return CDRF_SKIPDEFAULT;
        }
      }
      return DefWindowProcW(window, message, w, l);
    }
    case WM_CLOSE:
      Finish(window, DialogAction::None);
      return 0;
    default:
      return DefWindowProcW(window, message, w, l);
  }
}

/// Copies what the three display controls say back into the caller's struct.
///
/// Read on every change rather than once at the end, so that closing the window
/// with the X saves what was chosen just as OK would have. There is no OK: this
/// window has no wrong state to abandon.
void ReadDisplay(HWND window) {
  Modal* modal = Of(window);
  if (!modal || !modal->display || !modal->display->available) return;
  DisplaySettings& display = *modal->display;

  const LRESULT mode = SendMessageW(GetDlgItem(window, IDC_DLG_MODE), CB_GETCURSEL, 0, 0);
  if (mode != CB_ERR) display.mode = static_cast<int>(mode);
  const LRESULT shader =
      SendMessageW(GetDlgItem(window, IDC_DLG_SHADER), CB_GETCURSEL, 0, 0);
  if (shader != CB_ERR) display.shader = static_cast<int>(shader);
  display.keep_aspect =
      SendMessageW(GetDlgItem(window, IDC_DLG_ASPECT), BM_GETCHECK, 0, 0) == BST_CHECKED;
  display.changed = true;
}

void Register() {
  static bool done = false;
  if (done) return;
  WNDCLASSEXW definition = {};
  definition.cbSize = sizeof(definition);
  definition.lpfnWndProc = Proc;
  definition.hInstance = GetModuleHandleW(nullptr);
  definition.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  // The same parchment the main window stands on.
  definition.hbrBackground = ThemeBackgroundBrush();
  // The stock application icon while the custom one is off — see UniLoader.rc.
  definition.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
  definition.lpszClassName = kClassName;
  RegisterClassExW(&definition);
  done = true;
}

HFONT MessageFont() {
  NONCLIENTMETRICSW metrics = {};
  metrics.cbSize = sizeof(metrics);
  SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0);
  return CreateFontIndirectW(&metrics.lfMessageFont);
}

HWND Child(HWND parent, const wchar_t* type, const std::wstring& text, DWORD style,
           int id, HFONT font) {
  HWND child = CreateWindowExW(0, type, text.c_str(), WS_CHILD | WS_VISIBLE | style, 0, 0,
                               10, 10, parent,
                               reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                               GetModuleHandleW(nullptr), nullptr);
  SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
  return child;
}

/// Runs the window until it says it is finished.
///
/// The owner is disabled for the duration rather than a system modal loop being
/// used, which is what makes this behave like a dialog without being one. It is
/// re-enabled before the window is destroyed: doing it the other way round lets
/// Windows activate some other application on the way past, and the main window
/// comes back from behind whatever that was.
DialogAction RunModal(HWND owner, HWND window, Modal& modal) {
  EnableWindow(owner, FALSE);
  ShowWindow(window, SW_SHOW);
  UpdateWindow(window);

  MSG message;
  while (!modal.finished && GetMessageW(&message, nullptr, 0, 0) > 0) {
    if (IsDialogMessageW(window, &message)) continue;
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }

  EnableWindow(owner, TRUE);
  SetActiveWindow(owner);
  DestroyWindow(window);
  if (modal.font) DeleteObject(modal.font);
  if (modal.heading_font) DeleteObject(modal.heading_font);
  return modal.action;
}

/// Centred on the owner, and pulled back onto the screen if that would put it
/// off the edge — which happens when the main window has been dragged to the
/// bottom of a small display.
void CentreOn(HWND owner, HWND window, int width, int height) {
  RECT parent;
  GetWindowRect(owner, &parent);
  int x = parent.left + ((parent.right - parent.left) - width) / 2;
  int y = parent.top + ((parent.bottom - parent.top) - height) / 2;

  RECT work = {};
  if (SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0)) {
    if (x + width > work.right) x = work.right - width;
    if (y + height > work.bottom) y = work.bottom - height;
    if (x < work.left) x = work.left;
    if (y < work.top) y = work.top;
  }
  SetWindowPos(window, nullptr, x, y, width, height, SWP_NOZORDER);
}

/// CRLF, because an EDIT control draws a bare LF as a box.
std::wstring ForEditControl(const std::wstring& text) {
  std::wstring out;
  out.reserve(text.size() + text.size() / 8);
  for (const wchar_t c : text) {
    if (c == L'\n') out.push_back(L'\r');
    out.push_back(c);
  }
  return out;
}

}  // namespace

void ShowChangelog(HWND owner, const ul_release* release) {
  Register();
  Modal modal;
  modal.font = MessageFont();

  std::wstring title = Text(IDS_CHANGELOG);
  std::wstring body;
  if (release) {
    const std::wstring name = FromUtf8(ul_release_mod_name(release));
    if (!name.empty()) title = Format(IDS_CHANGELOG_TITLE, name.c_str());
    // Everything, not notes_since: this is the whole mod's history, which is a
    // different question from "what have I missed" and is why it has its own
    // button rather than sitting on the main window.
    char* notes = ul_release_notes_since(release, "");
    body = FromUtf8(notes ? notes : "");
    ul_free(notes);
  }
  if (body.empty()) body = Text(IDS_CHANGELOG_EMPTY);

  HWND window = CreateWindowExW(
      WS_EX_DLGMODALFRAME, kClassName, title.c_str(),
      WS_POPUPWINDOW | WS_CAPTION | WS_THICKFRAME | WS_CLIPCHILDREN, CW_USEDEFAULT,
      CW_USEDEFAULT, 640, 560, owner, nullptr, GetModuleHandleW(nullptr), nullptr);
  if (!window) {
    DeleteObject(modal.font);
    return;
  }
  SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&modal));

  Child(window, L"EDIT", ForEditControl(body),
        ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | WS_BORDER |
            WS_TABSTOP,
        IDC_DLG_TEXT, modal.font);
  Child(window, L"BUTTON", Text(IDS_CLOSE), BS_OWNERDRAW | WS_TABSTOP,
        IDC_DLG_CLOSE, modal.font);

  CentreOn(owner, window, 640, 560);
  LayoutText(window);
  RunModal(owner, window, modal);
}

DialogAction ShowSettings(HWND owner, const std::wstring& game_folder,
                          const std::wstring& store_folder,
                          const std::wstring& installed_version,
                          const std::wstring& cache_size, bool busy,
                          DisplaySettings& display) {
  Register();
  Modal modal;
  modal.kind = Modal::Kind::Settings;
  modal.display = &display;
  modal.font = MessageFont();
  {
    NONCLIENTMETRICSW metrics = {};
    metrics.cbSize = sizeof(metrics);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0);
    metrics.lfMessageFont.lfWeight = FW_SEMIBOLD;
    modal.heading_font = CreateFontIndirectW(&metrics.lfMessageFont);
  }

  HWND window = CreateWindowExW(
      WS_EX_DLGMODALFRAME, kClassName, Text(IDS_SETTINGS_TITLE).c_str(),
      WS_POPUPWINDOW | WS_CAPTION | WS_THICKFRAME | WS_CLIPCHILDREN, CW_USEDEFAULT,
      CW_USEDEFAULT, 560, 590, owner, nullptr, GetModuleHandleW(nullptr), nullptr);
  if (!window) {
    DeleteObject(modal.font);
    return DialogAction::None;
  }
  SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&modal));

  // The sentence and the path it introduces are two controls, not one string
  // with a break in it. A static carrying SS_PATHELLIPSIS is single-line, and
  // it does not merely ignore the "\r\n" — it drops it, so the label ran
  // straight into the path with no space between them.
  Child(window, L"STATIC", Text(IDS_SETTINGS_FOLDER), SS_LEFT, IDC_DLG_FOLDER,
        modal.font);
  Child(window, L"STATIC", game_folder.empty() ? Text(IDS_ERR_NO_GAME) : game_folder,
        SS_LEFT | SS_PATHELLIPSIS, IDC_DLG_FOLDER_PATH, modal.font);
  Child(window, L"BUTTON", Text(IDS_CHANGE_FOLDER), BS_OWNERDRAW | WS_TABSTOP,
        IDC_DLG_CHANGE_FOLDER, modal.font);

  Child(window, L"STATIC", Text(IDS_SETTINGS_STORE), SS_LEFT, IDC_DLG_STORE, modal.font);
  Child(window, L"STATIC", store_folder, SS_LEFT | SS_PATHELLIPSIS, IDC_DLG_STORE_PATH,
        modal.font);

  const std::wstring installed =
      installed_version.empty()
          ? Text(IDS_SETTINGS_NOTHING)
          : Format(IDS_SETTINGS_INSTALLED, installed_version.c_str());
  HWND remove = Child(window, L"BUTTON", Text(IDS_UNINSTALL) + L"…",
                      BS_OWNERDRAW | WS_TABSTOP, IDC_DLG_UNINSTALL, modal.font);
  // Nothing installed, or something already running: there is nothing to undo,
  // and a button that would start a second job while the first is going is a
  // button that should not be pressable.
  EnableWindow(remove, (!installed_version.empty() && !busy) ? TRUE : FALSE);

  // --- the display, first, because it is what somebody opens this to change --
  Child(window, L"STATIC", Text(IDS_DISPLAY), SS_LEFT, IDC_DLG_DISPLAY,
        modal.heading_font ? modal.heading_font : modal.font);
  Child(window, L"STATIC", Text(IDS_DISPLAY_MODE), SS_LEFT | SS_CENTERIMAGE,
        IDC_DLG_MODE_LABEL, modal.font);
  HWND mode = Child(window, L"COMBOBOX", L"",
                    CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, IDC_DLG_MODE,
                    modal.font);
  for (UINT label : {IDS_MODE_FULLSCREEN, IDS_MODE_BORDERLESS, IDS_MODE_WINDOWED}) {
    SendMessageW(mode, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(Text(label).c_str()));
  }
  SendMessageW(mode, CB_SETCURSEL, static_cast<WPARAM>(display.mode), 0);

  Child(window, L"STATIC", Text(IDS_SMOOTHING), SS_LEFT | SS_CENTERIMAGE,
        IDC_DLG_SHADER_LABEL, modal.font);
  HWND shader = Child(window, L"COMBOBOX", L"",
                      CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, IDC_DLG_SHADER,
                      modal.font);
  for (size_t i = 0; i < display.shaders.size(); ++i) {
    // The first entry is the empty string, which is "no filter". The rest are
    // whatever .glsl files the game folder happens to hold — listed by name
    // rather than through a table of friendly labels, so a shader added to
    // War2Combat next year turns up here without a code change.
    const std::wstring& file = display.shaders[i];
    SendMessageW(shader, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(file.empty() ? Text(IDS_SMOOTHING_NONE).c_str()
                                                       : file.c_str()));
  }
  SendMessageW(shader, CB_SETCURSEL, static_cast<WPARAM>(display.shader), 0);

  HWND aspect = Child(window, L"BUTTON", Text(IDS_KEEP_ASPECT),
                      BS_AUTOCHECKBOX | WS_TABSTOP, IDC_DLG_ASPECT, modal.font);
  SendMessageW(aspect, BM_SETCHECK,
               display.keep_aspect ? BST_CHECKED : BST_UNCHECKED, 0);

  // Nothing to change when the config file was not found. Shown rather than
  // hidden, so the absence is explained instead of leaving a gap.
  if (!display.available) {
    SetWindowTextW(GetDlgItem(window, IDC_DLG_DISPLAY),
                   Text(IDS_NO_DISPLAY_CONFIG).c_str());
    for (int id : {IDC_DLG_MODE, IDC_DLG_SHADER, IDC_DLG_ASPECT, IDC_DLG_MODE_LABEL,
                   IDC_DLG_SHADER_LABEL}) {
      ShowWindow(GetDlgItem(window, id), SW_HIDE);
    }
  }

  // Only what is installed. The versions and the credits are About's.
  Child(window, L"STATIC", installed, SS_LEFT, IDC_DLG_ABOUT, modal.font);

  // A leftover download, when there is one. `cache_size` counts only packages
  // nothing is using — never the installed version, whose folder holds the
  // plugins — so most of the time there is nothing here to say and the row is
  // not built at all. A disabled button explaining that a thing is absent is
  // worse than the absence.
  if (!cache_size.empty()) {
    Child(window, L"STATIC", Text(IDS_SETTINGS_CACHE), SS_LEFT, IDC_DLG_CACHE, modal.font);
    HWND clear = Child(window, L"BUTTON", Format(IDS_CLEAR_CACHE, cache_size.c_str()),
                       BS_OWNERDRAW | WS_TABSTOP, IDC_DLG_CLEAR_CACHE, modal.font);
    EnableWindow(clear, busy ? FALSE : TRUE);
  }

  Child(window, L"BUTTON", Text(IDS_CLOSE), BS_OWNERDRAW | WS_TABSTOP,
        IDC_DLG_CLOSE, modal.font);

  CentreOn(owner, window, 560, cache_size.empty() ? 496 : 552);
  LayoutSettings(window);
  return RunModal(owner, window, modal);
}

}  // namespace ulwin

// Resource and control ids.
//
// Retired ids stay retired. An id that stops being used is left here unused
// with a comment, never reassigned, so a stale template cannot land on the
// wrong control. String ids live in strings.h.

#pragma once

#define IDI_APP                 1
#define IDR_MANIFEST            1
// 2 was IDR_BTN and 3 IDR_BTN_PRESSED, the button plaque as its own two files.
// Retired with btn.png and btn_pressed.png: the plaque is sliced off the sheet.
// 4 was IDR_FONT, an embedded lettering face. Retired with the font.
#define IDR_SHEET               5   // the artist's sprite sheet — see Theme.cpp

// --------------------------------------------------------------- the window

#define IDC_STATUS            100   // the one line that says what is going on
#define IDC_PROGRESS          101
#define IDC_ACTION            102   // Install / Update / Cancel — one button, three jobs
#define IDC_PLAY              103
#define IDC_UNINSTALL         104
#define IDC_PLUGINS           105   // the list, with "No plugin" as its first row
// 106 was IDC_APPLY, the "Use this plugin" button. The checkbox in the list
// does that job now, so the button is gone and the id stays retired.
#define IDC_DESCRIPTION       107
#define IDC_SHOT              108   // the slideshow panel
#define IDC_SHOT_PREV         109
#define IDC_SHOT_NEXT         110
#define IDC_MOD_LINK          111   // the Mod page button, out to the GameBanana post
// 112 was IDC_AUTHOR_LINK, out to dannyldd's profile. It went with the About
// window; the profile is one click from the mod page.
// 113 was IDC_HEADLINE. Retired: the mod-name headline came off the window —
// the title bar, the status line and About already say what it said.
#define IDC_LATEST            114   // the version on offer, beside the button
#define IDC_CHANGELOG         115   // opens the whole mod's release notes
#define IDC_SETTINGS          116   // opens the folder, the store and uninstall
// 117 was IDC_ABOUT, the button that opened the About window. Both retired.
#define IDC_VARIANTS          118   // which of a plugin's .w2p files to load
// 119 was IDC_ACTIVATE. Play activates whatever is selected, so there is
// nothing left for a second button to do. The id stays retired.

// ------------------------------------------------------------- the dialogs
//
// Own ids rather than a second range: a dialog here is a plain popup window
// built by hand like the main one, so its children live in the same namespace.

#define IDC_DLG_TEXT          200   // the read-only body of the changelog window
#define IDC_DLG_CLOSE         201
#define IDC_DLG_FOLDER        202   // which War2Combat this is pointed at
#define IDC_DLG_CHANGE_FOLDER 203
#define IDC_DLG_UNINSTALL     204
#define IDC_DLG_STORE         205   // where UniLoader keeps its own files
#define IDC_DLG_ABOUT         206
// A path and the sentence introducing it are two controls, not one: a static
// with SS_PATHELLIPSIS is single-line and eats the break between them.
#define IDC_DLG_FOLDER_PATH   207
#define IDC_DLG_STORE_PATH    208
// 209-211 were IDC_DLG_MOD_PAGE, IDC_DLG_AUTHOR_PAGE and IDC_DLG_HEADING, the
// About window's children. Retired with the window; Mod page is a main-window
// button now (IDC_MOD_LINK).
#define IDC_DLG_CACHE         212   // what the kept download is costing
#define IDC_DLG_CLEAR_CACHE   213

// The game's own display options, read out of and written back into the INI
// beside War2Combat. Not the mod's settings, and not UniLoader's.
#define IDC_DLG_DISPLAY       214   // the "Display" heading
#define IDC_DLG_MODE          215   // fullscreen / borderless / windowed
#define IDC_DLG_ASPECT        216   // keep the 4:3 shape
#define IDC_DLG_SHADER        217   // the scaling filter
#define IDC_DLG_SHADER_LABEL  218
#define IDC_DLG_MODE_LABEL    219
// 220 was IDC_DLG_GAME_OPTIONS, which launched Warcraft II Config.exe. Retired:
// that tool is cnc-ddraw's own, and its Display Mode, OpenGL Filter and Maintain
// Aspect Ratio are the three settings this window already has, writing the same
// ddraw.ini. Two ways to set one thing, and one of them not ours to fix.

// ------------------------------------------------------------- the messages
//
// Below WM_USER on purpose where a value is read across a thread boundary; the
// two here are posted, never sent, and carry no pointer that outlives the post.

#define WM_UL_PROGRESS   (WM_APP + 1)   // wParam: permille, lParam: unused
#define WM_UL_FINISHED   (WM_APP + 2)   // wParam: a UL_* code, lParam: the job
#define WM_UL_THUMBS     (WM_APP + 3)   // a video thumbnail arrived; redraw the gallery
#define WM_UL_PLAYER_READY (WM_APP + 4) // WebView2 came up; prime the visible video

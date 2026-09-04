// The Warcraft II Combat Edition look, for the window that manages it.
//
// The reference is the game's own menu: an aged-parchment ground, deep maroon
// buttons framed in black with a metal bevel, and gold serif lettering. The
// parchment is generated at startup rather than shipped as an image — a small
// tile of layered noise, turned into a pattern brush — so the look costs no
// asset pipeline and no bytes in the exe beyond the code that mixes it.

#pragma once

#include <windows.h>

namespace ulwin {

/// Builds the brushes and fonts. Called once, before the window class is
/// registered — the class background brush comes from here.
void CreateTheme();
void DestroyTheme();

/// Whether the painted theme is in use at all. Off puts the program back in
/// the plain Windows look: no owner-drawing, no parchment, system colours.
///
/// Read wherever a control is created, so it must be settled before the window
/// is built — owner-draw is a creation-time style and cannot be turned on or
/// off under a control that already exists. That is why the switch in Settings
/// takes effect when the program next starts.
bool ThemeEnabled();
void SetThemeEnabled(bool on);

/// `bits` when the theme is on, nothing when it is off. For the owner-draw
/// style flags, which are the difference between a themed control and a
/// stock one: `ThemedStyle(BS_OWNERDRAW)`.
DWORD ThemedStyle(DWORD bits);

/// The parchment, as a tiling pattern brush. The window class background, and
/// what the statics hand back from WM_CTLCOLORSTATIC.
HBRUSH ThemeBackgroundBrush();
/// A lighter, solid parchment for the boxes that hold text: the plugin list
/// and the description.
HBRUSH ThemePanelBrush();

COLORREF ThemeInk();        // near-black brown, the reading colour on parchment
COLORREF ThemeInkFaint();   // the version label and other second voices
COLORREF ThemePanel();      // the solid colour behind ThemePanelBrush
COLORREF ThemeMaroon();     // the buttons, and the list's selection
COLORREF ThemeGold();       // lettering on maroon
COLORREF ThemeLink();       // the art's red, for links in the description

/// The serif faces the buttons letter in. The large one is Play's.
HFONT ThemeButtonFont();
HFONT ThemePlayFont();

/// Paints one owner-drawn button in the menu style: black frame, metal bevel,
/// maroon fill, gold text — pressed sinks it, focus outlines it in gold, and
/// disabled dims the lettering. `large` is Play. An artist's nine-sliced
/// plaque (btn.png, embedded; art\button.9.png beside the exe overrides)
/// replaces the painting when present.
void DrawThemedButton(const DRAWITEMSTRUCT* item, bool large);

/// A scratch surface the size of `box`, copied over the real device context in
/// one go when it falls out of scope.
///
/// Everything here paints in pieces — a nine-patch is nine or more separate
/// draws, a row is a fill and then its text — and every piece that lands on
/// the screen on its own is a frame the eye can catch. Drawing into a bitmap
/// and blitting once is the difference between a control appearing and a
/// control assembling itself.
///
/// Coordinates do not change: the scratch DC is offset so the same absolute
/// rectangle the caller was given still lands in the right place.
class Buffered {
 public:
  Buffered(HDC target, const RECT& box);
  ~Buffered();
  Buffered(const Buffered&) = delete;
  Buffered& operator=(const Buffered&) = delete;

  /// The surface to paint on — the real one if a scratch could not be had, so
  /// a failure here costs the flicker back and nothing else.
  HDC dc() const { return scratch ? scratch : target; }

 private:
  HDC target = nullptr;
  RECT box{};
  HDC scratch = nullptr;
  HBITMAP bitmap = nullptr;
  HGDIOBJ previous = nullptr;
};

/// Paints the button plaque into `box` for controls that are not buttons —
/// the same nine-patch, so a dropdown sits in the same set as the buttons.
void DrawThemedPlaque(HDC dc, const RECT& box, bool pressed);

/// Paints one owner-drawn combo: the closed control wears the plaque with gold
/// lettering, and a row of the open list is parchment, maroon when picked.
/// Needs CBS_OWNERDRAWFIXED | CBS_HASSTRINGS on the control.
void DrawThemedCombo(const DRAWITEMSTRUCT* item);

/// Subclasses a combo so its drop button wears the plaque too. Owner-draw does
/// not reach that button, so it is painted over after the control paints.
void ThemeDropdown(HWND combo);

/// Paints the download bar: a dark well in an ink frame, filled flat red to
/// `permille` of the width.
void DrawThemedProgress(HDC dc, const RECT& box, int permille);

/// Paints a checkbox in the game's preferences style: a small pane with a red
/// cross when checked, the label beside it in ink. Drawn over the whole
/// control through NM_CUSTOMDRAW, so the control keeps its own check state.
/// art\check.png and art\check-on.png beside the exe replace the pane.
void DrawThemedCheckbox(HDC dc, const RECT& box, const wchar_t* label,
                        bool checked, HFONT font);

}  // namespace ulwin

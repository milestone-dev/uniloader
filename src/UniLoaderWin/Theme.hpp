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

/// The serif faces the buttons letter in. The large one is Play's.
HFONT ThemeButtonFont();
HFONT ThemePlayFont();

/// Paints one owner-drawn button in the menu style: black frame, metal bevel,
/// maroon fill, gold text — pressed sinks it, focus outlines it in gold, and
/// disabled dims the lettering. `large` is Play.
void DrawThemedButton(const DRAWITEMSTRUCT* item, bool large);

}  // namespace ulwin

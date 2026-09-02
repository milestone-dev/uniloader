#include "Theme.hpp"

#include <cmath>
#include <cstdint>
#include <string>

namespace ulwin {
namespace {

// The palette, eyedropped from the game's menu screen.
constexpr COLORREF kParchment = RGB(213, 197, 162);   // the ground's average
constexpr COLORREF kPanel = RGB(228, 215, 184);       // boxes that hold text
constexpr COLORREF kInk = RGB(52, 38, 22);
constexpr COLORREF kInkFaint = RGB(122, 104, 78);
constexpr COLORREF kMaroon = RGB(112, 22, 16);
constexpr COLORREF kMaroonPressed = RGB(84, 15, 11);
constexpr COLORREF kGold = RGB(233, 195, 90);
constexpr COLORREF kGoldDim = RGB(150, 126, 78);
constexpr COLORREF kFrame = RGB(12, 10, 8);
constexpr COLORREF kBevelLight = RGB(190, 188, 182);
constexpr COLORREF kBevelDark = RGB(70, 68, 64);

struct Theme {
  HBITMAP parchment = nullptr;
  HBRUSH background = nullptr;
  HBRUSH panel = nullptr;
  HFONT button_font = nullptr;
  HFONT play_font = nullptr;
};

Theme g_theme;

/// A deterministic hash noise, so the parchment is the same parchment on
/// every start rather than a fresh sheet each time.
uint32_t Hash(uint32_t x, uint32_t y) {
  uint32_t h = x * 374761393u + y * 668265263u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return h ^ (h >> 16);
}

/// The parchment tile: the base tan, mottled twice. A slow wash of large
/// blotches gives the sheet its unevenness, and a fine grain of speckles gives
/// it its tooth. Both are subtle on purpose — the game's background reads as
/// paper first and texture second.
HBITMAP MakeParchment(int size) {
  BITMAPINFO info = {};
  info.bmiHeader.biSize = sizeof(info.bmiHeader);
  info.bmiHeader.biWidth = size;
  info.bmiHeader.biHeight = -size;   // top-down
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;

  void* bits = nullptr;
  HBITMAP bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
  if (!bitmap || !bits) return bitmap;

  auto* pixels = static_cast<uint32_t*>(bits);
  const double tau = 6.28318530718;
  for (int y = 0; y < size; ++y) {
    for (int x = 0; x < size; ++x) {
      // Two sine octaves, phase-locked to the tile size so the seams meet.
      const double u = tau * x / size;
      const double v = tau * y / size;
      const double wash = std::sin(u * 2 + std::cos(v)) + std::cos(v * 3 - u) +
                          std::sin(u * 5 + v * 4) * 0.5;
      const int slow = static_cast<int>(wash * 3.5);
      // And the grain: hashed per pixel, biased slightly dark like foxing.
      const int grain = static_cast<int>(Hash(x, y) % 9) - 5;

      auto channel = [&](int base) {
        const int value = base + slow + grain;
        return static_cast<uint32_t>(value < 0 ? 0 : (value > 255 ? 255 : value));
      };
      pixels[y * size + x] = (channel(GetRValue(kParchment)) << 16) |
                             (channel(GetGValue(kParchment)) << 8) |
                             channel(GetBValue(kParchment));
    }
  }
  return bitmap;
}

HFONT MakeSerif(int height, int weight) {
  // Palatino Linotype ships with every Windows this runs on and is the
  // closest stock face to the game's lettering; the fallbacks keep the shape
  // serif if a stripped install lacks it.
  LOGFONTW definition = {};
  definition.lfHeight = -height;
  definition.lfWeight = weight;
  definition.lfCharSet = DEFAULT_CHARSET;
  definition.lfQuality = CLEARTYPE_QUALITY;
  wcscpy_s(definition.lfFaceName, L"Palatino Linotype");
  HFONT font = CreateFontIndirectW(&definition);
  if (!font) {
    wcscpy_s(definition.lfFaceName, L"Georgia");
    font = CreateFontIndirectW(&definition);
  }
  return font;
}

void Line(HDC dc, int x1, int y1, int x2, int y2, COLORREF colour) {
  HPEN pen = CreatePen(PS_SOLID, 1, colour);
  HGDIOBJ previous = SelectObject(dc, pen);
  MoveToEx(dc, x1, y1, nullptr);
  LineTo(dc, x2, y2);
  SelectObject(dc, previous);
  DeleteObject(pen);
}

}  // namespace

void CreateTheme() {
  if (g_theme.background) return;
  g_theme.parchment = MakeParchment(256);
  g_theme.background = g_theme.parchment ? CreatePatternBrush(g_theme.parchment)
                                         : CreateSolidBrush(kParchment);
  g_theme.panel = CreateSolidBrush(kPanel);
  g_theme.button_font = MakeSerif(16, FW_BOLD);
  g_theme.play_font = MakeSerif(22, FW_BOLD);
}

void DestroyTheme() {
  if (g_theme.background) DeleteObject(g_theme.background);
  if (g_theme.panel) DeleteObject(g_theme.panel);
  if (g_theme.parchment) DeleteObject(g_theme.parchment);
  if (g_theme.button_font) DeleteObject(g_theme.button_font);
  if (g_theme.play_font) DeleteObject(g_theme.play_font);
  g_theme = Theme{};
}

HBRUSH ThemeBackgroundBrush() { return g_theme.background; }
HBRUSH ThemePanelBrush() { return g_theme.panel; }
COLORREF ThemeInk() { return kInk; }
COLORREF ThemeInkFaint() { return kInkFaint; }
COLORREF ThemePanel() { return kPanel; }
COLORREF ThemeMaroon() { return kMaroon; }
COLORREF ThemeGold() { return kGold; }
HFONT ThemeButtonFont() { return g_theme.button_font; }
HFONT ThemePlayFont() { return g_theme.play_font; }

void DrawThemedButton(const DRAWITEMSTRUCT* item, bool large) {
  if (!item) return;
  HDC dc = item->hDC;
  RECT box = item->rcItem;
  const bool pressed = (item->itemState & ODS_SELECTED) != 0;
  const bool disabled = (item->itemState & ODS_DISABLED) != 0;
  const bool focused = (item->itemState & ODS_FOCUS) != 0;

  // Black frame first — the game's buttons sit in a hard dark edge.
  HBRUSH frame = CreateSolidBrush(kFrame);
  FillRect(dc, &box, frame);
  DeleteObject(frame);
  InflateRect(&box, -1, -1);

  // The metal bevel: lit from the top-left, and flipped when pressed so the
  // button visibly sinks.
  const COLORREF top = pressed ? kBevelDark : kBevelLight;
  const COLORREF bottom = pressed ? kBevelLight : kBevelDark;
  Line(dc, box.left, box.top, box.right - 1, box.top, top);
  Line(dc, box.left, box.top, box.left, box.bottom - 1, top);
  Line(dc, box.left, box.bottom - 1, box.right, box.bottom - 1, bottom);
  Line(dc, box.right - 1, box.top, box.right - 1, box.bottom, bottom);
  InflateRect(&box, -1, -1);

  HBRUSH fill = CreateSolidBrush(disabled ? kMaroonPressed
                                          : (pressed ? kMaroonPressed : kMaroon));
  FillRect(dc, &box, fill);
  DeleteObject(fill);

  // Focus is the game's own signal: a gold line around the lettering, not the
  // dotted marquee Windows would draw.
  if (focused && !disabled) {
    HBRUSH gold = CreateSolidBrush(kGold);
    FrameRect(dc, &box, gold);
    DeleteObject(gold);
  }

  wchar_t text[256] = {};
  GetWindowTextW(item->hwndItem, text, 256);
  SetBkMode(dc, TRANSPARENT);
  HGDIOBJ previous = SelectObject(dc, large ? g_theme.play_font : g_theme.button_font);
  RECT label = box;
  if (pressed) OffsetRect(&label, 1, 1);
  // The dark cast under the lettering, then the gold: the game's text carries
  // an edge that keeps it readable over the red.
  RECT shadow = label;
  OffsetRect(&shadow, 1, 1);
  SetTextColor(dc, kFrame);
  DrawTextW(dc, text, -1, &shadow,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
  SetTextColor(dc, disabled ? kGoldDim : kGold);
  DrawTextW(dc, text, -1, &label,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
  SelectObject(dc, previous);
}

}  // namespace ulwin

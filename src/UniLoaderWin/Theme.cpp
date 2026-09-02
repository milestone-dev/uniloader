#include "Theme.hpp"

#include <objidl.h>   // before gdiplus.h, which needs IStream declared

#include <gdiplus.h>

#include <cmath>
#include <cstdint>
#include <memory>
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

/// A nine-slice image in the Android 9-patch format: the outer one-pixel
/// border is guides, black runs on the top and left edges marking the zones
/// that stretch. Corners stay pixel-true at any button size.
struct NinePatch {
  std::unique_ptr<Gdiplus::Bitmap> image;   // guides included; drawn around
  int left = 0, top = 0, right = 0, bottom = 0;   // fixed margins, in pixels
  bool ok = false;
};

struct Theme {
  HBITMAP parchment = nullptr;
  HBRUSH background = nullptr;
  HBRUSH panel = nullptr;
  HFONT button_font = nullptr;
  HFONT play_font = nullptr;
  NinePatch button;           // art/button.9.png, when the artist has drawn one
  NinePatch button_pressed;   // art/button-pressed.9.png, else button darkened
};

Theme g_theme;

/// A guide pixel: black, and actually there.
bool IsGuide(Gdiplus::Bitmap* image, int x, int y) {
  Gdiplus::Color colour;
  if (image->GetPixel(x, y, &colour) != Gdiplus::Ok) return false;
  return colour.GetA() > 128 && colour.GetR() < 64 && colour.GetG() < 64 &&
         colour.GetB() < 64;
}

NinePatch LoadNinePatch(const std::wstring& path) {
  NinePatch patch;
  auto image = std::make_unique<Gdiplus::Bitmap>(path.c_str());
  if (image->GetLastStatus() != Gdiplus::Ok) return patch;
  const int width = static_cast<int>(image->GetWidth());
  const int height = static_cast<int>(image->GetHeight());
  if (width < 4 || height < 4) return patch;

  // The stretch zones are the black runs on the guide row and column; what is
  // before and after them are the fixed margins.
  int first_x = 0, last_x = 0, first_y = 0, last_y = 0;
  for (int x = 1; x < width - 1; ++x) {
    if (!IsGuide(image.get(), x, 0)) continue;
    if (!first_x) first_x = x;
    last_x = x;
  }
  for (int y = 1; y < height - 1; ++y) {
    if (!IsGuide(image.get(), 0, y)) continue;
    if (!first_y) first_y = y;
    last_y = y;
  }
  // No guides means it is not a 9-patch; a third each way is the safe reading.
  patch.left = first_x ? first_x - 1 : (width - 2) / 3;
  patch.right = first_x ? (width - 2) - last_x : (width - 2) / 3;
  patch.top = first_y ? first_y - 1 : (height - 2) / 3;
  patch.bottom = first_y ? (height - 2) - last_y : (height - 2) / 3;
  patch.image = std::move(image);
  patch.ok = true;
  return patch;
}

/// Draws the patch over `box`: corners as they are, edges and middle
/// stretched. Source coordinates skip the one-pixel guide border.
void DrawNinePatch(HDC dc, const RECT& box, const NinePatch& patch) {
  Gdiplus::Graphics graphics(dc);
  graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBilinear);
  graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
  const int source_width = static_cast<int>(patch.image->GetWidth()) - 2;
  const int source_height = static_cast<int>(patch.image->GetHeight()) - 2;
  const int width = box.right - box.left;
  const int height = box.bottom - box.top;

  const int sx[4] = {0, patch.left, source_width - patch.right, source_width};
  const int sy[4] = {0, patch.top, source_height - patch.bottom, source_height};
  const int dx[4] = {0, patch.left, width - patch.right, width};
  const int dy[4] = {0, patch.top, height - patch.bottom, height};

  for (int row = 0; row < 3; ++row) {
    for (int column = 0; column < 3; ++column) {
      const int sw = sx[column + 1] - sx[column];
      const int sh = sy[row + 1] - sy[row];
      const int dw = dx[column + 1] - dx[column];
      const int dh = dy[row + 1] - dy[row];
      if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) continue;
      const Gdiplus::Rect destination(box.left + dx[column], box.top + dy[row], dw, dh);
      graphics.DrawImage(patch.image.get(), destination, 1 + sx[column], 1 + sy[row],
                         sw, sh, Gdiplus::UnitPixel);
    }
  }
}

/// Where the artist's files live: an `art` folder beside the exe, so a redrawn
/// PNG is picked up on the next start with no rebuild.
std::wstring ArtPath(const wchar_t* name) {
  wchar_t path[MAX_PATH] = {};
  GetModuleFileNameW(nullptr, path, MAX_PATH);
  std::wstring folder(path);
  const size_t slash = folder.find_last_of(L'\\');
  if (slash != std::wstring::npos) folder.resize(slash);
  return folder + L"\\art\\" + name;
}

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
  // The artist's buttons, when drawn. Absent files are simply the painted
  // style below; a pressed variant is optional on top of that.
  g_theme.button = LoadNinePatch(ArtPath(L"button.9.png"));
  g_theme.button_pressed = LoadNinePatch(ArtPath(L"button-pressed.9.png"));
}

void DestroyTheme() {
  if (g_theme.background) DeleteObject(g_theme.background);
  if (g_theme.panel) DeleteObject(g_theme.panel);
  if (g_theme.parchment) DeleteObject(g_theme.parchment);
  if (g_theme.button_font) DeleteObject(g_theme.button_font);
  if (g_theme.play_font) DeleteObject(g_theme.play_font);
  g_theme.button = NinePatch{};
  g_theme.button_pressed = NinePatch{};
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

  if (g_theme.button.ok) {
    // The artist's button. Pressed uses their pressed art when it exists, and
    // sinks the lettering either way.
    const NinePatch& patch = (pressed && g_theme.button_pressed.ok)
                                 ? g_theme.button_pressed
                                 : g_theme.button;
    // The pattern brush behind, in case the art carries transparency.
    FillRect(dc, &box, g_theme.background);
    DrawNinePatch(dc, box, patch);
    InflateRect(&box, -3, -3);
  } else {
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
  }

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

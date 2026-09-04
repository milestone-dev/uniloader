#include "Theme.hpp"

#include "resource.h"

#include <commctrl.h>   // SetWindowSubclass, for the dropdown's own button

#include <objidl.h>   // before gdiplus.h, which needs IStream declared

#include <gdiplus.h>

#include <algorithm>
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
// The art's own red, eyedropped off the sheet: the download bar's fill and the
// links in the description. `kWell` is the sunk socket the unchecked checkbox
// is drawn as, and what the empty half of the bar is dug out of.
constexpr COLORREF kRed = RGB(0x77, 0x00, 0x00);
constexpr COLORREF kWell = RGB(0x2C, 0x00, 0x00);
constexpr COLORREF kGold = RGB(233, 195, 90);
constexpr COLORREF kGoldDim = RGB(150, 126, 78);
constexpr COLORREF kFrame = RGB(12, 10, 8);
constexpr COLORREF kBevelLight = RGB(190, 188, 182);
constexpr COLORREF kBevelDark = RGB(70, 68, 64);

/// A nine-slice image. Two forms are read: the Android 9-patch, whose outer
/// one-pixel border is guides (black runs on the top and left edges marking
/// the zones that stretch), and a plain plaque with no guides, whose middle
/// third each way stretches. Corners stay pixel-true at any button size.
struct NinePatch {
  std::unique_ptr<Gdiplus::Bitmap> image;
  int border = 0;   // 1 when a guide border must be skipped, 0 for a plaque
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
  std::unique_ptr<Gdiplus::Bitmap> check;      // art/check.png, drawn 1:1-ish
  std::unique_ptr<Gdiplus::Bitmap> check_on;   // art/check-on.png
};

Theme g_theme;
bool g_themed = true;

/// A guide pixel: pure black, and actually there. Nothing merely dark counts —
/// the button's hand-drawn outer ring is 2C1A00, which a "dark enough" test
/// read as a guide, and the buttons lost their frame to it.
bool IsGuide(Gdiplus::Bitmap* image, int x, int y) {
  Gdiplus::Color colour;
  if (image->GetPixel(x, y, &colour) != Gdiplus::Ok) return false;
  return colour.GetA() > 128 && colour.GetR() == 0 && colour.GetG() == 0 &&
         colour.GetB() == 0;
}

/// Whether the top row and left column are an Android 9-patch guide border:
/// every pixel of them transparent or a pure black mark, with at least one
/// mark. Art that simply has a dark edge is a plaque and says so here, because
/// its border row is opaque paint rather than markings on nothing.
bool HasGuideBorder(Gdiplus::Bitmap* image, int width, int height) {
  bool marked = false;
  for (int x = 0; x < width; ++x) {
    Gdiplus::Color colour;
    if (image->GetPixel(x, 0, &colour) != Gdiplus::Ok) return false;
    if (colour.GetA() <= 16) continue;
    if (!IsGuide(image, x, 0)) return false;
    marked = true;
  }
  for (int y = 0; y < height; ++y) {
    Gdiplus::Color colour;
    if (image->GetPixel(0, y, &colour) != Gdiplus::Ok) return false;
    if (colour.GetA() <= 16) continue;
    if (!IsGuide(image, 0, y)) return false;
    marked = true;
  }
  return marked;
}

NinePatch MakePatch(std::unique_ptr<Gdiplus::Bitmap> image) {
  NinePatch patch;
  if (!image || image->GetLastStatus() != Gdiplus::Ok) return patch;
  const int width = static_cast<int>(image->GetWidth());
  const int height = static_cast<int>(image->GetHeight());
  if (width < 4 || height < 4) return patch;

  // With guides, the stretch zones are the black runs on the guide row and
  // column, and what is before and after them are the fixed margins.
  int first_x = 0, last_x = 0, first_y = 0, last_y = 0;
  if (HasGuideBorder(image.get(), width, height)) {
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
  }
  if (first_x && first_y) {
    patch.border = 1;
    patch.left = first_x - 1;
    patch.right = (width - 2) - last_x;
    patch.top = first_y - 1;
    patch.bottom = (height - 2) - last_y;
  } else {
    // A plain plaque: the fixed corners are a seventh of the sprite — four
    // pixels on the original 28, eight on a doubled 56 — so redrawing the art
    // at another scale keeps the same proportions without a word said here.
    patch.border = 0;
    const int smaller = width < height ? width : height;
    const int inset = smaller / 7 > 0 ? smaller / 7 : 1;
    patch.left = inset;
    patch.right = inset;
    patch.top = inset;
    patch.bottom = inset;
  }
  patch.image = std::move(image);
  patch.ok = true;
  return patch;
}

NinePatch LoadNinePatch(const std::wstring& path) {
  return MakePatch(std::make_unique<Gdiplus::Bitmap>(path.c_str()));
}

/// A bitmap out of an RCDATA resource — how the shipped one-file exe carries
/// the art. The pixels are copied off the stream so nothing has to outlive it.
std::unique_ptr<Gdiplus::Bitmap> LoadBitmapResource(int id) {
  HRSRC found = FindResourceW(nullptr, MAKEINTRESOURCEW(id), RT_RCDATA);
  if (!found) return {};
  HGLOBAL handle = LoadResource(nullptr, found);
  if (!handle) return {};
  const DWORD size = SizeofResource(nullptr, found);
  const void* bytes = LockResource(handle);
  if (!bytes || !size) return {};

  HGLOBAL copy = GlobalAlloc(GMEM_MOVEABLE, size);
  if (!copy) return {};
  memcpy(GlobalLock(copy), bytes, size);
  GlobalUnlock(copy);
  IStream* stream = nullptr;
  if (CreateStreamOnHGlobal(copy, TRUE, &stream) != S_OK) {
    GlobalFree(copy);
    return {};
  }
  auto loaded = std::make_unique<Gdiplus::Bitmap>(stream);
  // Cloned into pixels of its own: a Bitmap made from a stream reads from it
  // lazily, and the stream is about to be released.
  std::unique_ptr<Gdiplus::Bitmap> owned;
  if (loaded->GetLastStatus() == Gdiplus::Ok) {
    owned.reset(loaded->Clone(0, 0, loaded->GetWidth(), loaded->GetHeight(),
                              PixelFormat32bppARGB));
  }
  stream->Release();
  return owned;
}

/// Draws the patch over `box` the way a nine-patch is meant to be drawn, which
/// is Godot's NinePatchRect on tile: every piece lands at the size it was
/// drawn at, and nothing is scaled in either direction, ever. The corners go
/// down once each; the edges repeat along the one axis they are the middle of;
/// the centre repeats both ways. A piece that runs past its own cell is cut by
/// the clip, never squeezed. Source coordinates skip the guide border when
/// there is one.
void DrawNinePatch(HDC dc, const RECT& box, const NinePatch& patch) {
  const int source_width =
      static_cast<int>(patch.image->GetWidth()) - 2 * patch.border;
  const int source_height =
      static_cast<int>(patch.image->GetHeight()) - 2 * patch.border;
  const int width = box.right - box.left;
  const int height = box.bottom - box.top;
  if (source_width <= 0 || source_height <= 0 || width <= 0 || height <= 0) {
    return;
  }

  Gdiplus::Graphics graphics(dc);
  graphics.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
  graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);

  // The margins are the drawn ones. Only a box too small to hold both of them
  // moves them, and then they are shared out rather than shrunk on one side.
  int left = patch.left, right = patch.right;
  if (left + right > width) {
    const int corners = left + right;
    left = width * left / corners;
    right = width - left;
  }
  int top = patch.top, bottom = patch.bottom;
  if (top + bottom > height) {
    const int corners = top + bottom;
    top = height * top / corners;
    bottom = height - top;
  }

  const int sx[4] = {0, patch.left, source_width - patch.right, source_width};
  const int sy[4] = {0, patch.top, source_height - patch.bottom, source_height};
  const int dx[4] = {0, left, width - right, width};
  const int dy[4] = {0, top, height - bottom, height};

  for (int row = 0; row < 3; ++row) {
    const int sh = sy[row + 1] - sy[row];
    const int dh = dy[row + 1] - dy[row];
    if (sh <= 0 || dh <= 0) continue;
    for (int column = 0; column < 3; ++column) {
      const int sw = sx[column + 1] - sx[column];
      const int dw = dx[column + 1] - dx[column];
      if (sw <= 0 || dw <= 0) continue;

      const Gdiplus::GraphicsState state = graphics.Save();
      graphics.SetClip(
          Gdiplus::Rect(box.left + dx[column], box.top + dy[row], dw, dh),
          Gdiplus::CombineModeIntersect);
      // The far column and the bottom row are laid from their outer edge
      // inwards, so a cell too small loses its inside and keeps its frame.
      const int first_x = (column == 2) ? dx[3] - sw : dx[column];
      const int first_y = (row == 2) ? dy[3] - sh : dy[row];
      const int end_x = (column == 1) ? dx[2] : first_x + 1;
      const int end_y = (row == 1) ? dy[2] : first_y + 1;
      for (int y = first_y; y < end_y; y += sh) {
        for (int x = first_x; x < end_x; x += sw) {
          graphics.DrawImage(patch.image.get(),
                             Gdiplus::Rect(box.left + x, box.top + y, sw, sh),
                             patch.border + sx[column], patch.border + sy[row],
                             sw, sh, Gdiplus::UnitPixel);
        }
      }
      graphics.Restore(state);
    }
  }
}

/// One region of the sheet, in pixels, half-open.
struct Island {
  int left = 0, top = 0, right = 0, bottom = 0;
};

std::unique_ptr<Gdiplus::Bitmap> Crop(Gdiplus::Bitmap* sheet, const Island& island) {
  std::unique_ptr<Gdiplus::Bitmap> piece(
      sheet->Clone(island.left, island.top, island.right - island.left,
                   island.bottom - island.top, PixelFormat32bppARGB));
  if (piece && piece->GetLastStatus() == Gdiplus::Ok) return piece;
  return {};
}

/// How far the opaque pixels of one sheet row reach, counted from the left.
int RowReach(Gdiplus::Bitmap* sheet, int y) {
  Gdiplus::Color colour;
  for (int x = static_cast<int>(sheet->GetWidth()) - 1; x >= 0; --x) {
    if (sheet->GetPixel(x, y, &colour) == Gdiplus::Ok && colour.GetA() > 16) {
      return x + 1;
    }
  }
  return 0;
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
  // closest stock face to the game's lettering; the fallback keeps the shape
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
  // Nothing to build when the plain look was asked for: the brushes answer in
  // system colours, and no control is owner-drawn to want the rest.
  if (!g_themed) return;
  if (g_theme.background) return;
  g_theme.parchment = MakeParchment(256);
  g_theme.background = g_theme.parchment ? CreatePatternBrush(g_theme.parchment)
                                         : CreateSolidBrush(kParchment);
  g_theme.panel = CreateSolidBrush(kPanel);
  g_theme.button_font = MakeSerif(16, FW_BOLD);
  g_theme.play_font = MakeSerif(22, FW_BOLD);
  // The artist's sheet, ui.png:
  //
  //   the top block   the button plaque, then the same pressed
  //   under it        the checkbox, 15x15, unchecked then checked
  //
  // Both pairs read left to right as off then on.
  //
  // The row under those is the old progress unit and is not read any more —
  // the download bar is a flat fill. It stays on the sheet; only the code that
  // sliced it is gone.
  //
  // Sprites touch, so nothing here can be found by looking for gaps. A sheet
  // beside the exe (art\ui.png) wins, for redrawing without a rebuild; the
  // embedded copy is what ships.
  std::unique_ptr<Gdiplus::Bitmap> sheet;
  {
    auto from_disk = std::make_unique<Gdiplus::Bitmap>(ArtPath(L"ui.png").c_str());
    if (from_disk->GetLastStatus() == Gdiplus::Ok && from_disk->GetWidth() > 0) {
      sheet = std::move(from_disk);
    }
  }
  if (!sheet) sheet = LoadBitmapResource(IDR_SHEET);
  if (sheet) {
    const int sheet_width = static_cast<int>(sheet->GetWidth());
    const int sheet_height = static_cast<int>(sheet->GetHeight());
    // The button block is measured rather than written down, because it moves:
    // the plaque was drawn 28x28, is 56x56 now, and everything under it shifts
    // by the difference. The two buttons touch, so the pair is as wide as the
    // top row reaches and the block is as tall as the rows sharing that width.
    const int pair = RowReach(sheet.get(), 0);
    const int button = pair / 2;
    int block = 0;
    while (block < sheet_height && RowReach(sheet.get(), block) == pair) ++block;

    if (button > 0 && block > 0) {
      g_theme.button = MakePatch(Crop(sheet.get(), {0, 0, button, block}));
      g_theme.button_pressed =
          MakePatch(Crop(sheet.get(), {button, 0, 2 * button, block}));
    }

    // The checkbox pair is 15x15, drawn at that size on every sheet so far,
    // in the row below the button block.
    // Not named `small`: the Windows headers take that word for a typedef.
    const int unit = 15;
    if (block > 0 && sheet_width >= 2 * unit && sheet_height >= block + unit) {
      // Off first, then on — the same order the buttons are drawn in, where
      // the plaque comes before the same plaque pressed. Read by position, not
      // by which looks lit: once a checkmark is drawn on, "brighter" stops
      // telling the two apart.
      g_theme.check = Crop(sheet.get(), {0, block, unit, block + unit});
      g_theme.check_on =
          Crop(sheet.get(), {unit, block, 2 * unit, block + unit});
    }
  }

  // Loose files beside the exe fill whatever the sheet did not, and whatever
  // neither has falls through to the painted style. The plaque no longer ships
  // as its own resource — the sheet is the only art in the exe.
  auto plain = [](const std::wstring& path) -> std::unique_ptr<Gdiplus::Bitmap> {
    auto image = std::make_unique<Gdiplus::Bitmap>(path.c_str());
    if (image->GetLastStatus() != Gdiplus::Ok || image->GetWidth() == 0) return {};
    return image;
  };
  if (!g_theme.button.ok) g_theme.button = LoadNinePatch(ArtPath(L"button.9.png"));
  if (!g_theme.button_pressed.ok) {
    g_theme.button_pressed = LoadNinePatch(ArtPath(L"button-pressed.9.png"));
  }
  if (!g_theme.check) g_theme.check = plain(ArtPath(L"check.png"));
  if (!g_theme.check_on) g_theme.check_on = plain(ArtPath(L"check-on.png"));
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

Buffered::Buffered(HDC target_dc, const RECT& area) : target(target_dc), box(area) {
  const int width = box.right - box.left;
  const int height = box.bottom - box.top;
  if (!target || width <= 0 || height <= 0) return;
  scratch = CreateCompatibleDC(target);
  if (!scratch) return;
  bitmap = CreateCompatibleBitmap(target, width, height);
  if (!bitmap) {
    DeleteDC(scratch);
    scratch = nullptr;
    return;
  }
  previous = SelectObject(scratch, bitmap);
  // The whole point of not making the callers do arithmetic: the scratch is
  // shifted so that drawing at `box`'s own coordinates lands on it correctly.
  SetViewportOrgEx(scratch, -box.left, -box.top, nullptr);
}

Buffered::~Buffered() {
  if (!scratch) return;
  SetViewportOrgEx(scratch, 0, 0, nullptr);
  BitBlt(target, box.left, box.top, box.right - box.left, box.bottom - box.top,
         scratch, 0, 0, SRCCOPY);
  SelectObject(scratch, previous);
  DeleteObject(bitmap);
  DeleteDC(scratch);
}

bool ThemeEnabled() { return g_themed; }
void SetThemeEnabled(bool on) { g_themed = on; }
DWORD ThemedStyle(DWORD bits) { return g_themed ? bits : 0u; }

// Turned off, the two brushes answer in system colours: they are handed to a
// window class and to WM_CTLCOLOR*, both of which want a brush either way.
HBRUSH ThemeBackgroundBrush() {
  return g_themed ? g_theme.background : GetSysColorBrush(COLOR_BTNFACE);
}
HBRUSH ThemePanelBrush() {
  return g_themed ? g_theme.panel : GetSysColorBrush(COLOR_WINDOW);
}
COLORREF ThemeInk() { return kInk; }
COLORREF ThemeInkFaint() { return kInkFaint; }
COLORREF ThemePanel() { return kPanel; }
COLORREF ThemeMaroon() { return kMaroon; }
COLORREF ThemeGold() { return kGold; }
COLORREF ThemeLink() { return kRed; }
HFONT ThemeButtonFont() { return g_theme.button_font; }
HFONT ThemePlayFont() { return g_theme.play_font; }

void DrawThemedButton(const DRAWITEMSTRUCT* item, bool large) {
  if (!item) return;
  const Buffered buffer(item->hDC, item->rcItem);
  HDC dc = buffer.dc();
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

  // No focus outline of the theme's own: the artist's pressed sprite carries
  // the gold edge, and painting a second one over it doubled the signal.
  (void)focused;

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

void DrawThemedPlaque(HDC dc, const RECT& box, bool pressed) {
  const NinePatch& patch = (pressed && g_theme.button_pressed.ok)
                               ? g_theme.button_pressed
                               : g_theme.button;
  if (patch.ok) {
    FillRect(dc, &box, g_theme.background);
    DrawNinePatch(dc, box, patch);
    return;
  }
  HBRUSH frame = CreateSolidBrush(kFrame);
  FillRect(dc, &box, frame);
  DeleteObject(frame);
  RECT inner = box;
  InflateRect(&inner, -1, -1);
  HBRUSH fill = CreateSolidBrush(pressed ? kMaroonPressed : kMaroon);
  FillRect(dc, &inner, fill);
  DeleteObject(fill);
}

void DrawThemedCombo(const DRAWITEMSTRUCT* item) {
  if (!item) return;
  const Buffered buffer(item->hDC, item->rcItem);
  HDC dc = buffer.dc();
  RECT box = item->rcItem;
  const bool selected = (item->itemState & ODS_SELECTED) != 0;
  // ODS_COMBOBOXEDIT means the closed control itself; anything else is a row
  // of the list it drops down.
  const bool field = (item->itemState & ODS_COMBOBOXEDIT) != 0;

  wchar_t text[256] = {};
  if (item->itemID != static_cast<UINT>(-1)) {
    SendMessageW(item->hwndItem, CB_GETLBTEXT, item->itemID,
                 reinterpret_cast<LPARAM>(text));
  }

  HGDIOBJ previous = nullptr;
  if (field) {
    // Closed, the control wears the same plaque as a button, so the row of
    // controls reads as one set.
    DrawThemedPlaque(dc, box, false);
    SetTextColor(dc, kGold);
    previous = SelectObject(dc, g_theme.button_font);
    box.left += 8;
    box.right -= 4;
  } else {
    // Open, the list is a parchment pane and the row under the cursor takes
    // the maroon the plugin list uses for its selection.
    HBRUSH back = CreateSolidBrush(selected ? kMaroon : kPanel);
    FillRect(dc, &box, back);
    DeleteObject(back);
    SetTextColor(dc, selected ? kGold : kInk);
    box.left += 6;
    box.right -= 4;
  }

  SetBkMode(dc, TRANSPARENT);
  DrawTextW(dc, text, -1, &box,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
  if (previous) SelectObject(dc, previous);
}

namespace {

/// The dropdown's little chevron, in the lettering's gold.
void DrawChevron(HDC dc, const RECT& box) {
  const int x = (box.left + box.right) / 2;
  const int y = (box.top + box.bottom) / 2;
  const POINT points[3] = {{x - 4, y - 2}, {x + 4, y - 2}, {x, y + 3}};
  HBRUSH brush = CreateSolidBrush(kGold);
  HPEN pen = CreatePen(PS_SOLID, 1, kGold);
  HGDIOBJ old_brush = SelectObject(dc, brush);
  HGDIOBJ old_pen = SelectObject(dc, pen);
  Polygon(dc, points, 3);
  SelectObject(dc, old_brush);
  SelectObject(dc, old_pen);
  DeleteObject(brush);
  DeleteObject(pen);
}

/// The closed control is painted here rather than left to comctl32. Owner-draw
/// only ever hands over the text rectangle: the drop button and the themed
/// border around it are drawn by the control itself, and both came out in
/// system white against the plaque. Painting the whole client means there is
/// no part of it left for anyone else to draw. The list it drops down is still
/// owner-drawn a row at a time, through DrawThemedCombo.
LRESULT CALLBACK ComboProc(HWND window, UINT message, WPARAM w, LPARAM l,
                           UINT_PTR id, DWORD_PTR) {
  switch (message) {
    case WM_ERASEBKGND:
      return 1;   // every pixel is painted below; erasing first only flickers
    case WM_PAINT: {
      PAINTSTRUCT paint = {};
      HDC target = BeginPaint(window, &paint);
      if (!target) return 0;
      RECT client = {};
      GetClientRect(window, &client);
      {
        // Its own scope: the buffer copies itself over on the way out, and
        // that has to happen while the paint DC is still the caller's to draw
        // on — after EndPaint it is not.
        const Buffered buffer(target, client);
        HDC dc = buffer.dc();
        DrawThemedPlaque(dc, client, false);

        COMBOBOXINFO info = {};
        info.cbSize = sizeof(info);
        RECT button = {};
        if (GetComboBoxInfo(window, &info)) button = info.rcButton;

        wchar_t text[256] = {};
        const LRESULT chosen = SendMessageW(window, CB_GETCURSEL, 0, 0);
        if (chosen != CB_ERR) {
          SendMessageW(window, CB_GETLBTEXT, static_cast<WPARAM>(chosen),
                       reinterpret_cast<LPARAM>(text));
        }
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, kGold);
        HGDIOBJ previous = SelectObject(dc, g_theme.button_font);
        RECT label = client;
        label.left += 8;
        label.right = (button.left > label.left ? button.left : client.right) - 4;
        DrawTextW(dc, text, -1, &label,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        SelectObject(dc, previous);
        if (button.right > button.left) DrawChevron(dc, button);
      }
      EndPaint(window, &paint);
      return 0;
    }
    case WM_NCDESTROY:
      RemoveWindowSubclass(window, ComboProc, id);
      break;
    default:
      break;
  }
  return DefSubclassProc(window, message, w, l);
}

}  // namespace

void ThemeDropdown(HWND combo) {
  if (combo) SetWindowSubclass(combo, ComboProc, 1, 0);
}

void DrawThemedProgress(HDC target, const RECT& box, int permille) {
  if (permille < 0) permille = 0;
  if (permille > 1000) permille = 1000;
  // Buffered because this one repaints on every download tick, which is the
  // one place in the program where a flicker would be continuous.
  const Buffered buffer(target, box);
  HDC dc = buffer.dc();
  // The trough is a dark well, not parchment: filled with the panel colour it
  // was within a shade of the page behind it, and an empty bar read as nothing
  // being there at all.
  HBRUSH pane = CreateSolidBrush(kWell);
  FillRect(dc, &box, pane);
  DeleteObject(pane);
  HBRUSH frame = CreateSolidBrush(kFrame);
  FrameRect(dc, &box, frame);
  DeleteObject(frame);

  // Inside the frame, so the well keeps its edge at every width.
  RECT inside = {box.left + 1, box.top + 1, box.right - 1, box.bottom - 1};
  if (inside.right <= inside.left || inside.bottom <= inside.top) return;
  const int width = (inside.right - inside.left) * permille / 1000;
  if (width <= 0) return;
  RECT filled = {inside.left, inside.top, inside.left + width, inside.bottom};

  // Flat red, not the sheet's unit: a tiled sprite repeated its mottling along
  // the bar, and the one red reads as a filling bar at any width.
  HBRUSH fill = CreateSolidBrush(kRed);
  FillRect(dc, &filled, fill);
  DeleteObject(fill);
}

void DrawThemedCheckbox(HDC dc, const RECT& box, const wchar_t* label,
                        bool checked, HFONT font) {
  const int size = 16;
  const int top = box.top + ((box.bottom - box.top) - size) / 2;
  const RECT square = {box.left, top, box.left + size, top + size};

  Gdiplus::Bitmap* art =
      checked ? g_theme.check_on.get() : g_theme.check.get();
  if (art) {
    Gdiplus::Graphics graphics(dc);
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
    graphics.DrawImage(art, Gdiplus::Rect(square.left, square.top, size, size));
  } else {
    // The painted stand-in: a parchment pane in an ink frame, and the game's
    // red cross when it is on.
    HBRUSH pane = CreateSolidBrush(kPanel);
    FillRect(dc, &square, pane);
    DeleteObject(pane);
    HBRUSH frame = CreateSolidBrush(kFrame);
    FrameRect(dc, &square, frame);
    DeleteObject(frame);
    if (checked) {
      HPEN pen = CreatePen(PS_SOLID, 2, RGB(198, 30, 20));
      HGDIOBJ previous = SelectObject(dc, pen);
      MoveToEx(dc, square.left + 3, square.top + 3, nullptr);
      LineTo(dc, square.right - 3, square.bottom - 3);
      MoveToEx(dc, square.right - 4, square.top + 3, nullptr);
      LineTo(dc, square.left + 2, square.bottom - 4);
      SelectObject(dc, previous);
      DeleteObject(pen);
    }
  }

  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, kInk);
  HGDIOBJ previous = SelectObject(dc, font);
  RECT text = box;
  text.left = square.right + 8;
  DrawTextW(dc, label, -1, &text,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
  SelectObject(dc, previous);
}

}  // namespace ulwin

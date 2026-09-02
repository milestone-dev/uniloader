#include "Slideshow.hpp"

#include "Strings.hpp"
#include "strings.h"

#include <objidl.h>   // before gdiplus.h, which needs IStream declared

#include <gdiplus.h>

#include <memory>

namespace ulwin {
namespace {

constexpr wchar_t kClassName[] = L"UniLoaderSlideshow";

struct Panel {
  std::vector<GalleryItem> items;
  int index = 0;
  // Only the shot on screen is decoded. A plugin can carry a dozen 960x544 PNGs
  // and there are nineteen plugins; holding them all as bitmaps would be a few
  // hundred megabytes to show one picture.
  std::unique_ptr<Gdiplus::Image> current;
  int loaded = -1;   // which index `current` holds, or -1
};

Panel* Of(HWND window) {
  return reinterpret_cast<Panel*>(GetWindowLongPtrW(window, GWLP_USERDATA));
}

void Load(Panel* panel) {
  if (panel->loaded == panel->index) return;
  panel->current.reset();
  panel->loaded = panel->index;
  if (panel->index < 0 || panel->index >= static_cast<int>(panel->items.size())) return;
  const std::wstring& file = panel->items[static_cast<size_t>(panel->index)].image;
  if (file.empty()) return;   // a video whose thumbnail has not arrived yet
  auto image = std::make_unique<Gdiplus::Image>(file.c_str());
  // A file that is not an image, or is half-written, comes back as an Image
  // that reports a status. Kept as null rather than drawn, so the placeholder
  // shows instead of a black rectangle nobody can interpret.
  if (image->GetLastStatus() == Gdiplus::Ok && image->GetWidth() > 0) {
    panel->current = std::move(image);
  }
}

/// The largest rectangle with the image's aspect that fits inside `into`,
/// centred. Letterboxed rather than stretched: a screenshot at the wrong aspect
/// is the author's, and squashing their artwork to fill a box is worse than a
/// pair of margins.
RECT Fit(int width, int height, const RECT& into) {
  const double box_width = static_cast<double>(into.right - into.left);
  const double box_height = static_cast<double>(into.bottom - into.top);
  if (width <= 0 || height <= 0 || box_width <= 0 || box_height <= 0) return into;
  const double scale =
      (box_width / width < box_height / height) ? box_width / width : box_height / height;
  const int drawn_width = static_cast<int>(width * scale + 0.5);
  const int drawn_height = static_cast<int>(height * scale + 0.5);
  const int x = into.left + static_cast<int>((box_width - drawn_width) / 2);
  const int y = into.top + static_cast<int>((box_height - drawn_height) / 2);
  return RECT{x, y, x + drawn_width, y + drawn_height};
}

/// A play triangle in a rounded box, centred. Deliberately plain: it has to
/// read at a glance over any thumbnail, and anything more decorative competes
/// with the picture it is sitting on.
void DrawPlayBadge(HDC dc, const RECT& area) {
  const int width = area.right - area.left;
  const int height = area.bottom - area.top;
  const int size = (width < height ? width : height) / 6;
  if (size < 16) return;
  const int cx = area.left + width / 2;
  const int cy = area.top + height / 2;

  Gdiplus::Graphics graphics(dc);
  graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
  Gdiplus::SolidBrush plate(Gdiplus::Color(190, 0, 0, 0));
  graphics.FillEllipse(&plate, cx - size, cy - size, size * 2, size * 2);

  Gdiplus::PointF triangle[3] = {
      Gdiplus::PointF(static_cast<Gdiplus::REAL>(cx - size / 3),
                      static_cast<Gdiplus::REAL>(cy - size / 2)),
      Gdiplus::PointF(static_cast<Gdiplus::REAL>(cx - size / 3),
                      static_cast<Gdiplus::REAL>(cy + size / 2)),
      Gdiplus::PointF(static_cast<Gdiplus::REAL>(cx + size / 2),
                      static_cast<Gdiplus::REAL>(cy))};
  Gdiplus::SolidBrush arrow(Gdiplus::Color(235, 255, 255, 255));
  graphics.FillPolygon(&arrow, triangle, 3);
}

void DrawPlaceholder(HDC dc, const RECT& area) {
  // The fallback screen. A plugin with no screenshots is a normal state — most
  // of them will have none until dannyldd adds some — and an empty panel reads
  // as a program that failed to load something. A framed-picture glyph,
  // centred, says what the space is for; it needs no caption.
  const int width = area.right - area.left;
  const int height = area.bottom - area.top;
  const int glyph_h = (width < height ? width : height) / 5;
  const int glyph_w = glyph_h * 3 / 2;
  if (glyph_h < 24) return;   // too small to read as anything

  const int left = area.left + (width - glyph_w) / 2;
  const int top = area.top + (height - glyph_h) / 2;

  Gdiplus::Graphics graphics(dc);
  graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
  Gdiplus::Pen pen(Gdiplus::Color(255, 96, 96, 108), 2.0f);
  pen.SetLineJoin(Gdiplus::LineJoinRound);
  graphics.DrawRectangle(&pen, left, top, glyph_w, glyph_h);

  // The sun, top right.
  const int sun = glyph_h / 5;
  graphics.DrawEllipse(&pen, left + glyph_w - sun * 2, top + sun / 2 + 2, sun, sun);

  // The mountain range, clipped to the frame so its ends run off the edges.
  Gdiplus::Region outside;
  graphics.GetClip(&outside);
  graphics.SetClip(Gdiplus::Rect(left + 1, top + 1, glyph_w - 2, glyph_h - 2));
  const Gdiplus::Point range[] = {
      {left - glyph_w / 8, top + glyph_h},
      {left + glyph_w * 3 / 8, top + glyph_h * 2 / 5},
      {left + glyph_w * 5 / 8, top + glyph_h * 3 / 4},
      {left + glyph_w * 3 / 4, top + glyph_h / 2},
      {left + glyph_w + glyph_w / 8, top + glyph_h}};
  graphics.DrawLines(&pen, range, 5);
  graphics.SetClip(&outside);
}

void Paint(HWND window) {
  PAINTSTRUCT paint;
  HDC dc = BeginPaint(window, &paint);
  RECT area;
  GetClientRect(window, &area);

  // Painted into a memory DC and blitted once. The panel is nearly a megapixel
  // and repaints on every selection change; drawn straight to the window it
  // flickers, and the flicker is worst exactly when someone is clicking through
  // the list looking at plugins.
  HDC memory = CreateCompatibleDC(dc);
  HBITMAP bitmap = CreateCompatibleBitmap(dc, area.right, area.bottom);
  HGDIOBJ previous = SelectObject(memory, bitmap);

  // Pure black, the same ground the video player page paints — so a letterbox
  // margin, the fallback screen and the player are one continuous surface.
  HBRUSH background = CreateSolidBrush(RGB(0, 0, 0));
  FillRect(memory, &area, background);
  DeleteObject(background);

  Panel* panel = Of(window);
  if (panel) {
    Load(panel);
    if (panel->current) {
      Gdiplus::Graphics graphics(memory);
      graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
      graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
      const RECT into = Fit(static_cast<int>(panel->current->GetWidth()),
                            static_cast<int>(panel->current->GetHeight()), area);
      // Through a Rect rather than four ints: the four-int overload is
      // ambiguous between the INT and REAL forms, and the Rect says what is
      // meant anyway.
      const Gdiplus::Rect target(into.left, into.top, into.right - into.left,
                                 into.bottom - into.top);
      graphics.DrawImage(panel->current.get(), target);
    } else {
      DrawPlaceholder(memory, area);
    }

    // The play badge, over a video. Drawn here rather than baked into the
    // thumbnail so that a thumbnail that has not downloaded yet still shows
    // something that says "this one is a video".
    if (panel->index >= 0 && panel->index < static_cast<int>(panel->items.size()) &&
        !panel->items[static_cast<size_t>(panel->index)].video_url.empty()) {
      DrawPlayBadge(memory, area);
    }

    if (panel->items.size() > 1) {
      // "3 of 7", bottom right. Small, and only when there is more than one —
      // a counter that always says "1 of 1" teaches people to ignore it.
      const std::wstring counter =
          Format(IDS_SHOT_COUNT, panel->index + 1, static_cast<int>(panel->items.size()));
      RECT corner = area;
      corner.right -= 10;
      corner.bottom -= 6;
      SetBkMode(memory, TRANSPARENT);
      SetTextColor(memory, RGB(200, 200, 200));
      DrawTextW(memory, counter.c_str(), -1, &corner,
                DT_RIGHT | DT_BOTTOM | DT_SINGLELINE);
    }
  }

  BitBlt(dc, 0, 0, area.right, area.bottom, memory, 0, 0, SRCCOPY);
  SelectObject(memory, previous);
  DeleteObject(bitmap);
  DeleteDC(memory);
  EndPaint(window, &paint);
}

LRESULT CALLBACK Proc(HWND window, UINT message, WPARAM w, LPARAM l) {
  switch (message) {
    case WM_CREATE:
      SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(new Panel()));
      return 0;
    case WM_DESTROY:
      delete Of(window);
      SetWindowLongPtrW(window, GWLP_USERDATA, 0);
      return 0;
    case WM_PAINT:
      Paint(window);
      return 0;
    case WM_ERASEBKGND:
      return 1;   // painted whole in WM_PAINT; erasing first is the flicker
    case WM_LBUTTONDOWN: {
      // Clicking a video plays it; clicking a picture advances the gallery,
      // which is what people try first on a picture and never on a video.
      Panel* panel = Of(window);
      const bool video = panel && panel->index >= 0 &&
                         panel->index < static_cast<int>(panel->items.size()) &&
                         !panel->items[static_cast<size_t>(panel->index)]
                              .video_url.empty();
      if (video) {
        // Reported to the parent rather than opened here: the panel knows what
        // was clicked, and the host knows what opening a URL is allowed to mean.
        SendMessageW(GetParent(window), WM_COMMAND,
                     MAKEWPARAM(GetDlgCtrlID(window), kSlideshowPlayVideo),
                     reinterpret_cast<LPARAM>(window));
      } else {
        StepSlideshow(window, 1);
        // Told, so the hidden player can follow: prime the video now on
        // screen, or unload one the gallery just stepped off.
        SendMessageW(GetParent(window), WM_COMMAND,
                     MAKEWPARAM(GetDlgCtrlID(window), kSlideshowStepped),
                     reinterpret_cast<LPARAM>(window));
      }
      return 0;
    }
    default:
      return DefWindowProcW(window, message, w, l);
  }
}

}  // namespace

void RegisterSlideshow() {
  WNDCLASSEXW definition = {};
  definition.cbSize = sizeof(definition);
  definition.lpfnWndProc = Proc;
  definition.hInstance = GetModuleHandleW(nullptr);
  definition.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  // The picture is scaled to the panel, so a panel that changed size is a
  // picture that has to be drawn again — not one that keeps its old pixels in
  // the top-left corner of a bigger box.
  definition.style = CS_HREDRAW | CS_VREDRAW;
  definition.lpszClassName = kClassName;
  RegisterClassExW(&definition);
}

HWND CreateSlideshow(HWND parent, int id) {
  return CreateWindowExW(0, kClassName, L"", WS_CHILD | WS_VISIBLE, 0, 0, 10, 10,
                         parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                         GetModuleHandleW(nullptr), nullptr);
}

void SetSlideshowItems(HWND panel_window, const std::vector<GalleryItem>& items) {
  Panel* panel = Of(panel_window);
  if (!panel) return;
  panel->items = items;
  panel->index = 0;
  panel->loaded = -1;
  panel->current.reset();
  InvalidateRect(panel_window, nullptr, FALSE);
}

void StepSlideshow(HWND panel_window, int delta) {
  Panel* panel = Of(panel_window);
  if (!panel || panel->items.size() < 2) return;
  const int count = static_cast<int>(panel->items.size());
  panel->index = ((panel->index + delta) % count + count) % count;
  InvalidateRect(panel_window, nullptr, FALSE);
}

int SlideshowIndex(HWND panel_window) {
  Panel* panel = Of(panel_window);
  return panel ? panel->index : 0;
}

int SlideshowCount(HWND panel_window) {
  Panel* panel = Of(panel_window);
  return panel ? static_cast<int>(panel->items.size()) : 0;
}

std::wstring SlideshowVideoUrl(HWND panel_window) {
  Panel* panel = Of(panel_window);
  if (!panel || panel->index < 0 ||
      panel->index >= static_cast<int>(panel->items.size())) {
    return {};
  }
  return panel->items[static_cast<size_t>(panel->index)].video_url;
}

std::string SlideshowVideoId(HWND panel_window) {
  Panel* panel = Of(panel_window);
  if (!panel || panel->index < 0 ||
      panel->index >= static_cast<int>(panel->items.size())) {
    return {};
  }
  return panel->items[static_cast<size_t>(panel->index)].video_id;
}

std::string SlideshowVideoListId(HWND panel_window) {
  Panel* panel = Of(panel_window);
  if (!panel || panel->index < 0 ||
      panel->index >= static_cast<int>(panel->items.size())) {
    return {};
  }
  return panel->items[static_cast<size_t>(panel->index)].list_id;
}

}  // namespace ulwin

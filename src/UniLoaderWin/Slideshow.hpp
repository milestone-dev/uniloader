// The gallery.
//
// A plugin folder carries an info.txt and some PNGs, and the info.txt usually
// carries a YouTube link or two. Both go in here: to somebody choosing between
// eighteen sub-mods, a video of one running is a better screenshot than a
// screenshot, and putting it anywhere else would mean a second control that is
// empty most of the time.
//
// Drawn through GDI+, which is in every Windows since XP and needs nothing
// installed. The core has no image decoder and does not want one: a screenshot
// is the host's business, and so is the format the author happened to save it
// in — GDI+ reads PNG, JPEG, GIF and BMP without being told which.
//
// The shots are authored at 960x544. Anything else is fitted into the panel
// with its aspect kept, because a mod author who saves one at a different size
// should get a letterboxed screenshot and not a stretched one.

#pragma once

#include <windows.h>

#include <string>
#include <vector>

namespace ulwin {

/// One thing in the gallery: a picture, and — when it is a video — the page to
/// open if it is clicked. `image` is a local file either way; for a video it is
/// the thumbnail fetched from img.youtube.com.
struct GalleryItem {
  std::wstring image;
  std::wstring video_url;   // "" for a plain screenshot
  std::string video_id;     // the eleven characters YouTube knows it by
  std::string list_id;      // set instead of video_id when it is a playlist
};

/// Registers the window class. Called once, before the first panel is created.
void RegisterSlideshow();

/// Creates the panel as a child of `parent`.
HWND CreateSlideshow(HWND parent, int id);

/// Replaces what it is showing, and goes back to the first item. An empty list
/// is a normal state — a plugin may have neither pictures nor videos — and
/// draws the placeholder rather than nothing.
void SetSlideshowItems(HWND panel, const std::vector<GalleryItem>& items);

/// Steps the gallery. Wraps at both ends, so a two-item plugin can be flicked
/// between without noticing which end it is at.
void StepSlideshow(HWND panel, int delta);

int SlideshowIndex(HWND panel);
int SlideshowCount(HWND panel);

/// The page behind the item on screen, or "" when it is a plain screenshot.
/// Read by the parent after the panel reports a click: the panel knows what was
/// clicked and the host knows what opening a URL means.
std::wstring SlideshowVideoUrl(HWND panel);

/// The same item's YouTube id, for playing it in place. The watch URL is what a
/// browser wants; the id is what the local player page wants.
std::string SlideshowVideoId(HWND panel);

/// And its playlist id, for an item that is a playlist rather than one video.
std::string SlideshowVideoListId(HWND panel);

/// What the panel sends its parent through WM_COMMAND when a video is clicked.
constexpr WORD kSlideshowPlayVideo = 1;
/// And when a click on a picture advanced the gallery: the item on screen
/// changed without the parent doing it, and the parent keeps the hidden video
/// player matched to whatever is on screen.
constexpr WORD kSlideshowStepped = 2;

/// The size a shot is authored at. Used to lay the window out around the panel
/// so that the common case is drawn one-to-one and not resampled.
constexpr int kShotWidth = 960;
constexpr int kShotHeight = 544;

}  // namespace ulwin

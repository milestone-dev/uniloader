// Playing a plugin's video inside the gallery.
//
// A YouTube link in a plugin.txt used to open a browser. That is a different
// window, over the top of the one the choice is being made in, and it takes the
// person out of the program to look at the thing the program is describing.
//
// So the video plays where the screenshot would be, through WebView2 pointed at
// youtube.com/embed — the endpoint that exists to be framed, unlike the watch
// page, which refuses. The core already builds that URL:
// ul_plugin_video_embed_url.
//
// WebView2 is Edge's engine and ships with Windows 11 and with any Windows 10
// that has a current Edge. When it is not there the gallery keeps its
// thumbnail-and-play-badge behaviour and the click opens a browser as before —
// the loader is linked statically, so a missing *runtime* is a feature that
// does not appear, never a program that will not start.

#pragma once

#include <windows.h>

#include <string>

namespace ulwin {

/// Creates the player as a hidden child of `parent`. `user_data_folder` is
/// where WebView2 keeps its cache and profile; it is created if missing.
///
/// Returns immediately. The runtime starts up on a callback, so ready-ness is
/// asked for separately rather than waited on — the window must not block for
/// however long a first-run browser initialisation takes.
void CreateVideoPlayer(HWND parent, const std::wstring& user_data_folder);

/// Whether the engine came up. False until the callbacks have run, and false
/// forever on a machine with no WebView2 runtime.
bool VideoPlayerReady();

/// Whether it will never come up — the runtime is absent or refused to start.
/// Distinct from "not ready yet", because the two want opposite behaviour: one
/// waits, and the other falls back to a browser.
bool VideoPlayerFailed();

/// Plays the YouTube video `video_id` — or, when it is empty, the playlist
/// `list_id` — over `rect`, in the parent's client coordinates. The player
/// keeps one embed per recent gallery item, so a primed item starts
/// near-instantly and an item played before *resumes* where it was paused.
void PlayVideo(const std::string& video_id, const std::string& list_id,
               const RECT& rect);

/// Loads the item's embed into the still-hidden player and buffers it: it
/// autoplays muted and is paused the moment it reports playing, so a later
/// PlayVideo of it starts from the buffer instead of booting the embed and
/// fetching the stream. Embeds for other items are kept alongside, up to a
/// small cap, which is what makes stepping back and forth between videos
/// quick. Empty ids do nothing.
void PrimeVideo(const std::string& video_id, const std::string& list_id);

/// Drops every kept embed. Called when the gallery changes wholesale — a
/// different plugin was selected — since positions and buffers for items no
/// longer on offer are only memory spent.
void ResetVideoPlayer();

/// Stops playing and hides the player. Called whenever the thing underneath it
/// changes: another gallery item, another plugin, a window resize.
void StopVideo();

/// Whether the player is currently over the gallery.
bool VideoPlaying();

/// Follows the gallery when the window is resized.
void MoveVideoPlayer(const RECT& rect);

/// Shuts the engine down. Called before the window is destroyed.
void DestroyVideoPlayer();

}  // namespace ulwin

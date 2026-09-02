#include "Video.hpp"

#ifdef UL_NO_WEBVIEW2

// A build without the SDK. Every call is a no-op and VideoPlayerFailed() says
// so from the start, which is the same path a machine with no runtime takes.
namespace ulwin {
void CreateVideoPlayer(HWND, const std::wstring&) {}
bool VideoPlayerReady() { return false; }
bool VideoPlayerFailed() { return true; }
void PlayVideo(const std::string&, const std::string&, const RECT&) {}
void PrimeVideo(const std::string&, const std::string&) {}
void ResetVideoPlayer() {}
void StopVideo() {}
bool VideoPlaying() { return false; }
void MoveVideoPlayer(const RECT&) {}
void DestroyVideoPlayer() {}
}  // namespace ulwin

#else

#include "Files.hpp"
#include "resource.h"

// WebView2.h is a COM header and says so in COM's vocabulary — `interface`,
// IUnknown, EventRegistrationToken — none of which windows.h brings in while
// WIN32_LEAN_AND_MEAN is on, and the client builds with it on. Included here
// rather than by turning that off, which would slow every other file down.
#include <objbase.h>
#include <shellapi.h>   // ShellExecuteW: escaping links go to the real browser
#include <unknwn.h>
#include <eventtoken.h>

#include <WebView2.h>
#include <wrl/client.h>
#include <wrl/event.h>          // Microsoft::WRL::Callback
#include <wrl/implements.h>

namespace ulwin {
namespace {

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

struct Player {
  HWND parent = nullptr;
  ComPtr<ICoreWebView2Controller> controller;
  ComPtr<ICoreWebView2> view;
  bool ready = false;
  bool failed = false;
  bool showing = false;
  RECT bounds = {};
  // Whether the player page has finished loading. It loads exactly once, when
  // the engine comes up; until then every request is remembered below and
  // flushed when it lands — a script run into a page that is not there yet is
  // a script run into a void.
  bool page_ready = false;
  // A click or a prime that arrived before the page was up. A person who
  // clicks the play badge during the one-off first-run initialisation should
  // get their video when it finishes, not silence.
  bool play_pending = false;
  bool prime_pending = false;
  std::string pending_id;
  std::string pending_list;
};

Player g_player;

constexpr wchar_t kVirtualHost[] = L"uniloader.local";

/// The page the player actually loads.
///
/// Navigating straight to youtube.com/embed/<id> puts the embed up as the
/// top-level document, and YouTube refuses that: "Error 153, video player
/// configuration error". The embed wants to be *in* a page, on an origin it can
/// see. So there is a page, it is served from a virtual host mapped to a local
/// folder, and the iframe sits inside it.
constexpr char kPlayerPage[] = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><title>UniLoader</title>
<style>
  html,body{margin:0;height:100%;background:#000;overflow:hidden}
  iframe{border:0;display:block;width:100%;height:100%}
</style></head>
<body><script>
  // One page, several embeds. Navigating a fresh page per video meant every
  // switch tore an embed down and booted another — seconds, every time. Here
  // each video the gallery lands on gets its own iframe, loaded muted and
  // paused once buffered, and kept: switching is hiding one and showing
  // another, and a video stepped away from pauses in place and resumes.
  var players = {};    // "id|list" -> {frame, shown, state, wanted, order}
  var current = null;
  var stamp = 0;
  function srcFor(id, list) {
    // Eleven characters of YouTube's own alphabet for a video, 13 to 64 of
    // it for a playlist, or nothing happens: this page is local and the ids
    // come from a mod author's text file. videoseries is how YouTube spells
    // "embed a playlist". enablejsapi is what lets this page give orders;
    // autoplay muted is what makes YouTube actually buffer — a merely-loaded
    // player fetches nothing.
    var s = "";
    if (/^[A-Za-z0-9_-]{11}$/.test(id)) {
      s = "https://www.youtube.com/embed/" + id + "?";
    } else if (/^[A-Za-z0-9_-]{13,64}$/.test(list)) {
      s = "https://www.youtube.com/embed/videoseries?list=" + list + "&";
    } else {
      return "";
    }
    return s + "rel=0&playsinline=1&enablejsapi=1&autoplay=1&mute=1";
  }
  function tell(p, func, args) {
    p.frame.contentWindow.postMessage('{"event":"listening","id":"ul"}', "*");
    p.frame.contentWindow.postMessage(
        '{"event":"command","func":"' + func + '","args":' + (args || '""') + '}',
        "*");
  }
  window.addEventListener("message", function (e) {
    var d = null;
    try { d = JSON.parse(e.data); } catch (err) { return; }
    if (!d || !d.info || typeof d.info.playerState !== "number") return;
    for (var k in players) {
      var p = players[k];
      if (p.frame.contentWindow === e.source) {
        p.state = d.info.playerState;
        // A warm-up reached "playing" — muted, invisible. Paused right there:
        // the buffer it filled is the point, and a paused player keeps it.
        if (!p.wanted && p.state === 1) tell(p, "pauseVideo");
        return;
      }
    }
  });
  function load(id, list) {
    var src = srcFor(id, list);
    if (!src) return null;
    var key = id + "|" + list;
    if (players[key]) return players[key];
    // At most four embeds live at once: plenty for stepping around one
    // gallery, bounded for the machine. The one nobody has looked at for
    // longest goes.
    for (var keys = Object.keys(players); keys.length >= 4;
         keys = Object.keys(players)) {
      var oldest = null;
      for (var i = 0; i < keys.length; i++) {
        var q = players[keys[i]];
        if (q === current) continue;
        if (!oldest || q.order < players[oldest].order) oldest = keys[i];
      }
      if (!oldest) break;
      document.body.removeChild(players[oldest].frame);
      delete players[oldest];
    }
    var f = document.createElement("iframe");
    f.src = src;
    f.allow = "autoplay; encrypted-media; fullscreen; picture-in-picture";
    f.setAttribute("allowfullscreen", "");
    f.style.display = "none";
    document.body.appendChild(f);
    var p = {frame: f, shown: false, state: -2, wanted: false, order: ++stamp};
    players[key] = p;
    // The "listening" handshake, until the embed answers: answering is what
    // lets the pause-when-buffered above see the state at all.
    var hello = setInterval(function () {
      if (players[key] !== p || p.state !== -2) { clearInterval(hello); return; }
      p.frame.contentWindow.postMessage('{"event":"listening","id":"ul"}', "*");
    }, 250);
    return p;
  }
  function show(id, list) {
    var p = load(id, list);
    if (!p) return;
    if (current && current !== p) {
      current.wanted = false;
      tell(current, "pauseVideo");
      current.frame.style.display = "none";
    }
    current = p;
    p.order = ++stamp;
    p.wanted = true;
    p.frame.style.display = "block";
    // A first showing starts from the top, wherever the muted warm-up got to.
    // One somebody was already watching resumes where it paused.
    if (!p.shown) tell(p, "seekTo", "[0,true]");
    p.shown = true;
    // Repeated on a short timer because a still-booting embed drops commands
    // silently; stops the moment it reports playing, or when the person has
    // since stepped away, so nobody's own pause is fought.
    var tries = 0;
    var go = function () { tell(p, "unMute"); tell(p, "playVideo"); };
    go();
    var timer = setInterval(function () {
      if (!p.wanted || p.state === 1 || ++tries > 25) { clearInterval(timer); return; }
      go();
    }, 150);
  }
  function hide() {
    if (!current) return;
    current.wanted = false;
    tell(current, "pauseVideo");
    current.frame.style.display = "none";
    current = null;
  }
  function clearAll() {
    for (var k in players) document.body.removeChild(players[k].frame);
    players = {};
    current = null;
  }
</script></body></html>
)HTML";

std::wstring g_folder;

void ApplyBounds() {
  if (!g_player.controller) return;
  g_player.controller->put_Bounds(g_player.bounds);
}

void Navigate(const std::wstring& url) {
  if (!g_player.view) return;
  g_player.page_ready = false;
  g_player.view->Navigate(url.c_str());
}

/// Runs one script call in the player page. The ids reach here from the
/// catalogue's link scanner, which only ever produces the [A-Za-z0-9_-]
/// alphabet — and the page checks them again before building an embed URL.
void Script(const wchar_t* func, const std::string& video_id,
            const std::string& list_id) {
  if (!g_player.view || !g_player.page_ready) return;
  const std::wstring call = std::wstring(func) + L"('" +
                            std::wstring(video_id.begin(), video_id.end()) +
                            L"','" +
                            std::wstring(list_id.begin(), list_id.end()) + L"');";
  g_player.view->ExecuteScript(call.c_str(), nullptr);
}

}  // namespace

void CreateVideoPlayer(HWND parent, const std::wstring& user_data_folder) {
  g_player.parent = parent;
  g_folder = user_data_folder;
  EnsureFolder(user_data_folder);
  // Rewritten every start rather than only when missing: it is generated, and a
  // stale copy from an older build is a bug nobody would think to look for.
  WriteTextFile(user_data_folder + L"\\player.html", kPlayerPage);

  // Both stages are asynchronous and either can fail — most often because the
  // runtime is not installed, which is not an error worth telling anyone about:
  // it just means no inline player on this machine.
  const HRESULT started = CreateCoreWebView2EnvironmentWithOptions(
      nullptr, user_data_folder.c_str(), nullptr,
      Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
          [](HRESULT result, ICoreWebView2Environment* environment) -> HRESULT {
            if (FAILED(result) || !environment) {
              g_player.failed = true;
              return S_OK;
            }
            environment->CreateCoreWebView2Controller(
                g_player.parent,
                Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                    [](HRESULT made, ICoreWebView2Controller* controller) -> HRESULT {
                      if (FAILED(made) || !controller) {
                        g_player.failed = true;
                        return S_OK;
                      }
                      g_player.controller = controller;
                      controller->get_CoreWebView2(&g_player.view);
                      if (g_player.view) {
                        ComPtr<ICoreWebView2Settings> settings;
                        if (SUCCEEDED(g_player.view->get_Settings(&settings)) && settings) {
                          // Nothing here is a browser. No devtools, no context
                          // menu offering "save as", no status bar creeping over
                          // the corner of the picture.
                          settings->put_AreDevToolsEnabled(FALSE);
                          settings->put_AreDefaultContextMenusEnabled(FALSE);
                          settings->put_IsStatusBarEnabled(FALSE);
                        }
                        // And it never opens windows of its own. Everything the
                        // embed asks a window for — "Watch on YouTube", "More
                        // videos", the share targets — goes to the person's own
                        // browser, where their account and their history are. A
                        // WebView2 popup would be a bare browser window wearing
                        // this program's name and none of either.
                        EventRegistrationToken opened;
                        g_player.view->add_NewWindowRequested(
                            Callback<ICoreWebView2NewWindowRequestedEventHandler>(
                                [](ICoreWebView2*,
                                   ICoreWebView2NewWindowRequestedEventArgs* args)
                                    -> HRESULT {
                                  LPWSTR uri = nullptr;
                                  if (SUCCEEDED(args->get_Uri(&uri)) && uri) {
                                    ShellExecuteW(nullptr, L"open", uri, nullptr,
                                                  nullptr, SW_SHOWNORMAL);
                                    CoTaskMemFree(uri);
                                  }
                                  args->put_Handled(TRUE);
                                  return S_OK;
                                })
                                .Get(),
                            &opened);
                        // The page has landed: scripts can reach it now, and
                        // anything asked for while it was loading runs.
                        EventRegistrationToken landed;
                        g_player.view->add_NavigationCompleted(
                            Callback<ICoreWebView2NavigationCompletedEventHandler>(
                                [](ICoreWebView2*,
                                   ICoreWebView2NavigationCompletedEventArgs*)
                                    -> HRESULT {
                                  g_player.page_ready = true;
                                  if (g_player.play_pending) {
                                    g_player.play_pending = false;
                                    ApplyBounds();
                                    Script(L"show", g_player.pending_id,
                                           g_player.pending_list);
                                    g_player.controller->put_IsVisible(TRUE);
                                  } else if (g_player.prime_pending) {
                                    g_player.prime_pending = false;
                                    Script(L"load", g_player.pending_id,
                                           g_player.pending_list);
                                  }
                                  return S_OK;
                                })
                                .Get(),
                            &landed);
                        // Same for a link that tries to *navigate* the player
                        // away instead of opening a window. Only the local page
                        // and the blank page it parks on may load here.
                        EventRegistrationToken navigated;
                        g_player.view->add_NavigationStarting(
                            Callback<ICoreWebView2NavigationStartingEventHandler>(
                                [](ICoreWebView2*,
                                   ICoreWebView2NavigationStartingEventArgs* args)
                                    -> HRESULT {
                                  LPWSTR uri = nullptr;
                                  if (FAILED(args->get_Uri(&uri)) || !uri) return S_OK;
                                  std::wstring where = uri;
                                  CoTaskMemFree(uri);
                                  const bool ours =
                                      where.rfind(L"https://uniloader.local/", 0) == 0 ||
                                      where == L"about:blank";
                                  if (!ours) {
                                    args->put_Cancel(TRUE);
                                    ShellExecuteW(nullptr, L"open", where.c_str(),
                                                  nullptr, nullptr, SW_SHOWNORMAL);
                                  }
                                  return S_OK;
                                })
                                .Get(),
                            &navigated);
                      }
                      // The folder served as https://uniloader.local, which is
                      // what gives the page an origin YouTube will embed into.
                      ComPtr<ICoreWebView2_3> mapping;
                      if (g_player.view &&
                          SUCCEEDED(g_player.view.As(&mapping)) && mapping) {
                        mapping->SetVirtualHostNameToFolderMapping(
                            kVirtualHost, g_folder.c_str(),
                            COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW);
                      }
                      g_player.ready = true;
                      // Hidden until something is played: the controller is
                      // created visible and would otherwise paint white over
                      // the gallery from the moment it comes up.
                      controller->put_IsVisible(g_player.showing ? TRUE : FALSE);
                      ApplyBounds();
                      // The player page, loaded once for the life of the
                      // program. Everything after this is a script call into
                      // it; the pending requests flush when it reports in.
                      Navigate(std::wstring(L"https://") + kVirtualHost +
                               L"/player.html");
                      // Told to the window, because priming could not happen
                      // before now: the gallery was showing its video before
                      // the engine finished booting, and a PrimeVideo from
                      // back then fell on deaf ears. This is the difference
                      // between the first click playing at once and the first
                      // click waiting out the whole boot.
                      PostMessageW(g_player.parent, WM_UL_PLAYER_READY, 0, 0);
                      return S_OK;
                    })
                    .Get());
            return S_OK;
          })
          .Get());
  // A synchronous failure is the machine saying the runtime is not installed.
  if (FAILED(started)) g_player.failed = true;
}

bool VideoPlayerReady() { return g_player.ready; }

bool VideoPlayerFailed() { return g_player.failed; }

bool VideoPlaying() { return g_player.showing; }

void PlayVideo(const std::string& video_id, const std::string& list_id,
               const RECT& rect) {
  g_player.bounds = rect;
  g_player.showing = true;
  if (!g_player.ready || !g_player.page_ready) {
    // Remembered, and played the moment the page is up.
    g_player.play_pending = true;
    g_player.prime_pending = false;
    g_player.pending_id = video_id;
    g_player.pending_list = list_id;
    return;
  }
  ApplyBounds();
  // show() plays the item's own embed: buffered and paused by the priming in
  // the common case — near-instant — and freshly loaded when the click beat
  // the warm-up, with the play command repeating until the boot catches up.
  Script(L"show", video_id, list_id);
  g_player.controller->put_IsVisible(TRUE);
  g_player.controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
}

void PrimeVideo(const std::string& video_id, const std::string& list_id) {
  if (g_player.failed) return;
  if (video_id.empty() && list_id.empty()) return;
  if (!g_player.ready || !g_player.page_ready) {
    if (!g_player.play_pending) {
      g_player.prime_pending = true;
      g_player.pending_id = video_id;
      g_player.pending_list = list_id;
    }
    return;
  }
  Script(L"load", video_id, list_id);
}

void StopVideo() {
  g_player.play_pending = false;
  g_player.prime_pending = false;
  if (!g_player.showing) return;
  g_player.showing = false;
  if (!g_player.ready) return;
  g_player.controller->put_IsVisible(FALSE);
  // Paused rather than unloaded: a hidden WebView2 goes on playing, so the
  // pause is what silences it — and keeping the embed is what lets the same
  // video resume where it was when the gallery comes back to it.
  Script(L"hide", "", "");
}

void ResetVideoPlayer() {
  // A different plugin is a different gallery: its embeds, their positions and
  // their buffers describe things no longer on offer, and four of them held
  // forever would be four embeds too many.
  Script(L"clearAll", "", "");
}

void MoveVideoPlayer(const RECT& rect) {
  g_player.bounds = rect;
  if (g_player.showing) ApplyBounds();
}

void DestroyVideoPlayer() {
  if (g_player.controller) g_player.controller->Close();
  g_player.view.Reset();
  g_player.controller.Reset();
  g_player.ready = false;
  g_player.showing = false;
}

}  // namespace ulwin

#endif  // UL_NO_WEBVIEW2

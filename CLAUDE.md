# Working in this repository

One C++17 core behind a plain C ABI, and a Win32 client over it. There is no
second implementation of anything — if a rule about versions, packages, plugins
or uninstalling changes, it changes in `src/UniLoaderCore/`. Nothing in the core
may depend on Win32.

## Build and test

```powershell
cmake -S src -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target UniLoaderWin -- -m   # build/UniLoaderWin/Release/Unification Mod Loader.exe
```

```sh
cmake -S src -B build && cmake --build build     # core + tests, anywhere
ctest --test-dir build -j4
./build/Release/ul_tests . --group plan --filter uninstall
```

Seven groups. Six of them check a decision and run anywhere; `install` checks
the consequences and is Windows-only, because running a plan is the host's job.

## The split, and why it is where it is

**The core decides; the host acts.** Anything with an answer that could be wrong
— which release is newer, where its archive actually is, what a plugin folder
contains, which files an install must touch and in what order — is in the core,
in portable C++17, tested without a window or a network. The client downloads,
walks directories, copies bytes and draws.

**A plan is built, then run.** Nothing is copied or deleted by the function that
decided to. That split is what makes uninstall provably exact: the plan comes
from the receipt of what was installed, so it can only name files the manager
wrote. A stock War2Combat has `AutoWarLat.w2p`, `CpuSaveC.w2p`, `PlaySound.w2p`
and `lobby_map.w2p` in `plugin\` before this program has done anything, and an
uninstall that matched on a pattern or an extension would take them.
`install.uninstall_leaves_the_game_folder_untouched` is the test that says so.

**One exception, stated in the ABI.** `ul_archive_*` touches a disk, because a
package is hundreds of megabytes and a rule that made the host hold one in
memory to pass it across the boundary would be a rule against working on the
machines this program exists for. Nothing else in the core opens a file.

## Traps that have cost real time

- **Grep the build for `error`, not `error C`.** A resource compiler failure is
  `error RC2151`, and a narrower pattern reports a clean build while the exe
  quietly stays at its last good version. Two screenshots were taken of a
  binary that predated the feature being looked for.
- **String ids are not free below 300.** `IDS_ACTION_*` sits at 220-231 and the
  resource compiler refuses a reused id outright. New blocks go at 290+.
- **The exe can be stale when the build says it succeeded.** Compare timestamps
  before believing a screenshot; if the exe is older than the lib, build again.
- **A running `Unification Mod Loader.exe` blocks the next link** (`LNK1104`).
  Build and run the `UniLoaderTest` copy target instead.
- **Do not steal focus when verifying.** Screenshot with `PrintWindow`, drive
  with `PostMessage`; someone is usually at the keyboard. Reading a control
  across processes is safe below `WM_USER` (`LB_GETTEXT`, `WM_GETTEXT`,
  `CB_GETLBTEXT`) and is a crash at or above it — the pointer is not marshalled.
- **`--game` is honoured or refused, never substituted.** It used to fall back
  to the discovered install when the named folder was not a game folder, so one
  mistimed test setup wrote 1.2 GB into the real War2Combat instead of a
  sandbox. Check the folder exists *before* launching.
- **Never test against the real `C:\Program Files (x86)\War2Combat`.** Build a
  pretend one in the temporary directory, as `test_install.cpp` does. There is a
  real install on this machine and it belongs to somebody.
- **The package is 803 MB.** A live end-to-end run is worth doing and is not
  worth doing casually; the store it fills is under LOCALAPPDATA.
- **Two probes lie about a running client, and both cost an hour.**
  `PrintWindow` does not send `WM_DRAWITEM`, so an owner-drawn list comes back
  blank in the capture while the real window is fine — use `CopyFromScreen` over
  the window rectangle for anything owner-drawn. And a cross-process
  `GetWindowTextW` on the multiline description EDIT returns "" even when it has
  text. Read the listbox with `LB_GETTEXT`, and believe a screen grab over both.
- **The store holds the package exactly once.** After an install the bulk is in
  the game folder and only `Plugins/` stays behind; uninstalling *moves* the
  rest back rather than deleting it. Two copies of 1.2 GB is the bug on one
  side and an 800 MB re-download is the bug on the other. What stays behind is
  not a cache, whatever the folder is called: it is the plugin library the list
  is built from. A "delete downloaded files" button that took it emptied the
  list while leaving the mod installed, so the settings window offers only
  packages nothing is using and hides the row when there are none.
- **Never carry the package root forward from an old stamp.** `WriteStamp`
  recomputes it from the files that are there. Trusting a stale one installed a
  wrapped package *as* its wrapper — a whole mod one folder deep inside the
  game — and it looked like a successful install right up until nothing ran.
- **`ddraw.ini` says the same thing in two places, and the second one wins.**
  cnc-ddraw reads `[ddraw]` for defaults and then a section named after the
  running executable on top of them. War2Combat's install has `[Warcraft II
  BNE]`, and it sets `windowed` and `fullscreen` all over again. Writing only
  `[ddraw]` produced a clean diff, a correct-looking readback, and a game that
  started exactly as it had before. `ul_ini_set_everywhere` writes every section
  that already states the key — and only those, because adding a key to a
  section that never had one takes away an inheritance. This is the same trap as
  `war2_ddraw.ini` one level down: the wrong config fails silently, which is the
  worst way for a setting to fail.
- **A YouTube embed needs an origin, and a screen grab cannot see it.** Two
  traps in one feature. Navigating WebView2 straight at `youtube.com/embed/<id>`
  gets "Error 153, video player configuration error": the embed has to sit in a
  page on a real origin, so `Video.cpp` writes a `player.html` and serves it
  through `SetVirtualHostNameToFolderMapping` as `https://uniloader.local`. And
  WebView2 draws through DirectComposition, which `CopyFromScreen` *and*
  `PrintWindow` both miss — both came back showing the GDI+ panel underneath
  while the player was up and working. Read the child window list
  (`Chrome_WidgetWin_1` over the panel's rect) before believing either.
- **The main window needs `WS_CLIPCHILDREN` and `CS_HREDRAW | CS_VREDRAW`, and
  `Layout` moves everything in one `DeferWindowPos` batch.** Without the first
  two, a drag-resize smears the old layout over the new one; without the batch,
  fifteen `MoveWindow` calls are fifteen paints and the window flickers through
  every one of them. `MoveVideoPlayer` is called *after* `EndDeferWindowPos`,
  from the computed numbers rather than from `ShotRect()`, because the panel has
  not actually moved until the batch lands.
- **The gallery panel and the player share a rectangle.** Only one is shown at a
  time, by hiding the panel — z-order is not enough, because every layout pass
  moves the panel and a `MoveWindow` may raise it over a playing video.
- **A nine-patch guide border is pure black on nothing, not "dark".** The
  button plaque's hand-drawn outer ring is `2C1A00`, which an `R,G,B < 64` test
  read as Android 9-patch guide marks: `border` became 1, the margins came out
  as nonsense, and the buttons lost their frame and sat a pixel off — while
  still looking like a plausible button. A real `.9.png` border row is
  transparent apart from the marks, so that is what `HasGuideBorder` checks.
  The symptom looked like a stretching bug and is not one; before touching
  `DrawNinePatch`, dump a drawn button's corner and compare it to `ui.png`,
  where a correct patch matches pixel for pixel.
- **Buttons are drawn as a true nine-patch: nothing is ever scaled.** Corners
  land once at their drawn size, edges repeat along the one axis they are the
  middle of, the centre repeats both ways, and a piece running past its cell is
  cut by the clip. Scaling any of it — even proportionally — is the thing that
  was asked for and rejected.
- **`UniLoaderTest.exe` re-executes as `Unification Mod Loader.exe`**, so the
  process you started exits with code 0 and its `MainWindowHandle` is useless.
  Find the window by enumerating top-level windows of *any* process running out
  of the build folder. It also means the test copy still locks the real exe, so
  a leftover instance is an `LNK1104` on the next build.
- **A screen grab catches whatever is on top.** `CopyFromScreen` over the
  window rect returned a picture of the terminal sitting over the app. Bring
  the window to the front and let it settle before capturing.
- **Backslashes do not survive a heredoc through the Bash tool.** `L'\\'` has
  arrived in a file as `L'\'` more than once. Write C++ with the file tools.

## Rules

- **Bump the patch in `version.h` on every commit.** One place feeds the title
  bar, About, `--version` and `VERSIONINFO`. Pushing a new version to `master`
  *is* the release — CI tags it and uploads the exe — so a change worth shipping
  also earns a `CHANGELOG.md` section, which is what the release notes are made
  of: short bullets about what is different for a user, not the reasoning, which
  belongs in the commit and the code.
- **User-visible strings live in `Strings.rc`**, never in a `.cpp`. Sources and
  resources are UTF-8; the client is Unicode.
- **Retired ids stay retired.** An id that stops being used is left unused with
  a comment, never reassigned.
- **Comments say why, not what, and say it once.** Evidence — an endpoint that
  was tried and what it answered, a real file name, a size — is not narration
  and stays.
- **The C ABI is the boundary.** No C++ types, no exceptions crossing it.
- **Test against what the services really send.** `test/fixtures/` holds real
  GameBanana responses, the real 197-byte pointer archive, and the real OneDrive
  redeem and folder listing. A fixture invented to make a test pass proves
  nothing about a service nobody here owns. Credentials in a captured response
  are redacted: onedrive-children.json has its tempauth taken out.

## Where things are

| Path | |
|---|---|
| `UniLoaderCore/include/uniloader/uniloader.h` | the ABI, and where the reasoning about the release chain is written down |
| `UniLoaderCore/release.cpp` | the GameBanana mod page: version, author, changelog, and the ranked list of places the package might be |
| `UniLoaderCore/url.cpp` | OneDrive: the three-request listing of the shared folder, and picking the newest archive out of it |
| `UniLoaderCore/catalogue.cpp` | `Plugins/` as a catalogue: the folder names the plugin, its `info.txt` is prose (description + video/playlist links, never fields), plus the `.w2p` *alternatives* — and `base/` as the mod describing itself. `docs/plugin-info.md` says it for the mod author |
| `UniLoaderCore/plan.cpp` | install, plugin switch, uninstall — as lists of steps. `kPluginDestFolder` is `plugin`, which the package's own `mod description.txt` says and the game folder confirms |
| `UniLoaderCore/receipt.cpp` | what was written, so it can be taken back |
| `UniLoaderCore/archive.cpp` | the vendored UnRAR, behind four functions |
| `UniLoaderWin/main.cpp` | `struct App` — layout, the worker thread, and when to ask for administrator rights |
| `UniLoaderWin/Files.cpp` | walking, and running a plan. Win32 but window-free, which is why the tests can compile it |
| `UniLoaderWin/Slideshow.cpp` | the screenshot panel, through GDI+ |
| `UniLoaderWin/Video.cpp` | the inline player: WebView2, and the local page that gives the YouTube embed an origin |
| `UniLoaderWin/Display.cpp` | War2Combat's own display config: which of its two INIs is live, and reading three values out of it |
| `UniLoaderCore/ini.cpp` | an INI editor that changes only the keys asked for — the file belongs to the game |
| `test/fixtures/ddraw.ini` | the real config off this machine's War2Combat, so the display tests run against the thing itself |
| `UniLoaderWin/Strings.rc` | every sentence the program says |

**One resource is build output**: `app.ico`, from `app-icon-src.png`. Edit the
source, not the output. It is checked in because the conversion needs PowerShell
and cannot run on a cross-build.

## The OneDrive listing

Listing dannyldd's OneDrive folder takes three requests — an anonymous
`Badger` token from `api-badgerp.svc.ms`, a **POST** to the shares endpoint
carrying **`Prefer: autoredeem`**, and then the children — the way the OneDrive
web client lists it. `url.cpp` describes all three. Keep any commentary about
this mechanism to what the code does, not how it compares to other endpoints.

Which archive to take is a separate decision and a deliberately dull one:
**newest by date**, with the mod page's version used only to narrow the field
first. Nothing leans on a naming convention, because the folder has three
subfolders of old packages in it precisely because the naming has changed
before.

Expect the listing to break one day, and make sure it breaks *loudly*: the
source list is ranked so that a direct link to the package file on the mod page
(`UL_SOURCE_ALTERNATE`) is preferred over all of it, and that is the fix to ask
dannyldd for.

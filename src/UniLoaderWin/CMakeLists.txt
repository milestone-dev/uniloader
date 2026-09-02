# The Windows client.
#
# Only ever built on Windows; the parent guards this whole directory on WIN32 so
# a configure on macOS or Linux still gets the core and its tests.

# ------------------------------------------------------------------- artwork
#
# app.ico is build output that happens to be checked in: app-icon-src.png is the
# original, and editing the .ico by hand is editing the wrong file. Rebuilt
# whenever the PNG is newer, so redrawing the icon and pressing build is the
# whole of the work — before this the conversion was a script somebody had to
# remember, which meant the exe could ship yesterday's drawing while the PNG in
# the tree showed today's, a stale artefact that looks exactly like a bug.
#
# Converted rather than embedded raw because rc.exe understands .ico and
# understands no PNG.
find_program(UL_POWERSHELL NAMES pwsh powershell)
if(UL_POWERSHELL)
  add_custom_command(
    OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/app.ico"
    COMMAND "${UL_POWERSHELL}" -NoProfile -ExecutionPolicy Bypass
            -File "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/make-icon.ps1"
            -Sheet "${CMAKE_CURRENT_SOURCE_DIR}/app-icon-src.png"
            -Out "${CMAKE_CURRENT_SOURCE_DIR}/app.ico"
    # The script is a dependency as well as the drawing: changing how a size is
    # cut has to rebuild the artefact just as redrawing does.
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/app-icon-src.png"
            "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/make-icon.ps1"
    COMMENT "Converting app-icon-src.png -> app.ico"
    VERBATIM)
  # A target rather than a source of the client: what consumes the .ico is the
  # resource compiler, and CMake cannot see inside a .rc to know that. An
  # ordering dependency is the honest way to say it.
  add_custom_target(UniLoaderArt DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/app.ico")
else()
  message(STATUS
    "No PowerShell found: using the checked-in app.ico as it is. Re-run "
    "scripts/make-icon.ps1 on a Windows machine after changing app-icon-src.png.")
endif()

# -------------------------------------------------------------------- client

add_executable(UniLoaderWin WIN32
  main.cpp
  Dialogs.cpp
  Display.cpp
  Files.cpp
  GameData.cpp
  Net.cpp
  Slideshow.cpp
  Strings.cpp
  Video.cpp
  UniLoader.rc)

target_link_libraries(UniLoaderWin PRIVATE
  uniloader
  comctl32     # the progress bar
  shell32      # ShellExecuteEx, for Play and for the elevated relaunch
  ole32        # the folder picker
  shlwapi
  winhttp      # every fetch
  gdiplus)     # the screenshots

# ------------------------------------------------------------------ WebView2
#
# Only for the inline video player. The SDK is vendored under third_party the
# way UnRAR is, and the *loader* is the static one on purpose: the alternative
# ships a WebView2Loader.dll beside the exe, and this program is one file.
#
# What is not vendored, and cannot be, is the Evergreen runtime — Edge's engine,
# already present on Windows 11 and on any Windows 10 with a current Edge. Its
# absence is handled at runtime: the player never comes up, and a video click
# opens a browser as it did before. Nothing here is required to start the
# program, which is why the loader must be linked and not the runtime.
if(MSVC)
  # The loader is vendored per architecture: x64 from the original vendoring,
  # x86 from SDK 1.0.4191.47. The loader's job is only to find the installed
  # Evergreen runtime, so mixed SDK versions here are fine — the headers the
  # client compiles against are the vendored ones either way.
  if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(UL_WEBVIEW2_ARCH x64)
  else()
    set(UL_WEBVIEW2_ARCH x86)
  endif()
  target_include_directories(UniLoaderWin PRIVATE
                             "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/webview2/include")
  target_link_libraries(UniLoaderWin PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/webview2/${UL_WEBVIEW2_ARCH}/WebView2LoaderStatic.lib"
    version)   # what the static loader uses to find the installed runtime
else()
  # MinGW cannot build against the SDK's headers, which are MSVC COM. The
  # client still builds; the player is simply not in it.
  target_compile_definitions(UniLoaderWin PRIVATE UL_NO_WEBVIEW2)
endif()

# The exe is named what the release ships as — one name everywhere, spaces
# and all. Where a shell command touches it, quote it.
set_target_properties(UniLoaderWin PROPERTIES OUTPUT_NAME "Unification Mod Loader")

# The client is a Unicode application and says so rather than relying on the
# toolchain to assume it. Visual Studio defines these for new projects and MinGW
# does not, and the difference is not a compile error: the generic macros quietly
# resolve to their ANSI forms, so a wide string handed to a control shows up as
# its first letter.
# NOMINMAX for the same reason: windows.h defines min and max as macros, so
# every std::max( parses as a macro call with a stray '::' in front of it.
target_compile_definitions(UniLoaderWin PRIVATE UNICODE _UNICODE NOMINMAX
                           WIN32_LEAN_AND_MEAN)

# The resource scripts are UTF-8 and both resource compilers assume the system
# codepage unless told. Without this the em dashes and the ellipsis in
# Strings.rc reach the screen as three characters each.
if(MSVC)
  string(APPEND CMAKE_RC_FLAGS " /c65001")
else()
  string(APPEND CMAKE_RC_FLAGS " --codepage=65001")
endif()

if(TARGET UniLoaderArt)
  # Before the resource compiler runs, so the .rc always embeds an icon built
  # from the PNG as it is now.
  add_dependencies(UniLoaderWin UniLoaderArt)
endif()

if(MSVC)
  # UniLoader.rc embeds the manifest itself, as resource 1. MSVC's linker
  # generates one as well and then refuses to merge two: CVT1100, duplicate
  # resource. Keep the checked-in one — it is the same file both toolchains
  # read — and turn the other off.
  target_link_options(UniLoaderWin PRIVATE /MANIFEST:NO)
endif()

if(MINGW)
  # The entry point is wWinMain. MSVC's CRT finds it by itself; MinGW links the
  # ANSI startup, expecting WinMain, unless told the app is Unicode.
  target_link_options(UniLoaderWin PRIVATE -municode)
endif()

# The same binary under a second name, for anyone who wants to *run* the client
# while still being able to build it. Windows holds an executable open for as
# long as it is running, so a live "Unification Mod Loader.exe" fails the next
# link with LNK1104 and there is nothing the build can do about it.
add_custom_target(UniLoaderTest ALL
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          $<TARGET_FILE:UniLoaderWin>
          $<TARGET_FILE_DIR:UniLoaderWin>/UniLoaderTest.exe
  DEPENDS UniLoaderWin
  COMMENT "Copying the client to UniLoaderTest.exe")

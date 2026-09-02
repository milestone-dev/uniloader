# Build for the shared core, its tests and the Windows client.
#
# The core is platform-independent C++17 and builds anywhere; the client is
# guarded on WIN32. Same shape as PUDForge, and for the same reason: the parts
# worth testing are the parts that do not need a game, a network or a window.
#
#   cmake -S src -B build && cmake --build build && ctest --test-dir build

cmake_minimum_required(VERSION 3.16)
project(uniloader LANGUAGES CXX)

if(MSVC)
  # fopen and friends are portable and used correctly; MSVC's "unsafe" advice
  # would otherwise bury real warnings, and the vendored decoder is full of them.
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
  # /MT rather than /MD: the difference between "unzip and run" and "install the
  # Visual C++ redistributable first", and the second one loses people — which
  # matters more here than in most programs, because the whole point of this one
  # is that installing something is not supposed to be the hard part.
  # Set before any target is declared: the runtime is recorded in every object
  # file, and linking a /MD core into a /MT executable is a hard LNK2038.
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ------------------------------------------------------- the vendored decoder
#
# Alexander Roshal's UnRAR, built as a static library from the file set its own
# UnRARDll project lists. Its licence is in thirdparty/unrar/license.txt and its
# central condition is quoted in the repository's LICENSE; it permits use for
# extraction, which is all this does.
#
# Its own target so that its warnings, its exception model and its defines stay
# out of the core's. It is third-party code that compiles clean under its own
# makefile and noisily under anyone else's, and a wall of warnings nobody can
# act on is a wall that hides the ones somebody can.
set(unrar_dir ${CMAKE_CURRENT_SOURCE_DIR}/UniLoaderCore/thirdparty/unrar)
add_library(unrar STATIC
  ${unrar_dir}/archive.cpp    ${unrar_dir}/arcread.cpp   ${unrar_dir}/blake2s.cpp
  ${unrar_dir}/cmddata.cpp    ${unrar_dir}/consio.cpp    ${unrar_dir}/crc.cpp
  ${unrar_dir}/crypt.cpp      ${unrar_dir}/dll.cpp       ${unrar_dir}/encname.cpp
  ${unrar_dir}/errhnd.cpp     ${unrar_dir}/extinfo.cpp   ${unrar_dir}/extract.cpp
  ${unrar_dir}/filcreat.cpp   ${unrar_dir}/file.cpp      ${unrar_dir}/filefn.cpp
  ${unrar_dir}/filestr.cpp    ${unrar_dir}/find.cpp      ${unrar_dir}/getbits.cpp
  ${unrar_dir}/global.cpp     ${unrar_dir}/hash.cpp      ${unrar_dir}/headers.cpp
  ${unrar_dir}/isnt.cpp       ${unrar_dir}/largepage.cpp ${unrar_dir}/match.cpp
  ${unrar_dir}/motw.cpp       ${unrar_dir}/options.cpp   ${unrar_dir}/pathfn.cpp
  ${unrar_dir}/qopen.cpp      ${unrar_dir}/rar.cpp       ${unrar_dir}/rarvm.cpp
  ${unrar_dir}/rawread.cpp    ${unrar_dir}/rdwrfn.cpp    ${unrar_dir}/rijndael.cpp
  ${unrar_dir}/rs.cpp         ${unrar_dir}/rs16.cpp      ${unrar_dir}/scantree.cpp
  ${unrar_dir}/secpassword.cpp ${unrar_dir}/sha1.cpp     ${unrar_dir}/sha256.cpp
  ${unrar_dir}/smallfn.cpp    ${unrar_dir}/strfn.cpp     ${unrar_dir}/strlist.cpp
  ${unrar_dir}/system.cpp     ${unrar_dir}/threadpool.cpp ${unrar_dir}/timefn.cpp
  ${unrar_dir}/ui.cpp         ${unrar_dir}/unicode.cpp   ${unrar_dir}/unpack.cpp
  ${unrar_dir}/volume.cpp
)
# RARDLL is what turns the console tool into the embedding API this calls, and
# it is not optional: without it dll.cpp is not compiled in and the sources
# expect a main(). SILENT stops the decoder writing to a console the client does
# not have.
target_compile_definitions(unrar PUBLIC RARDLL SILENT)
target_include_directories(unrar PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/UniLoaderCore)
if(MSVC)
  target_compile_options(unrar PRIVATE /w /EHsc)
else()
  target_compile_options(unrar PRIVATE -w)
  target_compile_definitions(unrar PUBLIC _FILE_OFFSET_BITS=64 _LARGEFILE_SOURCE)
  find_package(Threads REQUIRED)
  target_link_libraries(unrar PUBLIC Threads::Threads)
endif()

# ------------------------------------------------------------------- the core

if(MSVC)
  # /utf-8: the sources are UTF-8 and have no BOM, and MSVC reads a BOM-less
  # file as the system ANSI code page. GCC assumes UTF-8, so this breaks only
  # here — and it breaks silently, in the output rather than the build.
  add_compile_options(/W4 /permissive- /utf-8)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# One number, fed to the core's ul_version(). The *client* has its own in
# UniLoaderWin/version.h, and that is the one a release is tagged from.
set(UL_CORE_VERSION "0.1.0")
add_library(uniloader STATIC
  UniLoaderCore/ini.cpp
  UniLoaderCore/json.cpp
  UniLoaderCore/util.cpp
  UniLoaderCore/version.cpp
  UniLoaderCore/url.cpp
  UniLoaderCore/release.cpp
  UniLoaderCore/catalogue.cpp
  UniLoaderCore/plan.cpp
  UniLoaderCore/receipt.cpp
  UniLoaderCore/sha256.cpp
  UniLoaderCore/archive.cpp
)
target_compile_definitions(uniloader PRIVATE UL_CORE_VERSION="${UL_CORE_VERSION}")
target_include_directories(uniloader
  PUBLIC  UniLoaderCore/include
  PRIVATE UniLoaderCore
)
target_link_libraries(uniloader PRIVATE unrar)

# ------------------------------------------------------------------ the tests

enable_testing()

add_executable(ul_tests
  Tests/harness.cpp
  Tests/test_ini.cpp
  Tests/test_version.cpp
  Tests/test_release.cpp
  Tests/test_catalogue.cpp
  Tests/test_plan.cpp
  Tests/test_archive.cpp
  Tests/test_install.cpp
)
target_link_libraries(ul_tests PRIVATE uniloader)
target_include_directories(ul_tests PRIVATE UniLoaderCore)

# The install group runs a plan against a real filesystem, and running a plan is
# the host's job. The two host files with no window in them are compiled into
# the test binary so that the pipeline end to end — unpack, install, switch,
# uninstall — is covered by ctest rather than by somebody clicking. The same
# trick PUDForge uses for its Win32-free editor state, and the reason those two
# files touch no HWND.
if(WIN32)
  target_sources(ul_tests PRIVATE
    UniLoaderWin/Files.cpp
    UniLoaderWin/Strings.cpp)
  target_compile_definitions(ul_tests PRIVATE UNICODE _UNICODE NOMINMAX)
  target_link_libraries(ul_tests PRIVATE shell32 ole32 shlwapi)
endif()

# Registered per group so `ctest -j` runs them in their own processes and a
# failure names the group rather than burying it in one log.
foreach(group version release catalogue plan archive install ini display)
  add_test(NAME ${group}
           COMMAND ul_tests "${CMAKE_CURRENT_SOURCE_DIR}/.." --group ${group})
endforeach()

# --------------------------------------------------------------- the client
#
# Guarded so a configure on macOS or Linux still works: the core and its tests
# are cross-platform and this is the only Windows-only target.
if(WIN32 AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/UniLoaderWin/CMakeLists.txt)
  add_subdirectory(UniLoaderWin)
endif()

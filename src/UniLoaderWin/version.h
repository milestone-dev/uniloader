// The client's version, written once.
//
// Four places show it — the title bar, Help > About, `--version`, and the
// VERSIONINFO block Explorer reads — and the resource compiler preprocesses, so
// UniLoader.rc includes this header the same way the sources do.
//
// This is the *client's* version and not ul_version(), which is the core's.
// Only this one is shown; `--version` prints both, where a machine may be
// reading. Note that neither is the *mod's* version, which is dannyldd's and
// comes off the mod page — three numbers in one program is two too many to put
// in front of a user, so the one on screen is always the mod's.
//
// Bump the patch on every commit, the minor on a release worth naming. A
// version that only moves when someone remembers makes a stale build look
// current. CI reads UL_APP_VERSION_STR below and cuts a release the first time
// it sees a version it has no tag for, so a bump is what ships a build.

#pragma once

#define UL_APP_VERSION_MAJOR 1
#define UL_APP_VERSION_MINOR 0
#define UL_APP_VERSION_PATCH 11

// Spelled out rather than stringised from the three numbers above: rc.exe's
// preprocessor does not do the two-level stringisation that would build it, and
// a VALUE expanding to something other than a string literal is a syntax error.
#define UL_APP_VERSION_STR "1.0.11"
#define UL_APP_VERSION_WSTR L"1.0.11"

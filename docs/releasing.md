# Releasing

A release is a push to `master` with a version in `version.h` that has no tag
yet. There is no separate release process to remember, because a process nobody
runs often is a process nobody remembers.

## What CI does

`.github/workflows/ci.yml` builds, runs `ctest`, and then reads
`UL_APP_VERSION_STR` out of `src/UniLoaderWin/version.h`. If no `v<version>` tag
exists on the remote, it tags, creates the GitHub release, and uploads
`UniLoader.exe` to it. The release notes are `CHANGELOG.md`'s section for that
version, verbatim.

So:

- a commit that bumps the version **ships**;
- a commit that does not **just builds**.

Neither fails on a tag that already exists, which is what makes it safe to push
a fix without thinking about releases at all.

## Cutting one

1. **Bump `src/UniLoaderWin/version.h`.** The patch on every commit; the minor
   on a release worth naming. Both `UL_APP_VERSION_PATCH` and the two spelled-out
   strings — `rc.exe` cannot build the string from the numbers, so they are
   written out and have to move together.
2. **Write the `CHANGELOG.md` section.** Short bullets about what is different
   *for a user*. The reasoning belongs in the commit message and in the code.
3. **Push to `master`.**

## Before you push

```powershell
cmake --build build --config Release -- -m
ctest --test-dir build -C Release -j4 --output-on-failure
```

Then run it. The suite covers every decision and one real install cycle, but it
cannot see a window: check that it starts, that it finds the game, that the mod
page comes back, and that the plugin list and a screenshot draw.

**Compare timestamps before believing a screenshot.** MSBuild has left an
executable unrelinked against a newer library, which is a green suite against a
binary without the change in it. If `UniLoader.exe` is older than
`uniloader.lib`, delete it and build again.

**A running `UniLoader.exe` blocks the next link** with `LNK1104`. Run the
`UniLoaderTest.exe` copy the build makes for exactly this.

## Version numbers, of which there are three

Only one of them is ever on screen, and it is not either of the two below.

| | Where | Shown |
|---|---|---|
| the mod's | dannyldd's GameBanana page | everywhere in the window — it is what a user cares about |
| the client's | `UniLoaderWin/version.h` | the title bar, About, `--version`, `VERSIONINFO` |
| the core's | `UL_CORE_VERSION` in `src/CMakeLists.txt` | `--version` only, where a machine may be reading |

A window quoting three numbers would ask every bug report to explain which was
which.

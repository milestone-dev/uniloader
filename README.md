# Unification Mod Loader

**[⬇ Download the latest release](https://github.com/milestone-dev/uniloader/releases/latest)**

Unification Mod Loader — *UniLoader* for short, and that is the name the code
uses — is a Windows program for [dannyldd's War2 Unification
mod](https://gamebanana.com/mods/644456): it installs the mod into a
War2Combat install, keeps it up to date, and switches which of its plugins is
loaded.

## What it does

- **Checks for updates.** The version comes from the mod page on GameBanana, which dannyldd already keeps current as part of publishing. The changelog he writes there is shown alongside it, and someone three releases behind sees all three.
- **Downloads and installs.** Files the mod replaces are moved aside first, and what was written is recorded.
- **Switches plugins.** One at a time, with **No plugin** as a real choice. A plugin holding more than one `.w2p` — a difficulty pair, usually — gets a second dropdown, and only the chosen one is ever in the game folder.
- **Shows what each plugin is.** Its own read-me as the description, its screenshots, and its YouTube videos and playlists, playing inline.
- **Starts the game**, through the mod's own `Unification.exe`.
- **Uninstalls**, from the record of what was installed rather than by guessing. War2Combat ships four `.w2p` files of its own; nothing here can touch them.

## Building

Visual Studio 2022's C++ workload and CMake, from the repo root:

```powershell
cmake -S src -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target UniLoaderWin -- -m
```

The result is `build/UniLoaderWin/Release/Unification Mod Loader.exe`. It links the C runtime statically, so it is one file that runs without anything installed alongside it.

The core and its tests build anywhere:

```sh
cmake -S src -B build && cmake --build build
ctest --test-dir build -j4
```

## Tests

```sh
./build/Release/ul_tests . --group plan
```

Six groups check decisions and one checks consequences. `install` unpacks a package fixture into a pretend War2Combat in the temporary directory, installs it, switches plugin and variant, uninstalls, and then asserts the folder is byte for byte what it was — including the four stock `.w2p` files that were never the mod's.

`release` and `archive` run against the real responses saved under `test/fixtures/`. A test written against a document invented to make it pass proves nothing about a service nobody here owns.

## Layout

```
src/UniLoaderCore/    the core — every decision, behind a C ABI
  include/uniloader/uniloader.h   the ABI itself
  thirdparty/unrar/               Alexander Roshal's UnRAR, for extraction only
src/UniLoaderWin/     the Win32 client: the window, the network, the filesystem
src/Tests/            the suite — one file per subject area
test/fixtures/        real API responses, the real pointer archive, a package
scripts/              the icon converter the build calls
```

## Credits

- **[dannyldd](https://gamebanana.com/members/1738346)** — the Unification mod, and every sub-mod inside it.
- **[war2.ru](https://en.war2.ru/)** — War2Combat, and the server this is all played on.
- **Alexander Roshal** — UnRAR, vendored under `src/UniLoaderCore/thirdparty/unrar`.

Warcraft II and all related intellectual property are owned by Blizzard Entertainment. This project is an unofficial fan-made tool and is not affiliated with, endorsed by, or sponsored by Blizzard Entertainment, war2.ru, or GameBanana.

## Licence

MIT, with one exception it names: the vendored UnRAR keeps Alexander Roshal's own terms, quoted in full in [`LICENSE`](LICENSE) and reproduced in `src/UniLoaderCore/thirdparty/unrar/license.txt`. UniLoader uses it to extract, never to compress.

No Warcraft II data, and no part of the Unification mod, is included in this repository.

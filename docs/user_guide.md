# UniLoader

UniLoader keeps [dannyldd's War2 Unification mod](https://gamebanana.com/mods/644456) up to date in your War2Combat install, and switches which of its plugins is loaded.

Run `UniLoader.exe`. There is nothing to install and nothing to configure.

## The window

**Top line** — the mod's name, the version that is currently published, and who made it. On the right, two buttons: **Changelog** and **Settings**.

**Second line** — what UniLoader knows about versions, and the button that acts on it, with the published version repeated immediately to its left so the button says what it is about. It checks GameBanana as soon as it starts, so this is filled in before you have done anything.

| It says | The button says | What happens |
|---|---|---|
| *v6.6 is available. Nothing is installed yet.* | **Install** | downloads and installs it |
| *v6.6 is available. You have v6.5.* | **Update** | downloads and installs the newer one |
| *v6.6 is installed, and it is the latest version.* | **Check again** | asks GameBanana again |

While something is downloading the button becomes **Cancel**. Cancelling leaves nothing behind — a half-finished download is deleted rather than kept, so the next attempt starts clean instead of installing a mixture of two versions.

**The list** — every plugin, with what kind of thing it is down the right: *Campaign*, *Difficulty mod*, and so on. The one the game is currently loading is in bold with a bar down its edge.

**No plugin** is the first row and is a real choice: Unification with none of the sub-mods loaded. Start there if you have not played it before, or come back to it to rule a plugin out — choosing it removes every plugin file the mod knows about from the game folder, so it also cleans up after anything that went wrong.

**The description** under the list is the plugin's own words, as dannyldd wrote them — usually including which numbered file is the harder difficulty.

**The picture** — the screenshots and videos that plugin ships with. Click a screenshot, or use ◀ and ▶, to step through them; click a video to watch it in your browser. A plugin with neither says so.

## Turning a plugin on

**Clicking a plugin in the list turns it on.** That is the whole of it — there is no confirm step and no second button. The one that is on is shown in bold with a bar down its edge, and the game will load it the next time it starts.

Clicking **No plugin** turns whatever was on back off.

**The dropdown under Play** appears when a plugin ships more than one version of itself — usually a difficulty, like *plugin trolls 1* and *plugin trolls 2*. Twelve of the eighteen plugins do. Changing it swaps the version straight away, the same as clicking a plugin does. Its space is kept even when a plugin has only one version, so the window does not shuffle as you move down the list.

## Playing

**Play** starts the game, and says what it is about to play: *Play Troll Wars*. It sits on its own at the bottom right because it is the one thing on this screen you actually do.

By the time you press it the plugin is already loaded, so it just starts the game. If a load did not happen — the game was open at the time, say — Play does it first and then starts.

The game is started through the mod's own `Unification.exe`. Without the mod installed, Play falls back to War2Combat's own launcher.

## Changelog

dannyldd's own notes for every release, newest first — the whole history of the mod, not just what you have missed. Worth a look before taking an update, and worth a look before a first install.

## About

Who made the mod, which version is published, and two buttons out to GameBanana: the **Mod page**, where the version and the changelog come from and where to leave a comment, and **About dannyldd**, who wrote all of it.

## Settings

**Display** — the game's own settings, at the top:

- **Screen**: *Fullscreen* changes the monitor's mode; *Borderless window* fills the screen without changing it, which is usually the one you want; *Windowed* is a window.
- **Smoothing**: the filter used when the 640x480 picture is scaled up. *None* gives sharp pixels; the rest are the shaders War2Combat ships.
- **Keep the original 4:3 shape**: off, the picture is stretched across a widescreen monitor.

These are the *game's* settings, not the mod's. They stay put when you install or uninstall Unification, and UniLoader changes only the lines it shows you — the rest of the file is left exactly as it was, with a copy of the original kept the first time it touches it.

Then the things that are not about playing:

- **Where War2Combat is.** UniLoader finds it by itself — where it was last told, the folder `UniLoader.exe` is sitting in, the registry, and the usual install paths — so dropping `UniLoader.exe` into your game folder is enough on its own. **Change…** points it somewhere else.
- **Where UniLoader keeps the mod and its backups.**
- **Uninstall**, which puts War2Combat back the way it was. It is greyed out when there is nothing installed.
- The version of UniLoader itself, which is the number to quote in a bug report.

## What uninstalling actually does

UniLoader writes down every file it copies into your game folder, and whether something was already there. Uninstalling reads that list back:

- a file UniLoader added is deleted;
- a file UniLoader replaced is put back exactly as it was;
- **a file UniLoader did not write is not touched.**

That last point matters more than it sounds. A stock War2Combat already has `AutoWarLat.w2p`, `CpuSaveC.w2p`, `PlaySound.w2p` and `lobby_map.w2p` in its `plugin` folder. They are the game's, not the mod's, and nothing UniLoader does can remove them.

Your saved games, maps and settings are never touched at all.

## Things it will tell you

**"Warcraft II is running."** Close it first. Windows will not let a file be replaced while a program has it open, and the error it gives for that names a sharing violation rather than the game, which sends people looking in the wrong place.

**"Writing to … needs administrator rights."** Your War2Combat is under `Program Files`, which is protected. Say yes to the prompt that follows and UniLoader restarts with the rights it needs and carries on from where it was. A portable War2Combat never asks.

**"The OneDrive folder on the mod page could not be read."** UniLoader finds the package by reading dannyldd's OneDrive folder the same way the OneDrive website does, and Microsoft changes how that works from time to time. Use **Mod page** to download the package the usual way, and please report it — it is fixable, and it is fixed for everyone at once.

## Where UniLoader keeps things

`%LOCALAPPDATA%\UniLoader`:

- `package\` — the unpacked mod, with the whole plugin catalogue in it. Plugins are kept here and copied into the game one at a time, which is what makes switching quick.

  Once the mod is installed this folder is small — a release is about 800 MB, and nearly all of it is now in the game folder, so only the plugins stay behind. They are not a spare copy of anything: they are where the plugin list comes from and where a plugin is copied from when you pick one. Uninstalling moves the rest back here, so changing your mind afterwards does not mean downloading 800 MB again.

  **Settings** offers to delete a download only when there is one nothing is using — a release you never installed, or one a newer version replaced. It never offers to delete the installed one, because that would empty the plugin list and cost a download to undo.
- `backup\` — the files the install replaced.
- `install.json` — the record of what was installed, and which plugin you chose.

Kept here rather than in the game folder on purpose: if you delete your game folder you have uninstalled already, and if you reinstall the game over the top you have not.

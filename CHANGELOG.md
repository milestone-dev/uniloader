# Changelog

What changed in each release, in the words a user would use. This file is what
the GitHub release notes are made of.

## 1.0.0

First release.

- Checks GameBanana for the current version of the Unification mod on startup,
  and shows dannyldd's own changelog for every release you have missed.
- Downloads and installs the package. Files it replaces are set aside first.
- Lists the mod's plugins with the name and category each one gives itself, and
  marks the one the game is loading. **No plugin** is one of the choices, and
  choosing it clears every plugin file the mod knows about out of the game —
  including anything a mistake left behind.
- Shows a plugin's own description, screenshots and YouTube videos, straight
  from its folder: it is named after its folder, the `info.txt` read-me is the
  description, shown as the author wrote it; every image in the folder is a
  screenshot; and every YouTube link in the read-me plays in the gallery —
  short links, watch links, shorts, playlists, whatever form it was written
  in. See `docs/plugin-info.md`. A plugin with no `info.txt` works exactly the
  same, just with nothing to say about itself.
- Videos start instantly: the one on screen is loaded and buffered, muted and
  out of sight, before you press play — pressing play just turns the sound on
  and lets it go.
- A `base/` folder at the package root describes the mod itself the same way,
  shown for **No plugin** — read as soon as a package ships one.
- Videos play in the window, not in a browser. A plugin with no screenshots or
  video of its own shows a stand-in screen that says so, so the gallery is
  never an empty space.
- Refuses to change files Warcraft II has open, and says that is what happened
  rather than reporting a sharing violation.
- Offers a second choice for a plugin that ships more than one `.w2p` — a
  difficulty pair, usually. Only the one you pick reaches the game folder.
- Clicking a plugin turns it on. No confirm step, no second button; the one in
  use is shown in bold with a bar down its edge.
- **Play** starts the game through the mod's own `Unification.exe`, and says
  what it will play.
- Uninstalls from the record of what was installed: everything it copied in is
  removed, everything it replaced is put back, and nothing else is touched.
- Finds War2Combat by itself, and asks for administrator rights only when the
  install folder actually needs them.
- Reinstalling, or changing your mind after an uninstall, does not download the
  800 MB again.
- Links out to the mod page and to dannyldd's profile.
- **Settings** also holds War2Combat's own display options — fullscreen or
  windowed, whether the 4:3 picture keeps its shape, and the smoothing filter —
  the three settings worth having in front of you. Only those are changed; the
  rest of the file is left exactly as found — including the setting War2Combat
  keeps in a second place for Warcraft II specifically, which is written too, so
  what you choose is what the game starts up with.
- **Changelog** shows dannyldd's notes for every release the mod has had.
- **Settings** holds the game folder, where UniLoader keeps its files, and the
  uninstall — the things looked at about twice in a lifetime.

It follows the mod page all the way to the package on its own: the post, the
small file attached to it, the OneDrive link inside that, and the newest package
in the folder. dannyldd does not have to change anything he does.

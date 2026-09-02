# What UniLoader reads from a plugin folder

This is for the mod author. Nothing here has to be written: every plugin works
with no description at all.

## The pieces

For each folder under `Plugins/`, the launcher shows:

**The name** — the folder's own name, cleaned up. `3_Legacy of Dalaran` shows
as "Legacy of Dalaran", `0_basegame` as "Basegame". The number prefix is your
running order and is not shown; underscores become spaces. The name always
comes from the folder — rename the folder to rename the plugin.

**The description** — the folder's own `info.txt` (top level, not one inside a
subfolder), shown exactly as you wrote it, blank lines and paragraph breaks
and all. It is your read-me: write it for people, nothing in it is parsed
beyond the links.

**Screenshots** — every `.png`, `.jpg`, `.gif` or `.bmp` anywhere in the
folder, shown in **name order**. Number them `1.png`, `2.png`, `3.png` to
control the order — and use `01`, `02` if you have ten or more, or `10.png`
will sort before `2.png`. Shots are drawn at **960 × 544**; other sizes are
fitted to that shape with the aspect kept, so nothing gets stretched.

**Videos** — every YouTube link in the `info.txt`, wherever it sits in the
prose, joins the gallery ahead of the screenshots, with a play button, and
plays inside UniLoader. Any of the usual ways of writing one works:
`youtu.be/…` short links (share suffix and all), `watch?v=…` links, embeds,
shorts, live links — from `www.`, `m.`, `music.` or a country domain. A
**playlist** link works too and plays through as a playlist. A channel link
is not watchable and is left alone. The same link written twice shows once.

## The `base` folder — the mod describing itself

A folder called `base/` at the package root, beside `Plugins/`, does the same
job for the mod itself: its `info.txt` and screenshots are shown when
**No plugin** is selected — the mod with no sub-mod loaded. Same rules
as a plugin folder. No package ships one yet; the launcher reads it the day
one does. Nothing in `base/` is ever copied into the game folder.

## Files

Only the `.w2p` files are ever copied into War2Combat. Everything else in a
plugin folder — the `info.txt`, your notes, a link to the original thread, a
zip of the files a sub-mod started from — stays in the launcher's store and is
never installed.

If a folder holds more than one `.w2p`, they are treated as **alternatives**
and UniLoader shows a dropdown to choose between them. Exactly one is ever in
the game folder at a time, so switching from hard to normal cannot leave both
there. The dropdown is labelled with the file names, so name them helpfully.

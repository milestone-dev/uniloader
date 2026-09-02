// UniLoader's core, behind a plain C ABI.
//
// The core decides; the host acts. Everything that needs judgement — which
// release is newer, where its archive can actually be fetched from, what a
// plugin folder contains, which files an install or an uninstall must touch and
// in what order — is here, in portable C++17 with no Win32 and no network. The
// host downloads, reads bytes, writes bytes and draws.
//
// The one deliberate exception is the archive reader (ul_archive_*): a
// Unification package is hundreds of megabytes, and a rule that made the host
// hold one in memory to hand it across this boundary would be a rule against
// working on the machines this program exists for. It streams to a directory
// the host names. Nothing else here opens a file.
//
// Rules that hold across the whole ABI:
//   - No C++ types cross it, and no exceptions.
//   - Every string in and out is UTF-8, NUL-terminated.
//   - A `const char*` returned by a getter belongs to the object it came from
//     and dies with it. A `char*` returned by a builder is the caller's, freed
//     with ul_free.
//   - Every _create can return null, and every getter tolerates null.

#ifndef UNILOADER_H
#define UNILOADER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// The core's own version, which is not the client's. `--version` prints both.
const char* ul_version(void);

/// Frees a buffer the core allocated and handed back.
void ul_free(void* p);

enum {
  UL_OK = 0,
  UL_ERR_OPEN = 1,       // not there, or not an archive
  UL_ERR_FORMAT = 2,     // damaged, or a format this build cannot read
  UL_ERR_WRITE = 3,      // could not write where it was told to
  UL_ERR_CANCELLED = 4,  // the user stopped it
  UL_ERR_UNSAFE_PATH = 5,
  UL_ERR_ENCRYPTED = 6,  // password-protected, which a mod package never is
  UL_ERR_PARSE = 7,      // the server answered, but not with what was expected
  UL_ERR_NO_ARCHIVE = 8, // a release exists, but nothing says where to get it
};

/// A human-readable form of one of the codes above, for a message box.
const char* ul_error_text(int code);

// ---------------------------------------------------------------- versions
//
// Release versions in this corner of the world are not semver. dannyldd's
// releases read "v6.6", "v6.5", and older packages carried dates instead, so
// the comparison reads runs of digits and compares them numerically, ignoring
// everything else. "v6.10" is newer than "v6.9", which a string compare gets
// backwards — and that is not hypothetical, the mod is at v6.6 and counting.

/// <0, 0 or >0 as `a` orders before, with, or after `b`.
int ul_version_compare(const char* a, const char* b);

/// The version embedded in a file name, or null if it has none.
/// "war2_unif_v6_6.rar" -> "6.6", "Unification v3.4.1.rar" -> "3.4.1".
/// The caller frees it. Used to recognise the newest archive in a folder
/// listing, and to sanity-check a download against the release it claims to be.
char* ul_version_from_filename(const char* filename);

/// Whether `latest` is a release the machine does not have. `installed` is null
/// or "" when nothing is installed, which counts as an update.
int ul_update_available(const char* installed, const char* latest);

// ----------------------------------------------------------- the release
//
// What GameBanana says the current release is. dannyldd maintains the mod page
// as part of publishing — he bumps its version and writes a changelog entry per
// release — so reading it asks nothing new of him, and there is no second
// place for the two to disagree.
//
// Two documents, both anonymous, both from apiv11:
//   https://gamebanana.com/apiv11/Mod/{id}/ProfilePage
//   https://gamebanana.com/apiv11/Mod/{id}/Updates?_nPage=1&_nPerpage={n}
// The first carries the version, the author, the page URL and the attached
// files; the second carries the per-version release notes.

typedef struct ul_release ul_release;

/// Parses a ProfilePage document. Null if the bytes are not one; ul_last_error
/// then says why. `mod_id` is checked against the document so a wrong or
/// redirected response cannot be mistaken for the right mod.
ul_release* ul_release_parse(const char* json, size_t length, int64_t mod_id);
void        ul_release_free(ul_release* r);

/// Why the last parse returned null. Never null itself.
const char* ul_last_error(void);

const char* ul_release_mod_name(const ul_release* r);
const char* ul_release_version(const ul_release* r);        // "v6.6"
int64_t     ul_release_modified(const ul_release* r);       // unix seconds, 0 if none
const char* ul_release_page_url(const ul_release* r);       // the GameBanana post
const char* ul_release_author(const ul_release* r);         // "dannyldd"
const char* ul_release_author_url(const ul_release* r);
const char* ul_release_description(const ul_release* r);    // plain text, may be ""

/// Preview screenshots from the mod page, full-size URLs, in page order. Shown
/// before anything is downloaded, so the first screen is not empty.
int         ul_release_image_count(const ul_release* r);
const char* ul_release_image(const ul_release* r, int index);

// --- where the archive actually is -----------------------------------------
//
// GameBanana holds the version but not the payload: the package outgrew what
// the site accepts, so the file attached to the post is a 197-byte archive
// containing one line of text — a link to the OneDrive folder holding the real
// thing. That indirection is dannyldd's, it predates this program, and it is
// still the published route, so the core follows it rather than replacing it.
//
// Sources are tried in order and the first that yields an archive wins, which
// is what lets this keep working whichever way the publishing settles down:
//
//   UL_SOURCE_ALTERNATE  an alternate file source on the mod page whose URL
//                        looks like an archive or a OneDrive file. The smallest
//                        possible ask of dannyldd — one field on a page he
//                        already edits, no re-upload — and so the one to prefer.
//   UL_SOURCE_ATTACHMENT the file attached to the post, when it is big enough
//                        to be the package rather than a pointer to it.
//   UL_SOURCE_POINTER    the text inside that attachment. Works today, with
//                        nothing changed by anyone.
//   UL_SOURCE_FOLDER     a OneDrive *folder*, which must still be listed before
//                        anything can be fetched from it. See ul_folder_*.
enum {
  UL_SOURCE_ALTERNATE = 0,
  UL_SOURCE_ATTACHMENT = 1,
  UL_SOURCE_POINTER = 2,
  UL_SOURCE_FOLDER = 3,
};

/// Candidate archive locations, best first. The host walks them: fetches the
/// one it is looking at, and moves to the next if that turns out to be a dead
/// end. A pointer source's URL is fetched, unpacked, and any URL found in the
/// text inside is fed back through ul_release_add_pointer_text.
int         ul_release_source_count(const ul_release* r);
int         ul_release_source_kind(const ul_release* r, int index);
const char* ul_release_source_url(const ul_release* r, int index);
int64_t     ul_release_source_size(const ul_release* r, int index);  // -1 unknown

/// Feeds back the text found inside a pointer attachment. Every URL in it is
/// added as a new source, ranked after the ones already there. Returns how many
/// were found — zero means that lead was a dead end.
int ul_release_add_pointer_text(ul_release* r, const char* text, size_t length);

// --- release notes ----------------------------------------------------------

/// Parses an Updates document, attaching its entries to the release. Each entry
/// is one published version with the note dannyldd wrote for it. Returns UL_OK,
/// or UL_ERR_PARSE. The notes are HTML on the wire and plain text here.
int         ul_release_add_updates(ul_release* r, const char* json, size_t length);
int         ul_release_note_count(const ul_release* r);
const char* ul_release_note_version(const ul_release* r, int index);
const char* ul_release_note_text(const ul_release* r, int index);
int64_t     ul_release_note_date(const ul_release* r, int index);
/// Every note published after `installed`, joined — what a user who is behind
/// by several releases has missed, which is what the update screen should show
/// rather than only the newest line. The caller frees it.
char*       ul_release_notes_since(const ul_release* r, const char* installed);

// -------------------------------------------------------------------- urls
//
// A OneDrive share link is not a download link, and the shape of the rewrite
// has changed more than once. url.cpp holds every form seen, with a note on
// each and what it was tested against.

/// A share link rewritten into something a plain GET can fetch, or a copy of
/// the input when it is already direct. The caller frees it.
char* ul_direct_download_url(const char* share_url);

/// Whether a URL points at a folder rather than a file. A folder cannot be
/// downloaded and has to be listed first.
int ul_url_is_folder(const char* url);

// --- listing a shared OneDrive folder ---------------------------------------
//
// Three requests, in this order. The core builds each one and reads each
// answer; the host performs them, because the host is the only thing here that
// is allowed to touch a network.
//
//   1. POST ul_onedrive_badger_url() with ul_onedrive_badger_body()
//        -> ul_onedrive_read_badger()      an anonymous token, good for hours
//   2. POST ul_onedrive_item_url(share) with an empty body and two headers,
//      "Authorization: Badger <token>" and "Prefer: autoredeem"
//        -> ul_onedrive_read_item()        redeems the link; names the folder
//   3. GET  ul_onedrive_children_url(drive, item), same Authorization
//        -> ul_folder_pick_archive()       the newest package in it
//
// `Prefer: autoredeem` and the POST are the whole trick, and neither is
// documented anywhere: without them the same URL answers 403 accessDenied, and
// without the token it answers 401. See url.cpp for how this was found and what
// each step was verified against.

const char* ul_onedrive_badger_url(void);
/// The request body for step 1: a fixed JSON document naming the application
/// the token is for, carrying nothing about the user or the share.
///
/// There is more than one, tried in order until one is accepted, and `attempt`
/// selects between them — "" once they are exhausted. The web client picks
/// between the same two by build environment, and an application id is exactly
/// the sort of thing that gets retired, so having the second one costs a
/// retried request and buys a failure mode that fixes itself.
const char* ul_onedrive_badger_body(int attempt);
/// The token out of step 1's reply. The caller frees it.
char* ul_onedrive_read_badger(const char* json, size_t length);

/// Step 2's URL, built from the share link. The caller frees it.
char* ul_onedrive_item_url(const char* share_url);
/// Reads step 2's reply. Both out-parameters are the caller's to free. Returns
/// UL_OK, or UL_ERR_PARSE with ul_last_error set.
int ul_onedrive_read_item(const char* json, size_t length, char** drive_id,
                          char** item_id);

/// Step 3's URL. The caller frees it.
char* ul_onedrive_children_url(const char* drive_id, const char* item_id);

/// Reads a folder listing and answers with the archive to download.
///
/// **The newest by date wins.** A modification date is always there and always
/// means what it says; a version in a file name is a convention, and a rule
/// that leant on it would break the day a package is called something else.
///
/// `expected_version` narrows the field before that rule is applied: when the
/// mod page already says v6.6, a file whose name carries v6.6 is preferred over
/// one that does not, and the date then decides between those. Pass "" to take
/// the newest thing in the folder outright.
///
/// `picked_name` receives the file's name, which the caller frees. It is worth
/// having because the download URL does not contain it — OneDrive answers with
/// a "download.aspx?UniqueId=…" link — so this is the only chance to find out
/// what is about to be downloaded.
///
/// Null if the listing holds no archive. The caller frees the result.
char* ul_folder_pick_archive(const char* json, size_t length,
                             const char* expected_version, char** picked_name);

// ------------------------------------------------------------- the archive
//
// Reads .rar, both rar4 and rar5. The only part of the core that touches a
// disk, and it only ever writes below the directory it is given.

typedef struct ul_archive ul_archive;

/// Progress, and the chance to stop. Return 0 to cancel an extraction.
/// `entry` is the path inside the archive, in UTF-8.
typedef int (*ul_archive_progress)(void* user, const char* entry,
                                   int64_t done, int64_t total);

ul_archive* ul_archive_open(const char* archive_path);
void        ul_archive_close(ul_archive* a);

/// Files in the archive, in archive order. Directories are not listed.
int         ul_archive_count(const ul_archive* a);
const char* ul_archive_entry(const ul_archive* a, int index);
int64_t     ul_archive_entry_size(const ul_archive* a, int index);
int64_t     ul_archive_total_size(const ul_archive* a);

/// Unpacks everything below `dest_dir`. Returns UL_OK, or one of the errors.
/// An entry whose path escapes `dest_dir` — "..", a drive letter, a leading
/// separator — is refused rather than sanitised: an archive containing one is
/// not a mod package, and should not be half-installed before anyone notices.
int ul_archive_extract(ul_archive* a, const char* dest_dir,
                       ul_archive_progress progress, void* user);

/// Reads one entry into memory. For the pointer archive on the mod page, which
/// is 197 bytes and whose whole purpose is the text inside it. Null on failure.
/// `length` receives the byte count. The caller frees it.
char* ul_archive_read_entry(ul_archive* a, int index, size_t* length);

// ---------------------------------------------------------------- packages
//
// What an extracted package looks like, decided from the paths it produced
// rather than by looking at a disk. dannyldd's packages are a folder holding
// UniFiles/, Data/, Unification.exe and Plugins/, but they have been shipped
// with all of that one level down inside a wrapper folder as often as not, so
// the root is found rather than assumed.

// --- the unpacked copy, kept ------------------------------------------------
//
// A release is 800 MB over a connection that is somebody's home line, and the
// same 800 MB whether it is the first install or the fourth. So the unpacked
// package stays in the manager's store after the install, with a stamp beside
// it saying what it is, and the next install of that version copies from it
// instead of fetching it again.
//
// The stamp is what makes the cache trustworthy. A folder full of files is not
// evidence of a *complete* extraction: a cancelled or crashed one leaves a
// folder that looks exactly the same, and installing from it would install half
// a mod. Nothing is reused unless the stamp agrees with what is there.

/// The stamp to write beside a freshly unpacked package. `root` is the prefix
/// ul_package_root found, and `file_count` is how many files the extraction
/// produced. The caller frees it; `length` receives the byte count.
/// `complete` says whether the whole package is in the store, or only the part
/// of it the game folder does not hold. After an install the bulk lives in the
/// game and only Plugins/ stays behind — the catalogue needs it, and the game
/// never holds more than one plugin at a time — so the copy in the store is no
/// longer something an install could be run from.
char* ul_package_stamp(const char* version, const char* root, int file_count,
                       int complete, size_t* length);

/// Whether a stamp describes a complete copy of `version` matching a folder
/// that now holds `file_count` files. Returns 1 when the cache can be used.
int ul_package_stamp_ok(const char* json, size_t length, const char* version,
                        int file_count);

/// Whether that stamp describes a whole package — one an install can be run
/// from without downloading anything.
int ul_package_stamp_complete(const char* json, size_t length);

/// The package root the stamp recorded, or "" if it says nothing useful. Saves
/// deriving it a second time. The caller frees it.
char* ul_package_stamp_root(const char* json, size_t length);

/// The package-relative prefix under which the real package sits — "" when the
/// archive's root is the package, "War2 Unification v6.6/" when it is wrapped.
/// `paths` is every extracted path, NUL-separated, ending with an empty string.
/// The caller frees it.
char* ul_package_root(const char* paths);

// ---------------------------------------------------------------- plugins
//
// A plugin is one folder under the package's Plugins/ directory. It carries
// the .w2p files that make it work, an info.txt in the author's own words, and
// screenshots. The package root may also carry a base/ folder describing the
// mod itself the same way — info.txt and screenshots for the "no plugin"
// selection. No package ships one yet; it is read the day one does.
//
// The catalogue is built from a *list of paths*, not by walking a disk: the
// host walks, the core decides what the walk means. That is what lets the
// grouping, the ordering and the description parsing be tested anywhere.

typedef struct ul_catalogue ul_catalogue;

ul_catalogue* ul_catalogue_create(void);
void          ul_catalogue_free(ul_catalogue* c);

/// Adds one path, relative to the package root and using either separator —
/// "Plugins/2_Insane/insane.w2p". Paths under Plugins/ build the plugins,
/// paths under base/ build the mod's own gallery, and everything else is
/// ignored, so the host can hand over the whole extraction unfiltered.
void ul_catalogue_add_path(ul_catalogue* c, const char* relative_path);

/// Reads an info.txt — a plugin's when `plugin_id` names its folder, the mod's
/// own when it is "" (the base/ folder's file).
///
/// The file is the author's read-me and is read as exactly that: the whole
/// text becomes the description, kept as written, and every YouTube video or
/// playlist link anywhere in it becomes a video in the gallery — see
/// ul_plugin_video_*. It never names the plugin: the folder does, for now.
void ul_catalogue_add_info(ul_catalogue* c, const char* plugin_id,
                           const char* text, size_t length);

/// Sorts and closes the catalogue. Ordering is the numeric prefix the folders
/// carry ("0_basegame", "1_DAIFE", "2_Insane"), which is the author's own
/// intended order, falling back to name order for a folder without one.
void ul_catalogue_finish(ul_catalogue* c);

int         ul_catalogue_count(const ul_catalogue* c);
/// The folder name, which is the stable identity remembered between runs.
const char* ul_plugin_id(const ul_catalogue* c, int index);
/// What the list shows: the folder name cleaned up, so "3_Legacy of Dalaran"
/// shows as "Legacy of Dalaran". Always the folder's, for now — an info.txt
/// describes a plugin but does not name it.
const char* ul_plugin_name(const ul_catalogue* c, int index);
const char* ul_plugin_description(const ul_catalogue* c, int index);
/// Whether the plugin said anything about itself, or whether the name and
/// description above were made up from its folder name. The client shows a
/// placeholder rather than a confident-looking empty panel when this is 0.
int         ul_plugin_has_info(const ul_catalogue* c, int index);

// --- variants ---------------------------------------------------------------
//
// A plugin folder's .w2p files are alternatives, not a set. Most plugins hold
// one. Some hold two or three, and those are difficulty settings — "plugin
// trolls 1.w2p" and "plugin trolls 2.w2p" are normal and hard, and copying both
// into the game would be copying two mods on top of each other.
//
// So exactly one variant is installed at a time, and a plugin with more than
// one gets a second choice in the client. Twelve of the eighteen plugins in
// v6.6 have more than one, up to five, so this is the common case and not a
// corner of it.

int         ul_plugin_variant_count(const ul_catalogue* c, int index);
/// What to call a variant. The file's own name without its extension —
/// "plugin trolls 1" — which is not friendly but is at least the author's.
const char* ul_plugin_variant_name(const ul_catalogue* c, int index, int variant);
/// Package-relative path of the variant's .w2p.
const char* ul_plugin_variant_path(const ul_catalogue* c, int index, int variant);

// Anything else in the folder is the author's notes — a tilesets list, a link
// to the original thread, a zip of the files a sub-mod started from — and is
// not installed. dannyldd's instructions are to send *the plugin file* to the
// game's plugin folder, singular, and copying his documentation in with it
// would litter the game folder with text the uninstall then has to take back.

int         ul_plugin_image_count(const ul_catalogue* c, int index);
/// Package-relative path of one screenshot, in name order — the slideshow's
/// order, and so the order the author gets by naming files 1.png, 2.png. The
/// client draws these at 960x544; anything else is fitted to that box.
const char* ul_plugin_image(const ul_catalogue* c, int index, int image);

// --- videos -----------------------------------------------------------------
//
// Any YouTube video or playlist link in the plugin's info.txt. They sit in
// the same gallery as the screenshots, because to somebody choosing between
// eighteen sub-mods a video of one running is a better screenshot than a
// screenshot. An item is one or the other: a video has an id, a playlist has
// a list id, and the getter for the kind it is not answers "".

int         ul_plugin_video_count(const ul_catalogue* c, int index);
/// The eleven-character video id, which is also the name of its thumbnail at
/// img.youtube.com/vi/<id>/hqdefault.jpg — no key, no API, just a GET. "" for
/// a playlist, which has no single thumbnail anyone can GET.
const char* ul_plugin_video_id(const ul_catalogue* c, int index, int video);
/// The playlist id, when the item is a playlist rather than a video. "" for a
/// video — including a video the author linked *from* a playlist, which
/// counts as the video alone.
const char* ul_plugin_video_list_id(const ul_catalogue* c, int index, int video);
/// The watch page — or for a playlist, the playlist page — for a browser.
const char* ul_plugin_video_url(const ul_catalogue* c, int index, int video);
/// The embed form of the same video, which is what a web view has to be given
/// — a watch page refuses to load in a frame. Nothing uses this yet: the
/// gallery opens the watch page in the user's browser, because embedding needs
/// WebView2 and its runtime is not on the older machines this has to run on.
/// It is here so that the day it is, the core already answers the question.
const char* ul_plugin_video_embed_url(const ul_catalogue* c, int index, int video);

/// The index of a plugin by its id, or -1. Used to restore the remembered
/// choice across an update that may have renumbered the folders.
int         ul_catalogue_find(const ul_catalogue* c, const char* plugin_id);

/// The id that means "no plugin — the mod's own base game". It is not a folder
/// and never appears in the catalogue; it is what an empty selection is called,
/// and selecting it removes every plugin file the manager placed.
const char* ul_plugin_none_id(void);

// --- the mod's own gallery --------------------------------------------------
//
// From the package's base/ folder, when it has one: what the mod itself looks
// like and says about itself, shown for the "no plugin" selection. The paths
// arrive through ul_catalogue_add_path like everything else; the description
// and videos through ul_catalogue_add_info with the empty id. All of these
// answer empty until a package actually ships the folder.

const char* ul_base_description(const ul_catalogue* c);
int         ul_base_image_count(const ul_catalogue* c);
const char* ul_base_image(const ul_catalogue* c, int image);
int         ul_base_video_count(const ul_catalogue* c);
const char* ul_base_video_id(const ul_catalogue* c, int video);
const char* ul_base_video_list_id(const ul_catalogue* c, int video);
const char* ul_base_video_url(const ul_catalogue* c, int video);

/// The thumbnail_url out of a YouTube oEmbed answer — how a playlist's cover
/// is found without an API key, since img.youtube.com only names videos. The
/// host GETs youtube.com/oembed?format=json&url=<the playlist page> and hands
/// the answer here. NULL when it holds none; the caller frees with ul_free.
char* ul_oembed_thumbnail_url(const char* json, size_t length);

// ------------------------------------------------------------------- plans
//
// Nothing is copied or deleted by a function that decided to. A plan is built,
// and then run. That split is what makes "uninstall" provably exact: the plan
// comes from the receipt of what was installed, so it names only files the
// manager itself wrote, and the stock War2Combat files beside them — the
// AutoWarLat.w2p and lobby_map.w2p every install already has — are not
// reachable from it at all.

typedef struct ul_plan ul_plan;

enum {
  UL_OP_MKDIR = 0,    // ensure dest exists
  UL_OP_BACKUP = 1,   // move dest aside to backup before it is overwritten
  UL_OP_COPY = 2,     // src -> dest
  UL_OP_DELETE = 3,   // remove dest, which the manager placed
  UL_OP_RESTORE = 4,  // move backup back over dest
  UL_OP_ARCHIVE = 5,  // move dest back into the store, rather than deleting it
};

int         ul_plan_count(const ul_plan* p);
int         ul_plan_op(const ul_plan* p, int index);
/// Absolute source path, for COPY and RESTORE. "" otherwise.
const char* ul_plan_src(const ul_plan* p, int index);
/// Absolute destination path. Every op has one.
const char* ul_plan_dest(const ul_plan* p, int index);
void        ul_plan_free(ul_plan* p);

/// The plan that puts an extracted package into a game folder.
///
/// `package_paths` is every file the extraction produced, relative to the
/// package root, NUL-separated and ending with an empty string — the host
/// already has that list from the walk it did to build the catalogue.
/// `existing` is the same, for files the game folder already has, so the plan
/// knows what it is about to overwrite and backs those up first.
///
/// Plugins/ is deliberately not installed into the game: the manager keeps the
/// whole catalogue in its own store and copies one plugin's files in on demand.
ul_plan* ul_plan_install(const char* package_dir, const char* game_dir,
                         const char* package_paths, const char* existing,
                         const char* backup_dir);

/// The plan that makes `plugin_id`'s `variant` the active one, having
/// previously made `active_id` active. Passing ul_plugin_none_id() as
/// `plugin_id` is the "no plugin" choice and yields a plan that only removes.
///
/// Removal covers *every* variant the outgoing plugin has, not just the one
/// that was installed. It costs a few delete steps for files that are not there
/// and it means switching from "trolls, hard" to "trolls, normal" cannot leave
/// the hard one behind — which the game would load, silently, giving a player
/// the difficulty they just changed away from.
ul_plan* ul_plan_set_plugin(const ul_catalogue* c, const char* package_dir,
                            const char* game_dir, const char* active_id,
                            const char* plugin_id, int variant);

/// The plan that undoes an install, read from its receipt.
///
/// Files the manager added are *moved back into the store* rather than
/// deleted, and files it replaced are restored from their backups after the
/// mod's version has been moved back. A file the receipt does not mention is
/// left alone, however much it looks like ours.
///
/// Moved rather than deleted because the package has to live exactly once. It
/// is 1.2 GB: keeping a copy in the store while the same files sit in the game
/// folder doubles that for nothing, and deleting it means the next install
/// fetches 800 MB again. Moving is neither — the package is in the game or in
/// the store, never in both and never nowhere.
///
/// The destination comes from the receipt, which records the package folder and
/// the game folder the install used.
ul_plan* ul_plan_uninstall(const char* receipt_json, size_t length);

// ----------------------------------------------------------------- receipt
//
// What was written, so that what was written can be taken back. Kept beside the
// manager's own store rather than in the game folder: a user who deletes the
// game folder has uninstalled already, and one who reinstalls the game over the
// top has not.

typedef struct ul_receipt ul_receipt;

ul_receipt* ul_receipt_create(const char* mod_version);
void        ul_receipt_free(ul_receipt* r);

/// Records one destination the manager wrote. `backup` is the path the previous
/// file was moved to, or "" when there was no previous file.
void ul_receipt_add(ul_receipt* r, const char* dest, const char* backup);

/// Which plugin, and which of its variants, was active — so the next run
/// reopens where the user left off, down to the difficulty they chose.
void        ul_receipt_set_plugin(ul_receipt* r, const char* plugin_id);
void        ul_receipt_set_variant(ul_receipt* r, int variant);
/// Where the package was unpacked, so a plugin can be switched without it.
void        ul_receipt_set_package_dir(ul_receipt* r, const char* dir);
void        ul_receipt_set_game_dir(ul_receipt* r, const char* dir);

/// Serialises to JSON. The caller frees it. `length` receives the byte count.
char* ul_receipt_to_json(const ul_receipt* r, size_t* length);

/// Reads one back. Null if the bytes are not a receipt.
ul_receipt* ul_receipt_parse(const char* json, size_t length);
const char* ul_receipt_mod_version(const ul_receipt* r);
const char* ul_receipt_plugin(const ul_receipt* r);
int         ul_receipt_variant(const ul_receipt* r);
const char* ul_receipt_package_dir(const ul_receipt* r);
const char* ul_receipt_game_dir(const ul_receipt* r);
int         ul_receipt_count(const ul_receipt* r);
const char* ul_receipt_dest(const ul_receipt* r, int index);
const char* ul_receipt_backup(const ul_receipt* r, int index);

// ------------------------------------------------- the game's own settings
//
// War2Combat's display options live in an INI beside the game. They are not the
// mod's and not UniLoader's, so they are edited in place: only the keys asked
// for change, and every comment, blank line, other section and the order of all
// of it survives. They also outlive an install and an uninstall, because they
// are the *game's* settings and always were.
//
// Two such files ship. The live one belongs to whichever ddraw.dll is in place;
// the other has different key names, so writing it changes nothing at all. The
// host works out which and hands in its contents.

typedef struct ul_ini ul_ini;

ul_ini* ul_ini_parse(const char* text, size_t length);
void    ul_ini_free(ul_ini* ini);

/// A value, or "" when the key is not there. Case-insensitive on both names.
const char* ul_ini_get(const ul_ini* ini, const char* section, const char* key);
/// Replaces a value in place, keeping the key's own spelling, its spacing and
/// any comment after it on the same line. A key that is not there is added at
/// the end of its section — not the end of the file, where it would land under
/// whichever section happened to come last.
void ul_ini_set(ul_ini* ini, const char* section, const char* key, const char* value);
/// The whole file back, byte for byte except where it was set. Caller frees.
char* ul_ini_write(const ul_ini* ini, size_t* length);

/// The value the wrapper will actually use.
///
/// `[ddraw]` holds the defaults and a section named after the executable
/// overrides them — and War2Combat's own configuration tool writes one:
/// `[Warcraft II BNE]` in this install sets `windowed` and `fullscreen` again.
/// Reading `[ddraw]` alone reports a setting the game is not using.
const char* ul_ini_effective(const ul_ini* ini, const char* key);

/// Sets `key` in `[ddraw]` and in every other section that already states it.
///
/// Not in sections that do not: adding `fullscreen` to a section that never
/// mentioned it would take a setting away from the defaults it was inheriting.
/// This only keeps the places that already have an opinion in agreement, so
/// that whichever one the wrapper reads, the answer is the same.
void ul_ini_set_everywhere(ul_ini* ini, const char* key, const char* value);

// --- what a person actually chooses -----------------------------------------
//
// The wrapper spells the display mode as two booleans whose pairing nobody
// guesses right: `fullscreen` alone is exclusive fullscreen, and `fullscreen`
// *with* `windowed` is a borderless window filling the screen. Three named
// modes go in front of a person; the pair stays behind this.
enum {
  UL_DISPLAY_FULLSCREEN = 0,   // exclusive, the screen changes mode
  UL_DISPLAY_BORDERLESS = 1,   // a window with no frame, filling the screen
  UL_DISPLAY_WINDOWED = 2,
};

int  ul_display_mode(const ul_ini* ini);
void ul_display_set_mode(ul_ini* ini, int mode);

/// Whether the 4:3 picture keeps its shape on a widescreen monitor instead of
/// being stretched across it.
int  ul_display_keep_aspect(const ul_ini* ini);
void ul_display_set_keep_aspect(ul_ini* ini, int on);

/// The scaling filter, as a path relative to the game folder —
/// "Shaders\bilinear.glsl" — or "" for none. Which shaders exist is a question
/// about a folder, so the host answers that one.
const char* ul_display_shader(const ul_ini* ini);
void ul_display_set_shader(ul_ini* ini, const char* file);

// ------------------------------------------------------------------ hashes
//
// Streamed rather than over a buffer: the thing being hashed is the download,
// and it is verified as it arrives.

typedef struct ul_sha256 ul_sha256;

ul_sha256* ul_sha256_create(void);
void       ul_sha256_update(ul_sha256* h, const void* data, size_t length);
/// Writes 64 lowercase hex characters and a NUL into `out`, then frees `h`.
void       ul_sha256_finish(ul_sha256* h, char* out /* [65] */);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // UNILOADER_H

// Ids for the user-visible strings, which live in Strings.rc and nowhere else.
//
// Retired ids stay retired: an id that stops being used is left unused with a
// comment rather than reassigned, so an old translation cannot land on the
// wrong sentence.

#pragma once

#define IDS_APP_TITLE            200
#define IDS_CHECKING             201
#define IDS_UP_TO_DATE           202
#define IDS_UPDATE_AVAILABLE     203
#define IDS_NOT_INSTALLED        204
#define IDS_INSTALLED            205
#define IDS_DOWNLOADING          206
#define IDS_EXTRACTING           207
#define IDS_INSTALLING           208
#define IDS_REMOVING             209
#define IDS_DONE                 210
#define IDS_CANCELLED            211
#define IDS_LISTING              212
#define IDS_USING_CACHE          213
#define IDS_SETTINGS_CACHE       214
#define IDS_CLEAR_CACHE          215
// 216 was IDS_CACHE_EMPTY. Retired, not reassigned: the settings window no
// longer builds the row at all when there is nothing to delete.
// The game's own display settings. Numbered well clear of everything else:
// 217 onwards ran straight into the IDS_ACTION_* block and the resource
// compiler refuses a reused id rather than quietly picking one.
#define IDS_DISPLAY              290
#define IDS_DISPLAY_MODE         291
#define IDS_MODE_FULLSCREEN      292
#define IDS_MODE_BORDERLESS      293
#define IDS_MODE_WINDOWED        294
#define IDS_KEEP_ASPECT          295
#define IDS_SMOOTHING            296
#define IDS_SMOOTHING_NONE       297
// 298 was IDS_GAME_OPTIONS. Retired with the button; see resource.h.
#define IDS_NO_DISPLAY_CONFIG    299

// 290-299 is full, 298 retired, so the next block starts here.
#define IDS_PROJECT_PAGE         300

#define IDS_ACTION_INSTALL       220
#define IDS_ACTION_UPDATE        221
#define IDS_ACTION_CANCEL        222
#define IDS_ACTION_CHECK         223
#define IDS_PLAY                 224
#define IDS_UNINSTALL            225
// 226 and 227 were "Use this plugin" and "In use", the button the checkbox in
// the list replaced. The ids stay retired.
#define IDS_NO_PLUGIN            228
#define IDS_NO_PLUGIN_ABOUT      229
#define IDS_PLUGINS_HEADING      230
#define IDS_CHANGE_FOLDER        231
#define IDS_MOD_PAGE             232
// 233 was IDS_AUTHOR_PAGE, the About window's link to dannyldd's profile. The
// window went, and the profile is one click from the mod page anyway.
#define IDS_CHANGELOG            234
#define IDS_SETTINGS             235
#define IDS_CLOSE                236
#define IDS_SETTINGS_TITLE       237
#define IDS_SETTINGS_FOLDER      238
#define IDS_SETTINGS_STORE       239
#define IDS_CHANGELOG_TITLE      243
#define IDS_CHANGELOG_EMPTY      244
#define IDS_SETTINGS_INSTALLED   245
#define IDS_SETTINGS_NOTHING     246
// 247 and 248 were IDS_ABOUT_TITLE and IDS_ABOUT_MOD. The About window went;
// everything it said is in the title bar, the status line, or on GameBanana.
// 249 and 250 were "Activate" and "Activated", the button Play replaced.
#define IDS_PLAY_NAMED           251
// 252 was IDS_BASE_PLUGIN, the grey "base game" beside "Unification only".
// The category column went, and it went with it.

// 240 was IDS_NO_INFO. Retired: a plugin with no description shows an empty
// box, which says the same thing without a sentence to read.
// 241 was IDS_NO_SHOTS, the caption under the fallback screen's glyph. The
// glyph says it alone now.
#define IDS_SHOT_COUNT           242   // "%d of %d"

#define IDS_ERR_NO_GAME          260
#define IDS_ERR_NO_ARCHIVE_LINK  261
#define IDS_ERR_FOLDER_LINK      262
#define IDS_ERR_NETWORK          263
#define IDS_ERR_NEEDS_ADMIN      264
#define IDS_ERR_GAME_RUNNING     265

#define IDS_CONFIRM_UNINSTALL    280
// 281 was IDS_ABOUT, the About window's small print. The window went with it.
#define IDS_FIND_GAME            282

// Deciding what to copy, back up, delete and restore — and deciding it all
// before anything happens.
//
// Nothing in UniLoader is copied or removed by a function that decided to. A
// plan is built, and then run. The split buys three things worth the extra
// type: an uninstall that can be proved exact because it is generated from the
// receipt and can only name files the manager wrote; a "what will this do"
// screen that is the same list the run will follow rather than a description of
// it; and the whole of this file being testable on a machine with no game and
// no filesystem to speak of.

#include "uniloader/uniloader.h"

#include "json.hpp"
#include "util.hpp"
#include "version.hpp"

#include <algorithm>
#include <new>
#include <set>
#include <string>
#include <vector>

namespace ul {

struct PlanStep {
  int op = UL_OP_COPY;
  std::string src;
  std::string dest;
};

}  // namespace ul

struct ul_plan {
  std::vector<ul::PlanStep> steps;
};

namespace ul {
namespace {

/// Where a plugin goes inside the game folder.
///
/// The package's own `mod description.txt` says it: "Send the plugin file 1 OR
/// 2 from BaseGame sub-folder to your War2Combat/Plugin sub-folder". That is
/// the same `plugin\` folder a stock War2Combat already has — the one holding
/// AutoWarLat.w2p, CpuSaveC.w2p, PlaySound.w2p and lobby_map.w2p, which belong
/// to the game and are not the mod's to remove. They are safe because nothing
/// is ever deleted that the catalogue or the receipt does not name.
///
/// Case does not matter on Windows and this matches the folder that is already
/// there, so no second folder is created beside it.
constexpr char kPluginDestFolder[] = "plugin";

/// The store's own note about the copy it holds. Named here because the
/// install has to know not to copy it into the game folder.
constexpr char kStampName[] = "uniloader-package.json";

/// Files at the top of a package that say "the package starts here". Used to
/// find the real root inside an archive that wraps everything in a folder,
/// which dannyldd's have done about as often as not.
const char* kRootMarkers[] = {"unifiles", "plugins", "data", "unification.exe"};

bool IsRootMarker(const std::string& segment) {
  const std::string lowered = Lower(segment);
  for (const char* marker : kRootMarkers) {
    if (lowered == marker) return true;
  }
  return false;
}

/// The segments of `path` before index `count`, rejoined, with a trailing '/'.
std::string PrefixOf(const std::string& path, size_t count) {
  if (count == 0) return {};
  std::string prefix;
  for (size_t i = 0; i < count; ++i) {
    const std::string segment = Segment(path, i);
    if (segment.empty()) return prefix;
    prefix += segment;
    prefix.push_back('/');
  }
  return prefix;
}

void Add(ul_plan* plan, int op, const std::string& src, const std::string& dest) {
  plan->steps.push_back(PlanStep{op, src, dest});
}

/// MKDIR for `dest`'s folder, once per folder.
///
/// Once, because a package is thousands of files in a few dozen folders and a
/// plan with a mkdir per file would be mostly noise — in the progress bar as
/// much as in a test's expectations.
void EnsureFolder(ul_plan* plan, std::set<std::string>& made, const std::string& dest) {
  const std::string folder = DirName(dest);
  if (folder.empty()) return;
  const std::string key = Lower(folder);
  if (!made.insert(key).second) return;
  // Parents first, so a run can create each one without recursing itself.
  const std::string parent = DirName(folder);
  if (!parent.empty() && parent != folder) EnsureFolder(plan, made, folder);
  Add(plan, UL_OP_MKDIR, {}, folder);
}

std::set<std::string> LoweredSet(const std::vector<std::string>& items) {
  std::set<std::string> set;
  for (const std::string& item : items) set.insert(Lower(NormaliseSlashes(item)));
  return set;
}

}  // namespace

std::string PackageRoot(const std::vector<std::string>& paths) {
  // The prefix a marker sits directly under. For a package already at the root
  // that prefix is "" and there is nothing to strip; for one wrapped in a
  // "War2 Unification v6.6/" folder it is that folder, which is exactly what
  // has to come off before any path means anything.
  //
  // Candidates come only from markers — "" is not seeded — because "" trivially
  // covers every path and would win every tie, which is the bug that made a
  // wrapped package install its wrapper folder into the game.
  std::string best;
  size_t best_covered = 0;
  std::set<std::string> candidates;
  for (const std::string& path : paths) {
    const std::string normalised = NormaliseSlashes(path);
    for (size_t i = 0; i < 4; ++i) {          // no package nests deeper than this
      const std::string segment = Segment(normalised, i);
      if (segment.empty()) break;
      if (IsRootMarker(segment)) {
        candidates.insert(PrefixOf(normalised, i));
        break;
      }
    }
  }
  for (const std::string& candidate : candidates) {
    size_t covered = 0;
    for (const std::string& path : paths) {
      if (candidate.empty() || StartsWithNoCase(NormaliseSlashes(path), candidate)) {
        ++covered;
      }
    }
    // A tie goes to the shallower prefix, which only arises between two wrapper
    // candidates — an archive holding both "Mod/Data" and "Mod/Extra/Data".
    const bool better = covered > best_covered ||
                        (covered == best_covered && candidate.size() < best.size());
    if (better) {
      best = candidate;
      best_covered = covered;
    }
  }
  // A wrapper that only a minority of the files sit under is not a wrapper: it
  // is one folder inside a package whose root is somewhere else, and stripping
  // it would throw the rest of the package away.
  if (!best.empty() && best_covered * 2 < paths.size()) return {};
  return best;
}

}  // namespace ul

// ------------------------------------------------------------------- the ABI

extern "C" {

int ul_plan_count(const ul_plan* p) {
  return p ? static_cast<int>(p->steps.size()) : 0;
}

int ul_plan_op(const ul_plan* p, int index) {
  if (!p || index < 0 || index >= static_cast<int>(p->steps.size())) return -1;
  return p->steps[static_cast<size_t>(index)].op;
}

const char* ul_plan_src(const ul_plan* p, int index) {
  if (!p || index < 0 || index >= static_cast<int>(p->steps.size())) return "";
  return p->steps[static_cast<size_t>(index)].src.c_str();
}

const char* ul_plan_dest(const ul_plan* p, int index) {
  if (!p || index < 0 || index >= static_cast<int>(p->steps.size())) return "";
  return p->steps[static_cast<size_t>(index)].dest.c_str();
}

void ul_plan_free(ul_plan* p) { delete p; }

char* ul_package_root(const char* paths) {
  return ul::Duplicate(ul::PackageRoot(ul::SplitNulList(paths)));
}

char* ul_package_stamp(const char* version, const char* root, int file_count,
                       int complete, size_t* length) {
  std::string out = "{\n";
  out += "  \"uniloader_package\": 1,\n";
  out += "  \"version\": \"" + ul::JsonEscaped(version ? version : "") + "\",\n";
  out += "  \"root\": \"" + ul::JsonEscaped(root ? root : "") + "\",\n";
  out += "  \"complete\": " + std::string(complete ? "1" : "0") + ",\n";
  out += "  \"files\": " + std::to_string(file_count) + "\n}\n";
  if (length) *length = out.size();
  return ul::Duplicate(out);
}

int ul_package_stamp_complete(const char* json, size_t length) {
  if (!json) return 0;
  ul::Json document;
  if (!ul::Json::Parse(json, length, document)) return 0;
  return document.Int("complete", 0) == 1 ? 1 : 0;
}

int ul_package_stamp_ok(const char* json, size_t length, const char* version,
                        int file_count) {
  if (!json) return 0;
  ul::Json document;
  if (!ul::Json::Parse(json, length, document)) return 0;
  if (document.Int("uniloader_package", 0) != 1) return 0;
  // The version has to match, or a cache kept from v6.5 would be installed as
  // v6.6 and the receipt would record a release that is not on disk.
  if (ul::CompareVersions(document.Str("version"), version ? version : "") != 0) {
    return 0;
  }
  // And the count has to match what is there *now*. A folder full of files is
  // not evidence of a complete extraction — a cancelled one leaves a folder
  // that looks the same — and this is the cheap check that tells them apart
  // without hashing 800 MB every time the program starts.
  if (document.Int("files", -1) != file_count) return 0;
  return 1;
}

char* ul_package_stamp_root(const char* json, size_t length) {
  if (!json) return nullptr;
  ul::Json document;
  if (!ul::Json::Parse(json, length, document)) return nullptr;
  return ul::Duplicate(document.Str("root"));
}

ul_plan* ul_plan_install(const char* package_dir, const char* game_dir,
                         const char* package_paths, const char* existing,
                         const char* backup_dir) {
  if (!package_dir || !game_dir || !backup_dir) return nullptr;
  auto plan = new (std::nothrow) ul_plan();
  if (!plan) return nullptr;

  const std::string package = ul::TrimTrailingSlash(ul::NormaliseSlashes(package_dir));
  const std::string game = ul::TrimTrailingSlash(ul::NormaliseSlashes(game_dir));
  const std::string backup = ul::TrimTrailingSlash(ul::NormaliseSlashes(backup_dir));
  const std::set<std::string> already = ul::LoweredSet(ul::SplitNulList(existing));

  std::set<std::string> made;
  for (const std::string& raw : ul::SplitNulList(package_paths)) {
    const std::string relative = ul::NormaliseSlashes(raw);
    if (relative.empty()) continue;
    // Refused rather than sanitised, and refused here as well as in the
    // extractor: a plan is also built from paths a caller supplies.
    if (!ul::IsSafeRelativePath(relative)) continue;
    // Plugins stay in the store. The whole catalogue is kept there and exactly
    // one plugin's files are copied into the game at a time, which is what
    // makes switching a copy and a delete rather than a reinstall.
    if (ul::EqualsNoCase(ul::Segment(relative, 0), "Plugins")) continue;
    // So does base/: it is the mod describing itself — screenshots and an
    // info.txt for the launcher's gallery — not files the game loads, and an
    // uninstall should never have to take a read-me back out of a game folder.
    if (ul::EqualsNoCase(ul::Segment(relative, 0), "base")) continue;
    // And the press-kit folder v6.6 ships at its root: screenshots and credit
    // sheets for people, nothing the game reads. It stays in the store like
    // the folders above rather than adding five megabytes to the game folder.
    if (ul::EqualsNoCase(ul::Segment(relative, 0), "SCREENSHOTS & CREDITS, MAIN")) {
      continue;
    }
    // And the stamp is UniLoader's own bookkeeping about the store, not part
    // of the package. Copied in, it turns up as a stray file in the game
    // folder that the mod knows nothing about.
    if (ul::EqualsNoCase(ul::BaseName(relative), ul::kStampName)) continue;

    const std::string dest = ul::JoinPath(game, relative);
    ul::EnsureFolder(plan, made, dest);
    if (already.count(ul::Lower(relative)) != 0) {
      // Moved aside, not copied aside. A move is atomic on the same volume and
      // costs nothing on a 10 MB maindat.war, and it means the game folder is
      // never briefly holding two copies of a file the size of the game.
      ul::Add(plan, UL_OP_BACKUP, dest, ul::JoinPath(backup, relative));
    }
    ul::Add(plan, UL_OP_COPY, ul::JoinPath(package, relative), dest);
  }
  return plan;
}

ul_plan* ul_plan_set_plugin(const ul_catalogue* c, const char* package_dir,
                            const char* game_dir, const char* active_id,
                            const char* plugin_id, int variant) {
  if (!game_dir) return nullptr;
  auto plan = new (std::nothrow) ul_plan();
  if (!plan) return nullptr;

  const std::string package =
      ul::TrimTrailingSlash(ul::NormaliseSlashes(package_dir ? package_dir : ""));
  const std::string game = ul::TrimTrailingSlash(ul::NormaliseSlashes(game_dir));
  const std::string destination = ul::JoinPath(game, ul::kPluginDestFolder);
  const std::string active = active_id ? active_id : "";
  const std::string wanted = plugin_id ? plugin_id : "";

  // "Plugins/2_Insane/foo/bar.w2p" lands at "<game>/Plugins/foo/bar.w2p": the
  // plugin's own folder is what is being unwrapped, and anything below it keeps
  // its shape.
  auto inside = [&](const ul_catalogue* catalogue, int index,
                    const std::string& relative) -> std::string {
    const std::string prefix =
        std::string("Plugins/") + ul_plugin_id(catalogue, index) + "/";
    if (!ul::StartsWithNoCase(relative, prefix)) return {};
    return relative.substr(prefix.size());
  };

  // Out with the old first, and completely — every variant it has, not only the
  // one that was installed. It costs a few deletes of files that are not there,
  // and it means switching from "trolls, hard" to "trolls, normal" cannot leave
  // the hard one behind for the game to load instead.
  //
  // Choosing "No plugin" goes further: every variant of every plugin in the
  // catalogue. It is the row a person reaches for when something is wrong, so
  // it cleans up whatever a mistake left behind — a remembered plugin that is
  // not the one actually on disk, say — not only what the receipt believes is
  // active. Still only files the catalogue names, so the game's own stock
  // .w2p files in the same folder are never touched.
  auto remove_all_of = [&](int index) {
    for (int i = 0; i < ul_plugin_variant_count(c, index); ++i) {
      const std::string part = inside(c, index, ul_plugin_variant_path(c, index, i));
      if (!part.empty()) ul::Add(plan, UL_OP_DELETE, {}, ul::JoinPath(destination, part));
    }
  };
  if (wanted.empty()) {
    for (int index = 0; index < ul_catalogue_count(c); ++index) remove_all_of(index);
  } else {
    const int active_index = ul_catalogue_find(c, active.c_str());
    if (active_index >= 0) remove_all_of(active_index);
  }

  const int wanted_index = ul_catalogue_find(c, wanted.c_str());
  if (wanted_index >= 0) {
    // One .w2p, and nothing else. The rest of a plugin folder is the author's
    // notes — "0_TILESETS x map ROH.txt", "mod original thread.txt", and in one
    // case a zip of the original files — and dannyldd's own instructions are to
    // send *the plugin file* to the game's plugin folder, singular. Copying the
    // documentation in with it would litter the game folder with text files
    // that the uninstall would then have to take back out again.
    std::set<std::string> made;
    const int variants = ul_plugin_variant_count(c, wanted_index);
    if (variants > 0) {
      // Out of range falls back to the first rather than installing nothing: a
      // remembered choice can outlive the release that had that many variants,
      // and a plugin the user asked for must not silently not appear.
      const int chosen = (variant >= 0 && variant < variants) ? variant : 0;
      const std::string relative = ul_plugin_variant_path(c, wanted_index, chosen);
      const std::string part = inside(c, wanted_index, relative);
      if (!part.empty()) {
        const std::string dest = ul::JoinPath(destination, part);
        ul::EnsureFolder(plan, made, dest);
        ul::Add(plan, UL_OP_COPY, ul::JoinPath(package, relative), dest);
      }
    }
  }
  // wanted_index < 0 with a non-empty id means a remembered plugin the package
  // no longer has. The plan removes the old one and adds nothing, leaving the
  // game on its base — the same as choosing "No plugin", and the only honest
  // outcome when the chosen one is gone.
  return plan;
}

ul_plan* ul_plan_uninstall(const char* receipt_json, size_t length) {
  ul_receipt* receipt = ul_receipt_parse(receipt_json, length);
  if (!receipt) return nullptr;
  auto plan = new (std::nothrow) ul_plan();
  if (!plan) {
    ul_receipt_free(receipt);
    return nullptr;
  }
  // Where the package goes back to, and what it was installed from. Both come
  // out of the receipt, so an uninstall needs nothing but the record it is
  // undoing.
  const std::string game =
      ul::TrimTrailingSlash(ul::NormaliseSlashes(ul_receipt_game_dir(receipt)));
  const std::string package =
      ul::TrimTrailingSlash(ul::NormaliseSlashes(ul_receipt_package_dir(receipt)));

  // In reverse. The receipt is in the order things were written, and undoing it
  // backwards means a file restored over a folder that a later step created is
  // put back before that folder is considered — and it reads, in the progress
  // list, as the install running in reverse, which is what it is.
  std::set<std::string> made;
  for (int i = ul_receipt_count(receipt) - 1; i >= 0; --i) {
    const std::string dest = ul_receipt_dest(receipt, i);
    const std::string backup = ul_receipt_backup(receipt, i);

    // Back into the store rather than into nothing. The file is the package's,
    // it is 1.2 GB of package in total, and a delete here is what would make
    // the next install fetch all of it again.
    std::string relative;
    const std::string normalised = ul::NormaliseSlashes(dest);
    if (!game.empty() && !package.empty() &&
        ul::StartsWithNoCase(normalised, game + "/")) {
      relative = normalised.substr(game.size() + 1);
    }
    if (!relative.empty()) {
      const std::string home = ul::JoinPath(package, relative);
      ul::EnsureFolder(plan, made, home);
      ul::Add(plan, UL_OP_ARCHIVE, dest, home);
    } else if (backup.empty()) {
      // Nowhere to put it back to — an install whose receipt predates this, or
      // a file written outside the game folder. Deleting is still correct; it
      // just costs a download next time.
      ul::Add(plan, UL_OP_DELETE, {}, dest);
    }

    if (!backup.empty()) {
      // After the archive, never before: moving the mod's file out first is
      // what leaves the destination free for the original to come back to.
      ul::Add(plan, UL_OP_RESTORE, backup, dest);
    }
  }
  ul_receipt_free(receipt);
  return plan;
}

}  // extern "C"

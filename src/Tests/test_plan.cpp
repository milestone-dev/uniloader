// What gets copied, backed up, deleted and restored.
//
// The test that matters most in this file is
// uninstall_leaves_the_games_own_plugins_alone. A stock War2Combat install has
// AutoWarLat.w2p, CpuSaveC.w2p, PlaySound.w2p and lobby_map.w2p in it before
// this program has done anything, and a mod manager that removed those while
// "uninstalling the mod" would be worse than no mod manager.

#include "harness.hpp"
#include "uniloader/uniloader.h"

#include <string>
#include <vector>

namespace {

std::string NulList(const std::vector<std::string>& items) {
  std::string joined;
  for (const std::string& item : items) {
    joined.append(item);
    joined.push_back('\0');
  }
  joined.push_back('\0');
  return joined;
}

struct Step {
  int op;
  std::string src;
  std::string dest;
};

std::vector<Step> Steps(ul_plan* plan) {
  std::vector<Step> steps;
  for (int i = 0; i < ul_plan_count(plan); ++i) {
    steps.push_back(Step{ul_plan_op(plan, i), ul_plan_src(plan, i), ul_plan_dest(plan, i)});
  }
  return steps;
}

bool Has(const std::vector<Step>& steps, int op, const std::string& dest) {
  for (const Step& step : steps) {
    if (step.op == op && step.dest == dest) return true;
  }
  return false;
}

bool Mentions(const std::vector<Step>& steps, const std::string& dest) {
  for (const Step& step : steps) {
    if (step.dest == dest) return true;
  }
  return false;
}

constexpr char kGame[] = "C:/Program Files (x86)/War2Combat";
constexpr char kStore[] = "C:/Users/x/AppData/Local/UniLoader/v6.6";
constexpr char kBackup[] = "C:/Users/x/AppData/Local/UniLoader/backup";

}  // namespace

TEST(plan, install_copies_and_backs_up_what_it_overwrites) {
  const std::string package = NulList({"Unification.exe", "UniFiles/a.dat",
                                       "Data/maindat.war"});
  // maindat.war is already there — it is the game's own data file.
  const std::string existing = NulList({"Data/maindat.war"});
  ul_plan* plan = ul_plan_install(kStore, kGame, package.c_str(), existing.c_str(),
                                  kBackup);
  CHECK(plan != nullptr);
  const std::vector<Step> steps = Steps(plan);

  CHECK(Has(steps, UL_OP_COPY, std::string(kGame) + "/Unification.exe"));
  CHECK(Has(steps, UL_OP_BACKUP, std::string(kBackup) + "/Data/maindat.war"));
  CHECK(Has(steps, UL_OP_COPY, std::string(kGame) + "/Data/maindat.war"));
  // Nothing was there before Unification.exe, so nothing is backed up for it —
  // and the uninstall will therefore delete it rather than restore over it.
  CHECK(!Has(steps, UL_OP_BACKUP, std::string(kBackup) + "/Unification.exe"));
  ul_plan_free(plan);
}

TEST(plan, install_backs_up_before_it_overwrites) {
  const std::string package = NulList({"Data/maindat.war"});
  const std::string existing = NulList({"Data/maindat.war"});
  ul_plan* plan = ul_plan_install(kStore, kGame, package.c_str(), existing.c_str(),
                                  kBackup);
  const std::vector<Step> steps = Steps(plan);
  int backup_at = -1, copy_at = -1;
  for (size_t i = 0; i < steps.size(); ++i) {
    if (steps[i].op == UL_OP_BACKUP) backup_at = static_cast<int>(i);
    if (steps[i].op == UL_OP_COPY) copy_at = static_cast<int>(i);
  }
  // Order is the whole point of a plan being a list. A copy that ran first
  // would back up the file it had just written.
  CHECK(backup_at >= 0 && copy_at >= 0);
  CHECK(backup_at < copy_at);
  ul_plan_free(plan);
}

TEST(plan, install_leaves_the_plugin_catalogue_in_the_store) {
  const std::string package =
      NulList({"Unification.exe", "Plugins/2_Insane/insane.w2p", "Plugins/1_DAIFE/d.w2p",
               "base/info.txt", "base/1.png",
               "SCREENSHOTS & CREDITS, MAIN/shot1.png"});
  ul_plan* plan = ul_plan_install(kStore, kGame, package.c_str(), "\0", kBackup);
  const std::vector<Step> steps = Steps(plan);
  // The whole catalogue stays in the store and one plugin is copied in on
  // demand. That is what makes switching a copy and a delete, not a reinstall —
  // and it is why installing does not put nineteen plugins into the game folder
  // at once, which is the one arrangement the game cannot cope with. base/
  // stays for the same reason it exists: it is the mod describing itself to
  // the launcher, not files the game loads — and the press-kit folder stays
  // for the same reason again.
  for (const Step& step : steps) {
    CHECK(step.dest.find("/Plugins/") == std::string::npos);
    CHECK(step.dest.find("/base/") == std::string::npos);
    CHECK(step.dest.find("SCREENSHOTS") == std::string::npos);
  }
  CHECK(Has(steps, UL_OP_COPY, std::string(kGame) + "/Unification.exe"));
  ul_plan_free(plan);
}

TEST(plan, what_stays_in_store_is_one_answer) {
  // Shared by the install plan and the host's post-install pruning. When these
  // were two lists they drifted, and the pruning deleted a folder the install
  // had never copied — which no uninstall could ever bring back.
  CHECK_EQ(ul_path_stays_in_store("Plugins/2_Insane/insane.w2p"), 1);
  CHECK_EQ(ul_path_stays_in_store("Plugins"), 1);
  CHECK_EQ(ul_path_stays_in_store("base/info.txt"), 1);
  CHECK_EQ(ul_path_stays_in_store("base"), 1);
  CHECK_EQ(ul_path_stays_in_store("SCREENSHOTS & CREDITS, MAIN"), 1);
  CHECK_EQ(ul_path_stays_in_store("SCREENSHOTS & CREDITS, MAIN/1.png"), 1);
  CHECK_EQ(ul_path_stays_in_store("UniFiles/uni.dat"), 0);
  CHECK_EQ(ul_path_stays_in_store("Unification.exe"), 0);
  CHECK_EQ(ul_path_stays_in_store(""), 0);
}

TEST(plan, install_refuses_a_path_that_escapes) {
  const std::string package = NulList({"../../Windows/System32/evil.dll", "ok.txt"});
  ul_plan* plan = ul_plan_install(kStore, kGame, package.c_str(), "\0", kBackup);
  const std::vector<Step> steps = Steps(plan);
  for (const Step& step : steps) {
    CHECK(step.dest.find("System32") == std::string::npos);
  }
  CHECK(Has(steps, UL_OP_COPY, std::string(kGame) + "/ok.txt"));
  ul_plan_free(plan);
}

TEST(plan, finds_the_package_root_inside_a_wrapper_folder) {
  const std::string wrapped = NulList({"War2 Unification v6.6/Unification.exe",
                                       "War2 Unification v6.6/UniFiles/a.dat",
                                       "War2 Unification v6.6/Plugins/2_Insane/i.w2p"});
  char* root = ul_package_root(wrapped.c_str());
  CHECK_EQ(std::string(root ? root : ""), std::string("War2 Unification v6.6/"));
  ul_free(root);

  // And a package already at the root is not mistaken for a wrapped one.
  const std::string flat = NulList({"Unification.exe", "UniFiles/a.dat"});
  char* flat_root = ul_package_root(flat.c_str());
  CHECK_EQ(std::string(flat_root ? flat_root : ""), std::string());
  ul_free(flat_root);
}

TEST(plan, switching_plugins_removes_every_variant_of_the_old_one) {
  ul_catalogue* catalogue = ul_catalogue_create();
  // A difficulty pair: two alternatives in one folder, only ever one installed.
  ul_catalogue_add_path(catalogue, "Plugins/10_trolls/plugin trolls 1.w2p");
  ul_catalogue_add_path(catalogue, "Plugins/10_trolls/plugin trolls 2.w2p");
  ul_catalogue_add_path(catalogue, "Plugins/1_DAIFE/daife.w2p");
  ul_catalogue_finish(catalogue);

  ul_plan* plan = ul_plan_set_plugin(catalogue, kStore, kGame, "10_trolls", "1_DAIFE", 0);
  const std::vector<Step> steps = Steps(plan);
  // Both variants are deleted, not just the one that happened to be installed.
  // The plan is built without being told which one that was, and guessing wrong
  // would leave a .w2p the game still loads.
  CHECK(Has(steps, UL_OP_DELETE, std::string(kGame) + "/plugin/plugin trolls 1.w2p"));
  CHECK(Has(steps, UL_OP_DELETE, std::string(kGame) + "/plugin/plugin trolls 2.w2p"));
  CHECK(Has(steps, UL_OP_COPY, std::string(kGame) + "/plugin/daife.w2p"));
  ul_plan_free(plan);
  ul_catalogue_free(catalogue);
}

TEST(plan, installs_exactly_one_variant) {
  ul_catalogue* catalogue = ul_catalogue_create();
  ul_catalogue_add_path(catalogue, "Plugins/10_trolls/plugin trolls 1.w2p");
  ul_catalogue_add_path(catalogue, "Plugins/10_trolls/plugin trolls 2.w2p");
  ul_catalogue_finish(catalogue);

  // Hard, not normal. Copying both would be copying two mods over each other.
  ul_plan* plan = ul_plan_set_plugin(catalogue, kStore, kGame, "", "10_trolls", 1);
  const std::vector<Step> steps = Steps(plan);
  CHECK(Has(steps, UL_OP_COPY, std::string(kGame) + "/plugin/plugin trolls 2.w2p"));
  CHECK(!Has(steps, UL_OP_COPY, std::string(kGame) + "/plugin/plugin trolls 1.w2p"));
  ul_plan_free(plan);
  ul_catalogue_free(catalogue);
}

TEST(plan, switching_variant_within_a_plugin_replaces_it) {
  ul_catalogue* catalogue = ul_catalogue_create();
  ul_catalogue_add_path(catalogue, "Plugins/10_trolls/plugin trolls 1.w2p");
  ul_catalogue_add_path(catalogue, "Plugins/10_trolls/plugin trolls 2.w2p");
  ul_catalogue_finish(catalogue);

  ul_plan* plan = ul_plan_set_plugin(catalogue, kStore, kGame, "10_trolls", "10_trolls", 0);
  const std::vector<Step> steps = Steps(plan);
  // Both go, then the chosen one comes back. Changing from hard to normal is a
  // real change and cannot be a no-op just because the plugin is the same one.
  CHECK(Has(steps, UL_OP_DELETE, std::string(kGame) + "/plugin/plugin trolls 2.w2p"));
  CHECK(Has(steps, UL_OP_COPY, std::string(kGame) + "/plugin/plugin trolls 1.w2p"));
  ul_plan_free(plan);
  ul_catalogue_free(catalogue);
}

TEST(plan, only_the_plugin_file_is_installed) {
  ul_catalogue* catalogue = ul_catalogue_create();
  ul_catalogue_add_path(catalogue, "Plugins/10_trolls/plugin trolls 1.w2p");
  ul_catalogue_add_path(catalogue, "Plugins/10_trolls/plugin trolls 2.w2p");
  ul_catalogue_add_path(catalogue, "Plugins/10_trolls/0_TROLL_orig_files.zip");
  ul_catalogue_finish(catalogue);

  ul_plan* plan = ul_plan_set_plugin(catalogue, kStore, kGame, "", "10_trolls", 1);
  const std::vector<Step> steps = Steps(plan);
  // One copy, and it is the .w2p. dannyldd's own instructions are to send the
  // plugin file to the game's plugin folder, singular; his notes and his zip of
  // the originals stay in the store where they can be read.
  int copies = 0;
  for (const Step& step : steps) {
    if (step.op == UL_OP_COPY) ++copies;
  }
  CHECK_EQ(copies, 1);
  CHECK(Has(steps, UL_OP_COPY, std::string(kGame) + "/plugin/plugin trolls 2.w2p"));
  CHECK(!Mentions(steps, std::string(kGame) + "/plugin/0_TROLL_orig_files.zip"));
  ul_plan_free(plan);
  ul_catalogue_free(catalogue);
}

TEST(plan, a_variant_out_of_range_falls_back_to_the_first) {
  ul_catalogue* catalogue = ul_catalogue_create();
  ul_catalogue_add_path(catalogue, "Plugins/10_trolls/plugin trolls 1.w2p");
  ul_catalogue_finish(catalogue);
  // A remembered choice can outlive the release that had that many variants.
  // Installing nothing would be a plugin the user asked for silently absent.
  ul_plan* plan = ul_plan_set_plugin(catalogue, kStore, kGame, "", "10_trolls", 5);
  const std::vector<Step> steps = Steps(plan);
  CHECK(Has(steps, UL_OP_COPY, std::string(kGame) + "/plugin/plugin trolls 1.w2p"));
  ul_plan_free(plan);
  ul_catalogue_free(catalogue);
}

TEST(plan, no_plugin_is_a_real_choice) {
  ul_catalogue* catalogue = ul_catalogue_create();
  ul_catalogue_add_path(catalogue, "Plugins/2_Insane/insane.w2p");
  ul_catalogue_finish(catalogue);

  ul_plan* plan =
      ul_plan_set_plugin(catalogue, kStore, kGame, "2_Insane", ul_plugin_none_id(), 0);
  const std::vector<Step> steps = Steps(plan);
  CHECK(Has(steps, UL_OP_DELETE, std::string(kGame) + "/plugin/insane.w2p"));
  // Nothing is copied: "no plugin" is the mod's own base game, and it is
  // reached by removing, not by installing something called "none".
  for (const Step& step : steps) {
    CHECK(step.op != UL_OP_COPY);
  }
  ul_plan_free(plan);
  ul_catalogue_free(catalogue);
}

TEST(plan, no_plugin_cleans_up_every_plugin_the_catalogue_knows) {
  ul_catalogue* catalogue = ul_catalogue_create();
  ul_catalogue_add_path(catalogue, "Plugins/2_Insane/insane.w2p");
  ul_catalogue_add_path(catalogue, "Plugins/7_Trolls/plugin trolls 1.w2p");
  ul_catalogue_add_path(catalogue, "Plugins/7_Trolls/plugin trolls 2.w2p");
  ul_catalogue_finish(catalogue);

  // The receipt believes Insane is active, but "No plugin" is the row a person
  // reaches for when something has gone wrong — so it removes every plugin
  // file the catalogue can name, including ones a mistake left behind.
  ul_plan* plan =
      ul_plan_set_plugin(catalogue, kStore, kGame, "2_Insane", ul_plugin_none_id(), 0);
  const std::vector<Step> steps = Steps(plan);
  CHECK(Has(steps, UL_OP_DELETE, std::string(kGame) + "/plugin/insane.w2p"));
  CHECK(Has(steps, UL_OP_DELETE, std::string(kGame) + "/plugin/plugin trolls 1.w2p"));
  CHECK(Has(steps, UL_OP_DELETE, std::string(kGame) + "/plugin/plugin trolls 2.w2p"));
  ul_plan_free(plan);
  ul_catalogue_free(catalogue);

  // Switching to a *particular* plugin stays surgical: the active one goes,
  // the others are not touched — their absence is not this plan's business.
  ul_catalogue* again = ul_catalogue_create();
  ul_catalogue_add_path(again, "Plugins/2_Insane/insane.w2p");
  ul_catalogue_add_path(again, "Plugins/7_Trolls/plugin trolls 1.w2p");
  ul_catalogue_add_path(again, "Plugins/1_DAIFE/daife.w2p");
  ul_catalogue_finish(again);
  ul_plan* narrow = ul_plan_set_plugin(again, kStore, kGame, "2_Insane", "1_DAIFE", 0);
  const std::vector<Step> surgical = Steps(narrow);
  CHECK(Has(surgical, UL_OP_DELETE, std::string(kGame) + "/plugin/insane.w2p"));
  for (const Step& step : surgical) {
    CHECK(step.dest.find("trolls") == std::string::npos);
  }
  ul_plan_free(narrow);
  ul_catalogue_free(again);
}

TEST(plan, a_remembered_plugin_that_vanished_falls_back_to_none) {
  ul_catalogue* catalogue = ul_catalogue_create();
  ul_catalogue_add_path(catalogue, "Plugins/1_DAIFE/daife.w2p");
  ul_catalogue_finish(catalogue);
  // The update renamed or dropped 2_Insane. Nothing is copied, and nothing can
  // be deleted either, because the catalogue no longer knows what it had.
  ul_plan* plan = ul_plan_set_plugin(catalogue, kStore, kGame, "9_Gone", "9_Gone", 0);
  CHECK(plan != nullptr);
  CHECK_EQ(ul_plan_count(plan), 0);
  ul_plan_free(plan);
  ul_catalogue_free(catalogue);
}

TEST(plan, a_plugin_of_only_screenshots_installs_nothing) {
  ul_catalogue* catalogue = ul_catalogue_create();
  ul_catalogue_add_path(catalogue, "Plugins/9_Preview/1.png");
  ul_catalogue_finish(catalogue);
  ul_plan* plan = ul_plan_set_plugin(catalogue, kStore, kGame, "", "9_Preview", 0);
  // Not an error: a folder with nothing to load simply has nothing to copy.
  CHECK(plan != nullptr);
  CHECK_EQ(ul_plan_count(plan), 0);
  ul_plan_free(plan);
  ul_catalogue_free(catalogue);
}

TEST(plan, a_variant_choice_round_trips_through_the_receipt) {
  ul_receipt* receipt = ul_receipt_create("v6.6");
  ul_receipt_set_plugin(receipt, "10_trolls");
  ul_receipt_set_variant(receipt, 1);
  size_t length = 0;
  char* json = ul_receipt_to_json(receipt, &length);
  ul_receipt_free(receipt);

  ul_receipt* read = ul_receipt_parse(json, length);
  ul_free(json);
  CHECK(read != nullptr);
  // The next run reopens on the difficulty the user chose, not on the first one.
  CHECK_EQ(std::string(ul_receipt_plugin(read)), std::string("10_trolls"));
  CHECK_EQ(ul_receipt_variant(read), 1);
  ul_receipt_free(read);
}

TEST(plan, uninstall_restores_what_it_replaced_and_deletes_what_it_added) {
  ul_receipt* receipt = ul_receipt_create("v6.6");
  ul_receipt_add(receipt, "C:/War2Combat/Unification.exe", "");
  ul_receipt_add(receipt, "C:/War2Combat/Data/maindat.war",
                 "C:/store/backup/Data/maindat.war");
  size_t length = 0;
  char* json = ul_receipt_to_json(receipt, &length);
  ul_receipt_free(receipt);
  CHECK(json != nullptr);

  ul_plan* plan = ul_plan_uninstall(json, length);
  ul_free(json);
  CHECK(plan != nullptr);
  const std::vector<Step> steps = Steps(plan);
  CHECK(Has(steps, UL_OP_DELETE, std::string("C:/War2Combat/Unification.exe")));
  CHECK(Has(steps, UL_OP_RESTORE, std::string("C:/War2Combat/Data/maindat.war")));
  ul_plan_free(plan);
}

TEST(plan, uninstall_moves_the_package_back_into_the_store) {
  ul_receipt* receipt = ul_receipt_create("v6.6");
  ul_receipt_set_game_dir(receipt, kGame);
  ul_receipt_set_package_dir(receipt, kStore);
  ul_receipt_add(receipt, (std::string(kGame) + "/Unification.exe").c_str(), "");
  ul_receipt_add(receipt, (std::string(kGame) + "/UniFiles/a.dat").c_str(), "");
  size_t length = 0;
  char* json = ul_receipt_to_json(receipt, &length);
  ul_receipt_free(receipt);

  ul_plan* plan = ul_plan_uninstall(json, length);
  ul_free(json);
  const std::vector<Step> steps = Steps(plan);

  // Moved back, not deleted. The package is 1.2 GB: deleting it means the next
  // install fetches 800 MB again, and keeping a second copy in the store while
  // it is installed doubles the space for nothing. Moving is neither.
  CHECK(Has(steps, UL_OP_ARCHIVE, std::string(kStore) + "/Unification.exe"));
  CHECK(Has(steps, UL_OP_ARCHIVE, std::string(kStore) + "/UniFiles/a.dat"));
  // And it goes back under the same relative path it was installed from.
  CHECK(Has(steps, UL_OP_MKDIR, std::string(kStore) + "/UniFiles"));
  for (const Step& step : steps) {
    CHECK(step.op != UL_OP_DELETE);
  }
  ul_plan_free(plan);
}

TEST(plan, a_replaced_file_is_moved_back_before_the_original_returns) {
  ul_receipt* receipt = ul_receipt_create("v6.6");
  ul_receipt_set_game_dir(receipt, kGame);
  ul_receipt_set_package_dir(receipt, kStore);
  ul_receipt_add(receipt, (std::string(kGame) + "/Data/maindat.war").c_str(),
                 (std::string(kBackup) + "/Data/maindat.war").c_str());
  size_t length = 0;
  char* json = ul_receipt_to_json(receipt, &length);
  ul_receipt_free(receipt);

  ul_plan* plan = ul_plan_uninstall(json, length);
  ul_free(json);
  const std::vector<Step> steps = Steps(plan);

  int archive_at = -1, restore_at = -1;
  for (size_t i = 0; i < steps.size(); ++i) {
    if (steps[i].op == UL_OP_ARCHIVE) archive_at = static_cast<int>(i);
    if (steps[i].op == UL_OP_RESTORE) restore_at = static_cast<int>(i);
  }
  // The mod's copy goes home first, then the game's own comes back over the
  // space it left. The other order would restore onto an occupied path and lose
  // the mod's file, which is the one the store still needs.
  CHECK(archive_at >= 0 && restore_at >= 0);
  CHECK(archive_at < restore_at);
  ul_plan_free(plan);
}

TEST(plan, an_old_receipt_with_no_package_still_deletes) {
  // Receipts written before the store learned to take files back name no
  // package folder. There is nowhere to move to, so removing is still right —
  // it just costs a download next time.
  ul_receipt* receipt = ul_receipt_create("v6.5");
  ul_receipt_add(receipt, (std::string(kGame) + "/Unification.exe").c_str(), "");
  size_t length = 0;
  char* json = ul_receipt_to_json(receipt, &length);
  ul_receipt_free(receipt);

  ul_plan* plan = ul_plan_uninstall(json, length);
  ul_free(json);
  CHECK(Has(Steps(plan), UL_OP_DELETE, std::string(kGame) + "/Unification.exe"));
  ul_plan_free(plan);
}

TEST(plan, uninstall_leaves_the_games_own_plugins_alone) {
  // A stock War2Combat has these four .w2p files before this program has done
  // anything. Nothing is removed unless the receipt says the manager wrote it,
  // which is the whole reason a receipt exists rather than a pattern match.
  ul_receipt* receipt = ul_receipt_create("v6.6");
  ul_receipt_add(receipt, "C:/War2Combat/Plugins/insane.w2p", "");
  size_t length = 0;
  char* json = ul_receipt_to_json(receipt, &length);
  ul_receipt_free(receipt);

  ul_plan* plan = ul_plan_uninstall(json, length);
  ul_free(json);
  const std::vector<Step> steps = Steps(plan);
  for (const char* stock : {"AutoWarLat.w2p", "CpuSaveC.w2p", "PlaySound.w2p",
                            "lobby_map.w2p", "war2mod.w2p"}) {
    CHECK(!Mentions(steps, std::string("C:/War2Combat/plugin/") + stock));
    CHECK(!Mentions(steps, std::string("C:/War2Combat/Plugins/") + stock));
  }
  CHECK_EQ(ul_plan_count(plan), 1);
  ul_plan_free(plan);
}

TEST(plan, uninstall_runs_the_install_backwards) {
  ul_receipt* receipt = ul_receipt_create("v6.6");
  ul_receipt_add(receipt, "C:/War2Combat/a.txt", "");
  ul_receipt_add(receipt, "C:/War2Combat/b.txt", "");
  ul_receipt_add(receipt, "C:/War2Combat/c.txt", "");
  size_t length = 0;
  char* json = ul_receipt_to_json(receipt, &length);
  ul_receipt_free(receipt);
  ul_plan* plan = ul_plan_uninstall(json, length);
  ul_free(json);
  const std::vector<Step> steps = Steps(plan);
  CHECK_EQ(static_cast<int>(steps.size()), 3);
  CHECK_EQ(steps[0].dest, std::string("C:/War2Combat/c.txt"));
  CHECK_EQ(steps[2].dest, std::string("C:/War2Combat/a.txt"));
  ul_plan_free(plan);
}

TEST(plan, a_receipt_round_trips) {
  ul_receipt* receipt = ul_receipt_create("v6.6");
  ul_receipt_set_plugin(receipt, "2_Insane");
  // A Windows path is full of backslashes, which is the one thing a
  // hand-written JSON writer gets wrong.
  ul_receipt_set_game_dir(receipt, "C:\\Program Files (x86)\\War2Combat");
  ul_receipt_set_package_dir(receipt, "C:\\Users\\x\\AppData\\Local\\UniLoader\\v6.6");
  ul_receipt_add(receipt, "C:\\War2Combat\\Data\\maindat.war",
                 "C:\\store\\backup\\Data\\maindat.war");
  size_t length = 0;
  char* json = ul_receipt_to_json(receipt, &length);
  ul_receipt_free(receipt);
  CHECK(json != nullptr);

  ul_receipt* read = ul_receipt_parse(json, length);
  ul_free(json);
  CHECK(read != nullptr);
  CHECK_EQ(std::string(ul_receipt_mod_version(read)), std::string("v6.6"));
  CHECK_EQ(std::string(ul_receipt_plugin(read)), std::string("2_Insane"));
  CHECK_EQ(std::string(ul_receipt_game_dir(read)),
           std::string("C:\\Program Files (x86)\\War2Combat"));
  CHECK_EQ(ul_receipt_count(read), 1);
  CHECK_EQ(std::string(ul_receipt_dest(read, 0)),
           std::string("C:\\War2Combat\\Data\\maindat.war"));
  ul_receipt_free(read);
}

TEST(plan, a_receipt_from_a_newer_build_is_refused_not_half_read) {
  // Half-understanding a receipt means an uninstall that silently misses files
  // and leaves the game folder modified and unclaimed.
  const std::string future = R"({"uniloader_receipt": 99, "files": []})";
  CHECK(ul_receipt_parse(future.data(), future.size()) == nullptr);
  CHECK(std::string(ul_last_error()).find("newer version") != std::string::npos);

  const std::string alien = R"({"something": "else"})";
  CHECK(ul_receipt_parse(alien.data(), alien.size()) == nullptr);
}

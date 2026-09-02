// The whole pipeline, on a real filesystem: unpack, install, switch plugin,
// switch variant, uninstall — and then check that the game folder is byte for
// byte what it was before any of it happened.
//
// This is the test the program exists to pass. Everything else here checks a
// decision; this one checks that the decisions, run in order against real
// files, leave nothing behind. It is Windows-only because running a plan is the
// host's job and the host is Win32 — Files.cpp is compiled into the test binary
// for exactly this, the way PUDForge compiles its Win32-free editor into its own.
//
// It never touches a real War2Combat. A pretend game folder is built in the
// temporary directory with the four stock .w2p files a real install has, and
// the point of the last assertion is that those four are still there.

#ifdef _WIN32

#include "harness.hpp"
#include "uniloader/uniloader.h"

#include "../UniLoaderWin/Files.hpp"
#include "../UniLoaderWin/Strings.hpp"

#include <windows.h>

#include <map>
#include <string>
#include <vector>

namespace {

using namespace ulwin;

std::wstring TempRoot() {
  wchar_t buffer[MAX_PATH] = {};
  GetTempPathW(MAX_PATH, buffer);
  return std::wstring(buffer) + L"uniloader-test";
}

void Write(const std::wstring& path, const std::string& contents) {
  const size_t slash = path.find_last_of(L"\\/");
  if (slash != std::wstring::npos) EnsureFolder(path.substr(0, slash));
  CHECK(WriteTextFile(path, contents));
}

std::string Read(const std::wstring& path) { return ToUtf8(ReadTextFile(path)); }

/// Every file under `root` with its contents — which is what "unchanged" has to
/// be checked against. Comparing names alone would pass an uninstall that put
/// a file back with the mod's contents in it.
std::map<std::string, std::string> Snapshot(const std::wstring& root) {
  std::map<std::string, std::string> tree;
  for (const std::string& relative : WalkFiles(root)) {
    tree[relative] = Read(root + L"\\" + FromUtf8(relative));
  }
  return tree;
}

std::string NulList(const std::vector<std::string>& items) {
  std::string joined;
  for (const std::string& item : items) {
    joined.append(item);
    joined.push_back('\0');
  }
  joined.push_back('\0');
  return joined;
}

/// A pretend War2Combat: the files a stock install has before UniLoader has
/// done anything. maindat.war is here because the package replaces it, and the
/// four .w2p files are here because nothing must ever remove them.
std::wstring MakeGameFolder(const std::wstring& root) {
  const std::wstring game = root + L"\\War2Combat";
  RemoveTree(game);
  Write(game + L"\\war2.exe", "the game");
  Write(game + L"\\Data\\maindat.war", "STOCK MAINDAT");
  for (const char* stock : {"AutoWarLat.w2p", "CpuSaveC.w2p", "PlaySound.w2p",
                            "lobby_map.w2p"}) {
    Write(game + L"\\plugin\\" + FromUtf8(stock), std::string("stock ") + stock);
  }
  return game;
}

struct Installed {
  std::wstring package;      // where the package's root turned out to be
  std::string receipt;       // the JSON, as written
};

/// Unpacks the fixture and runs the install plan, exactly as the client does.
Installed Install(const std::wstring& root, const std::wstring& game) {
  const std::string archive = ToUtf8(FromUtf8(ult::Root()) + L"/test/fixtures/package.rar");
  ul_archive* handle = ul_archive_open(archive.c_str());
  if (!handle) ult::Skip("no package.rar fixture");

  const std::wstring unpacked = root + L"\\store\\package";
  RemoveTree(unpacked);
  EnsureFolder(unpacked);
  CHECK_EQ(ul_archive_extract(handle, ToUtf8(unpacked).c_str(), nullptr, nullptr), UL_OK);
  ul_archive_close(handle);

  // The fixture wraps everything in "War2 Unification v9.9/", which is how
  // dannyldd's packages have shipped about as often as not.
  char* root_prefix = ul_package_root(NulList(WalkFiles(unpacked)).c_str());
  const std::string prefix = root_prefix ? root_prefix : "";
  ul_free(root_prefix);
  CHECK_EQ(prefix, std::string("War2 Unification v9.9/"));

  Installed result;
  result.package = unpacked + L"\\" + FromUtf8(prefix);
  while (!result.package.empty() && result.package.back() == L'\\') {
    result.package.pop_back();
  }

  // The base/ folder — the mod describing itself — is not in the fixture
  // archive, because dannyldd has not shipped one yet. Added to the unpacked
  // tree here, before the plan is built, so the pipeline is exercised the way
  // it will run the day a package carries it: present in the walk, kept out of
  // the game folder, read into the catalogue.
  Write(result.package + L"\\base\\info.txt",
        "The Unification mod itself.\nhttps://youtu.be/MO3MIWIPrkY\n");
  Write(result.package + L"\\base\\1.png", "pretend png");

  ul_plan* plan = ul_plan_install(
      ToUtf8(result.package).c_str(), ToUtf8(game).c_str(),
      NulList(WalkFiles(result.package)).c_str(), NulList(WalkFiles(game)).c_str(),
      ToUtf8(root + L"\\store\\backup").c_str());
  ul_receipt* receipt = ul_receipt_create("v9.9");
  ul_receipt_set_game_dir(receipt, ToUtf8(game).c_str());
  ul_receipt_set_package_dir(receipt, ToUtf8(result.package).c_str());

  std::wstring error;
  const int ran = RunPlan(plan, receipt, nullptr, error);
  ul_plan_free(plan);
  ::ult::Check(ran == UL_OK, "install plan ran", __FILE__, __LINE__, ToUtf8(error));

  size_t length = 0;
  char* json = ul_receipt_to_json(receipt, &length);
  result.receipt.assign(json ? json : "", length);
  ul_free(json);
  ul_receipt_free(receipt);
  return result;
}

ul_catalogue* ReadCatalogue(const std::wstring& package) {
  ul_catalogue* catalogue = ul_catalogue_create();
  const std::vector<std::string> paths = WalkFiles(package);
  for (const std::string& path : paths) ul_catalogue_add_path(catalogue, path.c_str());
  // Mirrors the client's BuildCatalogue: a folder's own top-level info.txt is
  // read and handed in, with the empty id for base/'s.
  for (const std::string& path : paths) {
    std::string id;
    if (_strnicmp(path.c_str(), "Plugins/", 8) == 0) {
      const size_t second = path.find('/', 8);
      if (second == std::string::npos) continue;
      if (_stricmp(path.substr(second + 1).c_str(), "info.txt") != 0) continue;
      id = path.substr(8, second - 8);
    } else if (_stricmp(path.c_str(), "base/info.txt") != 0) {
      continue;
    }
    const std::string text = Read(package + L"\\" + FromUtf8(path));
    ul_catalogue_add_info(catalogue, id.c_str(), text.data(), text.size());
  }
  ul_catalogue_finish(catalogue);
  return catalogue;
}

void SetPlugin(ul_catalogue* catalogue, const std::wstring& package,
               const std::wstring& game, const char* from, const char* to, int variant) {
  ul_plan* plan = ul_plan_set_plugin(catalogue, ToUtf8(package).c_str(),
                                     ToUtf8(game).c_str(), from, to, variant);
  std::wstring error;
  const int ran = RunPlan(plan, nullptr, nullptr, error);
  ul_plan_free(plan);
  ::ult::Check(ran == UL_OK, "plugin plan ran", __FILE__, __LINE__, ToUtf8(error));
}

}  // namespace

TEST(install, install_then_uninstall_leaves_the_game_folder_untouched) {
  const std::wstring root = TempRoot();
  RemoveTree(root);
  const std::wstring game = MakeGameFolder(root);
  const std::map<std::string, std::string> before = Snapshot(game);

  const Installed installed = Install(root, game);

  // The package landed, the wrapper folder did not come with it, and the
  // game's own maindat.war has been replaced by the mod's.
  CHECK(FileExists(game + L"\\Unification.exe"));
  CHECK(FileExists(game + L"\\UniFiles\\uni.dat"));
  CHECK(!FolderExists(game + L"\\War2 Unification v9.9"));
  CHECK_EQ(Read(game + L"\\Data\\maindat.war"), std::string("MOD MAINDAT\n"));
  // And the file it replaced was set aside rather than lost.
  CHECK_EQ(Read(root + L"\\store\\backup\\Data\\maindat.war"),
           std::string("STOCK MAINDAT"));
  // The plugin catalogue stayed in the store: installing must not drop
  // nineteen plugins into the game folder at once. And so did base/ — the
  // mod's self-description is the launcher's to show, not the game's to hold.
  CHECK(!FolderExists(game + L"\\Plugins\\0_basegame"));
  CHECK(!FolderExists(game + L"\\base"));

  ul_plan* undo = ul_plan_uninstall(installed.receipt.data(), installed.receipt.size());
  CHECK(undo != nullptr);
  std::wstring error;
  const int ran = RunPlan(undo, nullptr, nullptr, error);
  ul_plan_free(undo);
  ult::Check(ran == UL_OK, "uninstall plan ran", __FILE__, __LINE__, ToUtf8(error));

  // The assertion the whole design is for: same files, same contents, nothing
  // added and nothing missing.
  const std::map<std::string, std::string> after = Snapshot(game);
  CHECK_EQ(static_cast<int>(after.size()), static_cast<int>(before.size()));
  CHECK(after == before);
  RemoveTree(root);
}

TEST(install, uninstall_keeps_the_games_own_plugin_files) {
  const std::wstring root = TempRoot();
  RemoveTree(root);
  const std::wstring game = MakeGameFolder(root);
  const Installed installed = Install(root, game);

  ul_catalogue* catalogue = ReadCatalogue(installed.package);
  SetPlugin(catalogue, installed.package, game, "", "7_Trolls", 1);
  CHECK(FileExists(game + L"\\plugin\\plugin trolls 2.w2p"));

  ul_plan* undo = ul_plan_uninstall(installed.receipt.data(), installed.receipt.size());
  std::wstring error;
  RunPlan(undo, nullptr, nullptr, error);
  ul_plan_free(undo);

  // The four the game shipped with sit in the same plugin\ folder the mod's own
  // plugin was just copied into, and are not in the receipt. An uninstall that
  // matched on "*.w2p", or that emptied the folder, would have taken them.
  for (const char* stock : {"AutoWarLat.w2p", "CpuSaveC.w2p", "PlaySound.w2p",
                            "lobby_map.w2p"}) {
    CHECK(FileExists(game + L"\\plugin\\" + FromUtf8(stock)));
  }
  ul_catalogue_free(catalogue);
  RemoveTree(root);
}

TEST(install, switching_variant_replaces_rather_than_stacks) {
  const std::wstring root = TempRoot();
  RemoveTree(root);
  const std::wstring game = MakeGameFolder(root);
  const Installed installed = Install(root, game);
  ul_catalogue* catalogue = ReadCatalogue(installed.package);

  SetPlugin(catalogue, installed.package, game, "", "7_Trolls", 0);
  CHECK(FileExists(game + L"\\plugin\\plugin trolls 1.w2p"));
  CHECK(!FileExists(game + L"\\plugin\\plugin trolls 2.w2p"));

  // Normal to hard. Both files exist in the package and only one may ever be in
  // the game folder — leaving the other behind is the game loading the
  // difficulty the player just changed away from.
  SetPlugin(catalogue, installed.package, game, "7_Trolls", "7_Trolls", 1);
  CHECK(FileExists(game + L"\\plugin\\plugin trolls 2.w2p"));
  CHECK(!FileExists(game + L"\\plugin\\plugin trolls 1.w2p"));

  // And on to another plugin: nothing of the trolls is left.
  SetPlugin(catalogue, installed.package, game, "7_Trolls", "0_basegame", 0);
  CHECK(FileExists(game + L"\\plugin\\base.w2p"));
  CHECK(!FileExists(game + L"\\plugin\\plugin trolls 1.w2p"));
  CHECK(!FileExists(game + L"\\plugin\\plugin trolls 2.w2p"));

  // And "No plugin" removes without adding.
  SetPlugin(catalogue, installed.package, game, "0_basegame", ul_plugin_none_id(), 0);
  CHECK(!FileExists(game + L"\\Plugins\\base.w2p"));

  ul_catalogue_free(catalogue);
  RemoveTree(root);
}

TEST(install, the_catalogue_reads_the_packages_own_info_files) {
  const std::wstring root = TempRoot();
  RemoveTree(root);
  const std::wstring game = MakeGameFolder(root);
  const Installed installed = Install(root, game);
  ul_catalogue* catalogue = ReadCatalogue(installed.package);

  CHECK_EQ(ul_catalogue_count(catalogue), 2);
  CHECK_EQ(std::string(ul_plugin_id(catalogue, 0)), std::string("0_basegame"));
  // The folder names the plugin; its info.txt is the description, not a name.
  CHECK_EQ(std::string(ul_plugin_name(catalogue, 0)), std::string("Basegame"));
  CHECK_EQ(ul_plugin_has_info(catalogue, 0), 1);
  CHECK(std::string(ul_plugin_description(catalogue, 0)).find("nothing else") !=
        std::string::npos);
  CHECK_EQ(ul_plugin_image_count(catalogue, 0), 1);
  // The one with a difficulty choice, read off the disk rather than described
  // to the catalogue by hand.
  const int trolls = ul_catalogue_find(catalogue, "7_Trolls");
  CHECK(trolls >= 0);
  CHECK_EQ(ul_plugin_variant_count(catalogue, trolls), 2);
  CHECK_EQ(std::string(ul_plugin_name(catalogue, trolls)), std::string("Trolls"));
  // The description keeps the author's note about which file is which
  // difficulty, which is the only place that mapping is written down.
  CHECK(std::string(ul_plugin_description(catalogue, trolls)).find("hard") !=
        std::string::npos);
  // And the links in that prose became videos in the gallery, in the order the
  // author wrote them.
  CHECK_EQ(ul_plugin_video_count(catalogue, trolls), 2);
  CHECK_EQ(std::string(ul_plugin_video_id(catalogue, trolls, 0)),
           std::string("MO3MIWIPrkY"));
  CHECK_EQ(std::string(ul_plugin_video_id(catalogue, trolls, 1)),
           std::string("hpcbE_40i9c"));
  CHECK_EQ(std::string(ul_plugin_video_url(catalogue, trolls, 0)),
           std::string("https://www.youtube.com/watch?v=MO3MIWIPrkY"));

  // The base/ folder written into the package reached the mod's own gallery,
  // and never reached the plugin list.
  CHECK_EQ(std::string(ul_base_description(catalogue)),
           std::string("The Unification mod itself.\nhttps://youtu.be/MO3MIWIPrkY"));
  CHECK_EQ(ul_base_image_count(catalogue), 1);
  CHECK_EQ(std::string(ul_base_image(catalogue, 0)), std::string("base/1.png"));
  CHECK_EQ(ul_base_video_count(catalogue), 1);
  CHECK_EQ(std::string(ul_base_video_id(catalogue, 0)), std::string("MO3MIWIPrkY"));

  ul_catalogue_free(catalogue);
  RemoveTree(root);
}

#endif  // _WIN32

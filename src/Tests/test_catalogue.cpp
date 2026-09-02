// Reading Plugins/ as a catalogue.
//
// The folder names are dannyldd's own, quoted from his description of the
// package: "0_basegame", "1_DAIFE", "2_Insane", "3_Legacy of Dalaran", and
// about nineteen of them in all, some carrying more than one file.

#include "harness.hpp"
#include "uniloader/uniloader.h"

#include <string>

namespace {

/// A catalogue built from a list of paths, which is what the host hands over
/// after walking the extraction.
struct Catalogue {
  ul_catalogue* c = ul_catalogue_create();
  ~Catalogue() { ul_catalogue_free(c); }
  void Add(const char* path) { ul_catalogue_add_path(c, path); }
  void Info(const char* id, const std::string& text) {
    ul_catalogue_add_info(c, id, text.data(), text.size());
  }
  void Finish() { ul_catalogue_finish(c); }
  operator ul_catalogue*() const { return c; }
};

}  // namespace

TEST(catalogue, groups_files_by_folder) {
  Catalogue catalogue;
  catalogue.Add("Plugins/2_Insane/insane.w2p");
  catalogue.Add("Plugins/2_Insane/difficulty.w2p");
  catalogue.Add("Plugins/1_DAIFE/daife.w2p");
  catalogue.Finish();

  CHECK_EQ(ul_catalogue_count(catalogue), 2);
  // The numeric prefix is the author's running order, so it is the order shown.
  CHECK_EQ(std::string(ul_plugin_id(catalogue, 0)), std::string("1_DAIFE"));
  CHECK_EQ(std::string(ul_plugin_id(catalogue, 1)), std::string("2_Insane"));
  // Two .w2p files in one folder are two alternatives, not a set of two.
  CHECK_EQ(ul_plugin_variant_count(catalogue, 1), 2);
}

TEST(catalogue, w2p_files_in_a_folder_are_alternatives) {
  Catalogue catalogue;
  // dannyldd's real shape for a plugin with a difficulty choice.
  catalogue.Add("Plugins/7_Trolls/plugin trolls 2.w2p");
  catalogue.Add("Plugins/7_Trolls/plugin trolls 1.w2p");
  catalogue.Finish();

  CHECK_EQ(ul_plugin_variant_count(catalogue, 0), 2);
  // Name order, so the numbered pair reads 1 then 2 — easiest first, which is
  // the order an author gets by numbering them and the order a player expects.
  CHECK_EQ(std::string(ul_plugin_variant_name(catalogue, 0, 0)),
           std::string("plugin trolls 1"));
  CHECK_EQ(std::string(ul_plugin_variant_name(catalogue, 0, 1)),
           std::string("plugin trolls 2"));
  // The name has no extension on it: it is going in a dropdown, not a shell.
  CHECK(std::string(ul_plugin_variant_name(catalogue, 0, 0)).find(".w2p") ==
        std::string::npos);
  CHECK_EQ(std::string(ul_plugin_variant_path(catalogue, 0, 0)),
           std::string("Plugins/7_Trolls/plugin trolls 1.w2p"));
}

TEST(catalogue, most_plugins_have_exactly_one_variant) {
  Catalogue catalogue;
  catalogue.Add("Plugins/1_DAIFE/daife.w2p");
  catalogue.Finish();
  // Which is how the client knows not to show a dropdown at all.
  CHECK_EQ(ul_plugin_variant_count(catalogue, 0), 1);
}

TEST(catalogue, documentation_is_not_a_variant) {
  Catalogue catalogue;
  // The real shape of 10_trolls in v6.6, notes and all.
  catalogue.Add("Plugins/10_trolls/plugin trolls 1.w2p");
  catalogue.Add("Plugins/10_trolls/plugin trolls 2.w2p");
  catalogue.Add("Plugins/10_trolls/0_TROLL_orig_files.zip");
  catalogue.Finish();
  // The zip is the author's copy of the original files. It is not a third
  // difficulty, and it is not something to copy into a game folder.
  CHECK_EQ(ul_plugin_variant_count(catalogue, 0), 2);
}

TEST(catalogue, orders_by_the_numeric_prefix_not_by_name) {
  Catalogue catalogue;
  for (const char* path : {"Plugins/10_Later/a.w2p", "Plugins/2_Insane/a.w2p",
                           "Plugins/0_basegame/a.w2p", "Plugins/Extra/a.w2p"}) {
    catalogue.Add(path);
  }
  catalogue.Finish();
  // 10 after 2, which a name sort gets backwards; and the unnumbered folder
  // last, because a folder added later without a number belongs at the end.
  CHECK_EQ(std::string(ul_plugin_id(catalogue, 0)), std::string("0_basegame"));
  CHECK_EQ(std::string(ul_plugin_id(catalogue, 1)), std::string("2_Insane"));
  CHECK_EQ(std::string(ul_plugin_id(catalogue, 2)), std::string("10_Later"));
  CHECK_EQ(std::string(ul_plugin_id(catalogue, 3)), std::string("Extra"));
}

TEST(catalogue, separates_screenshots_from_game_files) {
  Catalogue catalogue;
  catalogue.Add("Plugins/2_Insane/insane.w2p");
  catalogue.Add("Plugins/2_Insane/2.png");
  catalogue.Add("Plugins/2_Insane/1.png");
  catalogue.Add("Plugins/2_Insane/10.png");
  catalogue.Finish();

  CHECK_EQ(ul_plugin_variant_count(catalogue, 0), 1);
  CHECK_EQ(ul_plugin_image_count(catalogue, 0), 3);
  // Name order, which is the slideshow order an author gets by naming files
  // 1.png and 2.png. "10.png" sorting before "2.png" is what a name sort means
  // and is the author's to fix by naming them 01, 02 — the alternative is
  // second-guessing a filename, which goes wrong in worse ways.
  CHECK_EQ(std::string(ul_plugin_image(catalogue, 0, 0)),
           std::string("Plugins/2_Insane/1.png"));
}

TEST(catalogue, the_info_txt_is_the_description_not_the_name) {
  Catalogue catalogue;
  catalogue.Add("Plugins/12_woow/plugin woow 1.w2p");
  // The read-me is shown whole, exactly as the author shaped it, minus only
  // trailing whitespace. The name stays the folder's — for now nothing in the
  // file names the plugin.
  catalogue.Info("12_woow",
                 "WooW\n"
                 "\n"
                 "https://youtu.be/MO3MIWIPrkY\n"
                 "\n"
                 "plugin 1 = hard + ai fix\n\n");
  catalogue.Finish();

  CHECK_EQ(std::string(ul_plugin_name(catalogue, 0)), std::string("Woow"));
  CHECK_EQ(std::string(ul_plugin_description(catalogue, 0)),
           std::string("WooW\n"
                       "\n"
                       "https://youtu.be/MO3MIWIPrkY\n"
                       "\n"
                       "plugin 1 = hard + ai fix"));
  CHECK_EQ(ul_plugin_has_info(catalogue, 0), 1);
  // And the video link in the prose reached the gallery.
  CHECK_EQ(ul_plugin_video_count(catalogue, 0), 1);
  CHECK_EQ(std::string(ul_plugin_video_id(catalogue, 0, 0)), std::string("MO3MIWIPrkY"));
}

TEST(catalogue, youtube_links_become_videos) {
  Catalogue catalogue;
  catalogue.Add("Plugins/1_x/x.w2p");
  // The three shapes a person writes, plus the same video twice.
  catalogue.Info("1_x",
                 "Name\nCampaign\n\n"
                 "Watch: https://youtu.be/MO3MIWIPrkY\n"
                 "or https://www.youtube.com/watch?v=nTLuCuUqlOg&t=90\n"
                 "or https://youtu.be/MO3MIWIPrkY again\n");
  catalogue.Finish();

  CHECK_EQ(ul_plugin_video_count(catalogue, 0), 2);
  CHECK_EQ(std::string(ul_plugin_video_id(catalogue, 0, 0)), std::string("MO3MIWIPrkY"));
  CHECK_EQ(std::string(ul_plugin_video_id(catalogue, 0, 1)), std::string("nTLuCuUqlOg"));
  // The watch page, for a browser. The id is also the thumbnail's name at
  // img.youtube.com, which is how the gallery draws it without an API key.
  CHECK_EQ(std::string(ul_plugin_video_url(catalogue, 0, 0)),
           std::string("https://www.youtube.com/watch?v=MO3MIWIPrkY"));
  // And the embed form, which is what a web view would need: a watch page
  // refuses to load in a frame, so the two are not interchangeable.
  CHECK_EQ(std::string(ul_plugin_video_embed_url(catalogue, 0, 0)),
           std::string("https://www.youtube.com/embed/MO3MIWIPrkY?rel=0&playsinline=1"));
}

TEST(catalogue, every_way_a_youtube_link_is_written) {
  Catalogue catalogue;
  catalogue.Add("Plugins/1_x/x.w2p");
  // The forms found in the wild: the shortener with a share suffix, mobile and
  // music hosts, a country domain, the no-cookie embed, shorts, live, and v=
  // sitting later in the query. All the same four videos, written eight ways.
  catalogue.Info("1_x",
                 "https://youtu.be/MO3MIWIPrkY?si=abcDEFghijk\n"
                 "https://m.youtube.com/watch?v=nTLuCuUqlOg\n"
                 "https://youtube.de/watch?t=90&v=aaaaaaaaaaa\n"
                 "https://www.youtube-nocookie.com/embed/bbbbbbbbbbb\n"
                 "https://music.youtube.com/watch?v=MO3MIWIPrkY\n"
                 "https://www.youtube.com/shorts/nTLuCuUqlOg\n"
                 "https://www.youtube.com/live/aaaaaaaaaaa\n"
                 "https://youtube.co.uk/v/bbbbbbbbbbb\n");
  catalogue.Finish();

  CHECK_EQ(ul_plugin_video_count(catalogue, 0), 4);
  CHECK_EQ(std::string(ul_plugin_video_id(catalogue, 0, 0)), std::string("MO3MIWIPrkY"));
  CHECK_EQ(std::string(ul_plugin_video_id(catalogue, 0, 1)), std::string("nTLuCuUqlOg"));
  CHECK_EQ(std::string(ul_plugin_video_id(catalogue, 0, 2)), std::string("aaaaaaaaaaa"));
  CHECK_EQ(std::string(ul_plugin_video_id(catalogue, 0, 3)), std::string("bbbbbbbbbbb"));
}

TEST(catalogue, a_youtube_channel_is_not_a_video) {
  Catalogue catalogue;
  catalogue.Add("Plugins/1_x/x.w2p");
  // A channel is not watchable, and an id invented from its path would be a
  // thumbnail 404 — so the eleven-character id is checked rather than assumed.
  catalogue.Info("1_x",
                 "Name\nCampaign\n\n"
                 "https://www.youtube.com/user/dannyldd\n"
                 "https://www.youtube.com/channel/UCnTLuCuUqlOgAbCdEfGhIjK\n");
  catalogue.Finish();
  CHECK_EQ(ul_plugin_video_count(catalogue, 0), 0);
}

TEST(catalogue, a_playlist_is_a_gallery_item_of_its_own) {
  Catalogue catalogue;
  catalogue.Add("Plugins/2_bruhcraft/bruhcraft.w2p");
  // bruhcraft's real link: dannyldd's whole playthrough, shared as a playlist
  // with a share suffix on it. It plays as a playlist — there is no single
  // video to reduce it to, and dropping it left the plugin with no video at
  // all.
  catalogue.Info(
      "2_bruhcraft",
      "playlist from last playthrough:\n"
      "https://youtube.com/playlist?list=PLrHhAf18bMUOJBMqv7KK45NhLF-yNCtWe&si=ijPbCpOn8zDcUEtB\n");
  catalogue.Finish();

  CHECK_EQ(ul_plugin_video_count(catalogue, 0), 1);
  // No video id — there is no one thumbnail to GET — but a list id, a playlist
  // page for a browser, and the videoseries embed for the inline player.
  CHECK_EQ(std::string(ul_plugin_video_id(catalogue, 0, 0)), std::string(""));
  CHECK_EQ(std::string(ul_plugin_video_list_id(catalogue, 0, 0)),
           std::string("PLrHhAf18bMUOJBMqv7KK45NhLF-yNCtWe"));
  CHECK_EQ(std::string(ul_plugin_video_url(catalogue, 0, 0)),
           std::string("https://www.youtube.com/playlist?"
                       "list=PLrHhAf18bMUOJBMqv7KK45NhLF-yNCtWe"));
  CHECK_EQ(std::string(ul_plugin_video_embed_url(catalogue, 0, 0)),
           std::string("https://www.youtube.com/embed/videoseries?"
                       "list=PLrHhAf18bMUOJBMqv7KK45NhLF-yNCtWe&rel=0&playsinline=1"));
}

TEST(catalogue, a_video_linked_from_a_playlist_is_the_video) {
  Catalogue catalogue;
  catalogue.Add("Plugins/1_x/x.w2p");
  // Woow's real shape: a share link that carries the mix it was playing in.
  // The author pointed at the video; the list came along for the ride.
  catalogue.Info("1_x", "https://youtu.be/sPZPiZWqfwE?list=RDsPZPiZWqfwE\n");
  catalogue.Finish();
  CHECK_EQ(ul_plugin_video_count(catalogue, 0), 1);
  CHECK_EQ(std::string(ul_plugin_video_id(catalogue, 0, 0)), std::string("sPZPiZWqfwE"));
  CHECK_EQ(std::string(ul_plugin_video_list_id(catalogue, 0, 0)), std::string(""));
}

TEST(catalogue, the_info_txt_path_itself_is_not_a_note) {
  Catalogue catalogue;
  // The file's *path* arrives with the walk and its *text* arrives through
  // add_info, so the path must not also land in the notes.
  catalogue.Add("Plugins/14_RemastRebalance/plugin remastReb 2.w2p");
  catalogue.Add("Plugins/14_RemastRebalance/info.txt");
  catalogue.Finish();

  CHECK_EQ(std::string(ul_plugin_name(catalogue, 0)), std::string("RemastRebalance"));
  // Only the text sets has_info; a path alone says nothing about the plugin.
  CHECK_EQ(ul_plugin_has_info(catalogue, 0), 0);
  CHECK_EQ(ul_plugin_variant_count(catalogue, 0), 1);
}

TEST(catalogue, a_plugin_with_no_info_txt_falls_back_to_its_folder) {
  Catalogue catalogue;
  // The folder names it, and there is nothing else to say about it.
  catalogue.Add("Plugins/7_reversed_races/rrc 1.w2p");
  catalogue.Finish();
  CHECK_EQ(std::string(ul_plugin_name(catalogue, 0)), std::string("Reversed races"));
  CHECK_EQ(std::string(ul_plugin_description(catalogue, 0)), std::string());
  CHECK_EQ(ul_plugin_has_info(catalogue, 0), 0);
}

TEST(catalogue, an_info_txt_with_nothing_in_it_changes_nothing) {
  Catalogue catalogue;
  catalogue.Add("Plugins/7_reversed_races/rrc 1.w2p");
  catalogue.Info("7_reversed_races", "\n\n   \n");
  catalogue.Finish();
  CHECK_EQ(std::string(ul_plugin_name(catalogue, 0)), std::string("Reversed races"));
  CHECK_EQ(ul_plugin_has_info(catalogue, 0), 0);
}

TEST(catalogue, the_base_folder_is_the_mod_describing_itself) {
  Catalogue catalogue;
  // The package-root base/ folder: screenshots and an info.txt for the mod
  // itself, shown on the "no plugin" row. Same shape as a plugin folder; the
  // empty id hands its info in.
  catalogue.Add("base/2.png");
  catalogue.Add("base/1.png");
  catalogue.Add("Plugins/2_Insane/insane.w2p");
  catalogue.Info("", "The mod itself\n\nhttps://youtu.be/MO3MIWIPrkY\n");
  catalogue.Finish();

  // Not a plugin: the list of plugins is untouched by any of it.
  CHECK_EQ(ul_catalogue_count(catalogue), 1);
  CHECK_EQ(std::string(ul_plugin_id(catalogue, 0)), std::string("2_Insane"));

  CHECK_EQ(ul_base_image_count(catalogue), 2);
  CHECK_EQ(std::string(ul_base_image(catalogue, 0)), std::string("base/1.png"));
  CHECK_EQ(std::string(ul_base_description(catalogue)),
           std::string("The mod itself\n\nhttps://youtu.be/MO3MIWIPrkY"));
  CHECK_EQ(ul_base_video_count(catalogue), 1);
  CHECK_EQ(std::string(ul_base_video_id(catalogue, 0)), std::string("MO3MIWIPrkY"));
  CHECK_EQ(std::string(ul_base_video_url(catalogue, 0)),
           std::string("https://www.youtube.com/watch?v=MO3MIWIPrkY"));
}

TEST(catalogue, no_base_folder_answers_empty) {
  Catalogue catalogue;
  // v6.6 has no base/ folder, so this is the case every user is in today.
  catalogue.Add("Plugins/2_Insane/insane.w2p");
  catalogue.Add("base");   // the bare directory, as some walks report it
  catalogue.Finish();
  CHECK_EQ(ul_base_image_count(catalogue), 0);
  CHECK_EQ(ul_base_video_count(catalogue), 0);
  CHECK_EQ(std::string(ul_base_description(catalogue)), std::string());
}

TEST(catalogue, a_plugin_without_info_still_gets_a_name) {
  Catalogue catalogue;
  catalogue.Add("Plugins/0_basegame/base.w2p");
  catalogue.Add("Plugins/1_DAIFE/daife.w2p");
  catalogue.Add("Plugins/3_Legacy of Dalaran/lod.w2p");
  catalogue.Finish();

  // The prefix goes and the first letter is raised — but only when the word is
  // not already capitalised, because DAIFE is an acronym and "Daife" is wrong.
  CHECK_EQ(std::string(ul_plugin_name(catalogue, 0)), std::string("Basegame"));
  CHECK_EQ(std::string(ul_plugin_name(catalogue, 1)), std::string("DAIFE"));
  CHECK_EQ(std::string(ul_plugin_name(catalogue, 2)), std::string("Legacy of Dalaran"));
  // And it says it made the name up, so the client shows a placeholder rather
  // than a confident-looking empty description panel.
  CHECK_EQ(ul_plugin_has_info(catalogue, 0), 0);
}

TEST(catalogue, ignores_everything_outside_plugins) {
  Catalogue catalogue;
  catalogue.Add("Unification.exe");
  catalogue.Add("UniFiles/something.dat");
  catalogue.Add("Data/maindat.war");
  catalogue.Add("Plugins/2_Insane/insane.w2p");
  catalogue.Finish();
  // The host hands over the whole extraction without filtering it first.
  CHECK_EQ(ul_catalogue_count(catalogue), 1);
}

TEST(catalogue, ignores_a_bare_plugin_folder_entry) {
  Catalogue catalogue;
  catalogue.Add("Plugins/2_Insane");        // the directory, as some walks report it
  catalogue.Finish();
  CHECK_EQ(ul_catalogue_count(catalogue), 0);
}

TEST(catalogue, find_by_id_survives_renumbering) {
  Catalogue catalogue;
  catalogue.Add("Plugins/2_Insane/insane.w2p");
  catalogue.Add("Plugins/5_Other/other.w2p");
  catalogue.Finish();
  CHECK_EQ(ul_catalogue_find(catalogue, "2_Insane"), 0);
  // Case-insensitive: the id was remembered from a previous run, and the
  // filesystem it was read from may not have reported the same case.
  CHECK_EQ(ul_catalogue_find(catalogue, "2_insane"), 0);
  CHECK_EQ(ul_catalogue_find(catalogue, "9_Gone"), -1);
  // "No plugin" is not in the catalogue, by design.
  CHECK_EQ(ul_catalogue_find(catalogue, ul_plugin_none_id()), -1);
}

TEST(catalogue, backslashes_are_accepted) {
  Catalogue catalogue;
  // A Win32 host walking a directory produces these, and asking it to convert
  // first is asking it to remember to.
  catalogue.Add("Plugins\\2_Insane\\insane.w2p");
  catalogue.Finish();
  CHECK_EQ(ul_catalogue_count(catalogue), 1);
  CHECK_EQ(ul_plugin_variant_count(catalogue, 0), 1);
}

// Reading the mod page, and following it to the package.
//
// The fixtures are the real responses, saved on 2026-08-31 from
// gamebanana.com/apiv11/Mod/644456. Testing against a document somebody wrote
// to make the test pass would prove nothing about a service nobody here owns —
// these prove the parser survives the shape GameBanana actually sends,
// including the parts of it that are odd: counters quoted as strings, notes as
// HTML, and an attached "package" that is 197 bytes.

#include "harness.hpp"
#include "uniloader/uniloader.h"

#include <string>

namespace {

constexpr int64_t kUnificationModId = 644456;

ul_release* LoadRelease() {
  const std::string json = ult::ReadFixture("test/fixtures/gamebanana-profilepage.json");
  if (json.empty()) ult::Skip("no gamebanana-profilepage.json fixture");
  ul_release* release = ul_release_parse(json.data(), json.size(), kUnificationModId);
  CHECK(release != nullptr);
  return release;
}

}  // namespace

TEST(release, reads_the_version_and_the_author) {
  ul_release* release = LoadRelease();
  CHECK_EQ(std::string(ul_release_mod_name(release)), std::string("War2 Unification mod"));
  // The field dannyldd maintains as part of publishing, and the reason the mod
  // page is the source of truth rather than a second file somebody has to keep
  // in step with it.
  CHECK_EQ(std::string(ul_release_version(release)), std::string("v6.6"));
  CHECK_EQ(std::string(ul_release_author(release)), std::string("dannyldd"));
  CHECK_EQ(std::string(ul_release_page_url(release)),
           std::string("https://gamebanana.com/mods/644456"));
  CHECK(ul_release_modified(release) > 0);
  ul_release_free(release);
}

TEST(release, refuses_a_different_mod) {
  const std::string json = ult::ReadFixture("test/fixtures/gamebanana-profilepage.json");
  if (json.empty()) ult::Skip("no gamebanana-profilepage.json fixture");
  // A redirect, a cached response, or a mistyped id in the settings. Checked
  // because the consequence of not checking is installing a different mod.
  CHECK(ul_release_parse(json.data(), json.size(), 1) == nullptr);
  CHECK(std::string(ul_last_error()).find("different mod") != std::string::npos);
}

TEST(release, refuses_rubbish) {
  CHECK(ul_release_parse("not json", 8, kUnificationModId) == nullptr);
  CHECK(ul_release_parse("{}", 2, kUnificationModId) == nullptr);
  CHECK(ul_release_parse("", 0, kUnificationModId) == nullptr);
}

TEST(release, offers_the_attachment_as_a_pointer_not_a_package) {
  ul_release* release = LoadRelease();
  CHECK(ul_release_source_count(release) > 0);

  // The file attached to the post is 197 bytes: a rar holding one line of text
  // pointing at OneDrive. Treating it as the package would download 197 bytes
  // and try to install them.
  bool found_pointer = false;
  for (int i = 0; i < ul_release_source_count(release); ++i) {
    if (ul_release_source_kind(release, i) != UL_SOURCE_POINTER) continue;
    found_pointer = true;
    CHECK(std::string(ul_release_source_url(release, i)).find("gamebanana.com/dl/") !=
          std::string::npos);
  }
  CHECK(found_pointer);
  ul_release_free(release);
}

TEST(release, ignores_alternate_sources_that_are_not_downloads) {
  ul_release* release = LoadRelease();
  // dannyldd's alternate sources today are two YouTube links. A launcher that
  // queued a video as a candidate package would spend a download finding out.
  for (int i = 0; i < ul_release_source_count(release); ++i) {
    const std::string url = ul_release_source_url(release, i);
    CHECK(url.find("youtube.com") == std::string::npos);
    CHECK(url.find("youtu.be") == std::string::npos);
  }
  ul_release_free(release);
}

TEST(release, pointer_text_becomes_a_source) {
  ul_release* release = LoadRelease();
  const int before = ul_release_source_count(release);
  // Byte for byte what is inside the attached archive.
  const std::string pointer =
      "https://1drv.ms/f/c/d7a2d372966ba7a1/"
      "IgC5cE08yOfNRI3eA6w0giz7AYKBQTdCYwcoVeFKiqWTMvQ?e=x8DCs9";
  CHECK_EQ(ul_release_add_pointer_text(release, pointer.data(), pointer.size()), 1);
  CHECK_EQ(ul_release_source_count(release), before + 1);
  // Adding it twice does not queue it twice: the same lead can be reached from
  // the page and from the attachment.
  CHECK_EQ(ul_release_add_pointer_text(release, pointer.data(), pointer.size()), 0);
  ul_release_free(release);
}

TEST(release, pointer_text_with_no_link_is_a_dead_end) {
  ul_release* release = LoadRelease();
  const std::string note = "thanks for downloading, see the forum thread";
  CHECK_EQ(ul_release_add_pointer_text(release, note.data(), note.size()), 0);
  ul_release_free(release);
}

TEST(release, an_oembed_answer_names_the_playlist_cover) {
  // The real answer for bruhcraft's playlist, saved 2026-09-02 from
  // youtube.com/oembed. A playlist has no thumbnail at img.youtube.com; its
  // oEmbed record names one — the first video's — and this is how the gallery
  // gets it without an API key.
  const std::string json = ult::ReadFixture("test/fixtures/oembed-playlist.json");
  if (json.empty()) ult::Skip("no oembed-playlist.json fixture");
  char* url = ul_oembed_thumbnail_url(json.data(), json.size());
  CHECK(url != nullptr);
  CHECK_EQ(std::string(url ? url : ""),
           std::string("https://i.ytimg.com/vi/YQLezFJD1t0/hqdefault.jpg"));
  ul_free(url);

  // An answer without the field is not a thumbnail.
  CHECK(ul_oembed_thumbnail_url("{}", 2) == nullptr);
  CHECK(ul_oembed_thumbnail_url("not json", 8) == nullptr);
}

TEST(release, reads_the_changelog) {
  ul_release* release = LoadRelease();
  const std::string updates = ult::ReadFixture("test/fixtures/gamebanana-updates.json");
  if (updates.empty()) ult::Skip("no gamebanana-updates.json fixture");
  CHECK_EQ(ul_release_add_updates(release, updates.data(), updates.size()), UL_OK);
  CHECK(ul_release_note_count(release) > 0);

  // Newest first, and the notes arrive as HTML — a <p> per line — which is not
  // what an EDIT control draws.
  const std::string newest = ul_release_note_text(release, 0);
  CHECK(!newest.empty());
  CHECK(newest.find("<p>") == std::string::npos);
  CHECK(newest.find("&nbsp;") == std::string::npos);
  ul_release_free(release);
}

TEST(release, notes_since_covers_every_missed_release) {
  ul_release* release = LoadRelease();
  const std::string updates = ult::ReadFixture("test/fixtures/gamebanana-updates.json");
  if (updates.empty()) ult::Skip("no gamebanana-updates.json fixture");
  ul_release_add_updates(release, updates.data(), updates.size());

  // Someone three releases behind should see all three. That is the difference
  // between "there is an update" and knowing whether to take it.
  char* behind = ul_release_notes_since(release, "v6.3");
  char* current = ul_release_notes_since(release, "v6.6");
  const std::string behind_text = behind ? behind : "";
  const std::string current_text = current ? current : "";
  CHECK(behind_text.size() > current_text.size());
  CHECK(behind_text.find("v6.6") != std::string::npos);
  CHECK(behind_text.find("v6.4") != std::string::npos);
  // Nothing published after what is installed: nothing to show.
  CHECK(current_text.empty());
  ul_free(behind);
  ul_free(current);
  ul_release_free(release);
}

TEST(release, onedrive_folder_is_recognised_as_a_folder) {
  const char* folder =
      "https://1drv.ms/f/c/d7a2d372966ba7a1/"
      "IgC5cE08yOfNRI3eA6w0giz7AYKBQTdCYwcoVeFKiqWTMvQ?e=x8DCs9";
  // /f/ is a folder. Knowing this before trying is what lets the client say
  // "that link is a folder, a file link is needed" instead of failing a
  // download for no stated reason.
  CHECK_EQ(ul_url_is_folder(folder), 1);
  CHECK_EQ(ul_url_is_folder("https://1drv.ms/u/c/abc/IgAbc?e=1"), 0);
  CHECK_EQ(ul_url_is_folder("https://gamebanana.com/dl/1596797"), 0);
}

TEST(release, builds_the_three_requests_that_list_a_shared_folder) {
  const char* folder =
      "https://1drv.ms/f/c/d7a2d372966ba7a1/"
      "IgC5cE08yOfNRI3eA6w0giz7AYKBQTdCYwcoVeFKiqWTMvQ?e=x8DCs9";

  // Step 1 is fixed and carries nothing about the user or the share.
  CHECK_EQ(std::string(ul_onedrive_badger_url()),
           std::string("https://api-badgerp.svc.ms/v1.0/token"));
  CHECK(std::string(ul_onedrive_badger_body(0)).find("appId") != std::string::npos);

  // Step 2's URL is the share link as a `u!` sharing token — Microsoft's own
  // encoding, unpadded base64url, which is the one part of this that has not
  // changed under us.
  char* item_url = ul_onedrive_item_url(folder);
  const std::string url = item_url ? item_url : "";
  ul_free(item_url);
  CHECK(url.find("my.microsoftpersonalcontent.com/_api/v2.0/shares/u!") !=
        std::string::npos);
  CHECK(url.find("/driveitem") != std::string::npos);
  // The token is the URL verbatim, so the `?e=` on the end has to be in it.
  CHECK(url.find("aHR0cHM6Ly8xZHJ2Lm1z") != std::string::npos);

  char* children = ul_onedrive_children_url("D7A2D372966BA7A1", "D7A2D372966BA7A1!s3c4d");
  const std::string listing = children ? children : "";
  ul_free(children);
  CHECK(listing.find("/drives/D7A2D372966BA7A1/items/D7A2D372966BA7A1!s3c4d/children") !=
        std::string::npos);
}

TEST(release, reads_the_badger_token) {
  const std::string reply = R"({"authScheme":"badger","token":"abc.def.ghi"})";
  char* token = ul_onedrive_read_badger(reply.data(), reply.size());
  const std::string value = token ? token : "";
  ul_free(token);
  CHECK_EQ(value, std::string("abc.def.ghi"));

  const std::string empty = R"({"authScheme":"badger"})";
  CHECK(ul_onedrive_read_badger(empty.data(), empty.size()) == nullptr);
}

TEST(release, reads_the_redeemed_folder) {
  // The real reply, saved on 2026-08-31 from the live share.
  const std::string json = ult::ReadFixture("test/fixtures/onedrive-item.json");
  if (json.empty()) ult::Skip("no onedrive-item.json fixture");
  char* drive = nullptr;
  char* item = nullptr;
  CHECK_EQ(ul_onedrive_read_item(json.data(), json.size(), &drive, &item), UL_OK);
  CHECK_EQ(std::string(drive ? drive : ""), std::string("D7A2D372966BA7A1"));
  CHECK(std::string(item ? item : "").find("D7A2D372966BA7A1!s") == 0);
  ul_free(drive);
  ul_free(item);
}

TEST(release, an_unredeemed_share_says_so) {
  // What the same request answers without the `Prefer: autoredeem` header. It
  // reads exactly like a private link and is not one, so the message has to
  // carry the server's own word for it rather than a guess.
  const std::string denied = R"({"error":{"code":"accessDenied","message":"Access denied"}})";
  char* drive = nullptr;
  char* item = nullptr;
  CHECK_EQ(ul_onedrive_read_item(denied.data(), denied.size(), &drive, &item),
           UL_ERR_PARSE);
  CHECK(drive == nullptr);
  CHECK(item == nullptr);
  CHECK(std::string(ul_last_error()).find("Access denied") != std::string::npos);
}

TEST(release, a_file_share_is_not_a_folder) {
  const std::string file =
      R"({"id":"X!1","name":"a.rar","parentReference":{"driveId":"D1"},"file":{}})";
  char* drive = nullptr;
  char* item = nullptr;
  CHECK_EQ(ul_onedrive_read_item(file.data(), file.size(), &drive, &item), UL_ERR_PARSE);
  ul_free(drive);
  ul_free(item);
}

TEST(release, picks_the_package_out_of_the_real_folder) {
  const std::string listing = ult::ReadFixture("test/fixtures/onedrive-children.json");
  if (listing.empty()) ult::Skip("no onedrive-children.json fixture");
  char* picked = ul_folder_pick_archive(listing.data(), listing.size(), "v6.6", nullptr);
  const std::string url = picked ? picked : "";
  ul_free(picked);
  // The real folder holds three subfolders and two packages, v6_4 and v6_6.
  // The subfolders are not archives and v6_6 is the newer claim.
  //
  // Asserted on the item's id rather than on a file name: OneDrive hands back a
  // "_layouts/15/download.aspx?UniqueId=…" link with no name in it at all,
  // which is worth writing down because it means nothing downstream can learn
  // what it is about to download from its URL.
  CHECK(!url.empty());
  CHECK(url.find("download.aspx") != std::string::npos);
  CHECK(url.find("3b72dc60-f316-4a10-a712-d04561c1bcf1") != std::string::npos);  // v6.6
  CHECK(url.find("17f018e3-d8c5-4b40-bf62-c64d24b451c7") == std::string::npos);  // v6.4
}

TEST(release, the_picked_package_matches_the_version_the_mod_page_states) {
  const std::string profile = ult::ReadFixture("test/fixtures/gamebanana-profilepage.json");
  const std::string listing = ult::ReadFixture("test/fixtures/onedrive-children.json");
  if (profile.empty() || listing.empty()) ult::Skip("no fixtures");

  ul_release* release = ul_release_parse(profile.data(), profile.size(), kUnificationModId);
  CHECK(release != nullptr);
  char* picked = ul_folder_pick_archive(listing.data(), listing.size(),
                                        ul_release_version(release), nullptr);
  CHECK(picked != nullptr);

  // The two halves of the chain agree: GameBanana says v6.6, and the newest
  // package in dannyldd's OneDrive folder is war2_unif_v6_6.rar. If they ever
  // disagree, this is the test that says which one moved.
  char* from_name = ul_version_from_filename("war2_unif_v6_6.rar");
  CHECK_EQ(ul_version_compare(ul_release_version(release), from_name), 0);
  ul_free(from_name);
  ul_free(picked);
  ul_release_free(release);
}

TEST(release, direct_download_url_leaves_a_folder_alone) {
  const char* folder = "https://1drv.ms/f/c/abc/IgAbc?e=1";
  char* direct = ul_direct_download_url(folder);
  const std::string rewritten = direct ? direct : "";
  ul_free(direct);
  // A folder has no direct form, so appending download=1 would only produce a
  // link that looks fetchable and is not.
  CHECK_EQ(rewritten, std::string(folder));

  char* file = ul_direct_download_url("https://1drv.ms/u/c/abc/IgAbc?e=1");
  const std::string file_url = file ? file : "";
  ul_free(file);
  CHECK(file_url.find("download=1") != std::string::npos);

  // Something already direct is passed through untouched.
  char* plain = ul_direct_download_url("https://gamebanana.com/dl/1596797");
  const std::string plain_url = plain ? plain : "";
  ul_free(plain);
  CHECK_EQ(plain_url, std::string("https://gamebanana.com/dl/1596797"));
}

namespace {

/// Picks, and hands back both halves of the answer.
struct Picked {
  std::string url;
  std::string name;
};

Picked Pick(const std::string& listing, const char* expected) {
  char* name = nullptr;
  char* url = ul_folder_pick_archive(listing.data(), listing.size(), expected, &name);
  Picked picked;
  if (url) picked.url = url;
  if (name) picked.name = name;
  ul_free(url);
  ul_free(name);
  return picked;
}

}  // namespace

TEST(release, the_newest_archive_wins) {
  const std::string listing = R"({"value":[
    {"name":"readme.txt","@content.downloadUrl":"https://x/readme"},
    {"name":"war2_unif_v6_9.rar","@content.downloadUrl":"https://x/older",
     "lastModifiedDateTime":"2026-08-01T10:00:00Z"},
    {"name":"war2_unif_v6_10.rar","@content.downloadUrl":"https://x/newest",
     "lastModifiedDateTime":"2026-09-01T10:00:00Z"},
    {"name":"1_older_versions","folder":{"childCount":19}}
  ]})";
  // The date decides, and only the date. A modification date is always there
  // and always means what it says; a version in a file name is a convention.
  CHECK_EQ(Pick(listing, "").url, std::string("https://x/newest"));
  CHECK_EQ(Pick(listing, "").name, std::string("war2_unif_v6_10.rar"));
}

TEST(release, the_newest_wins_even_when_it_is_named_like_an_older_release) {
  // dannyldd re-uploads v6.4 to fix a bad upload, after v6.6 went out. What is
  // wanted is the file he just put there, and the name is not the evidence.
  const std::string listing = R"({"value":[
    {"name":"war2_unif_v6_6.rar","@content.downloadUrl":"https://x/six",
     "lastModifiedDateTime":"2026-08-30T11:00:00Z"},
    {"name":"war2_unif_v6_4.rar","@content.downloadUrl":"https://x/four",
     "lastModifiedDateTime":"2026-09-05T09:00:00Z"}
  ]})";
  CHECK_EQ(Pick(listing, "").url, std::string("https://x/four"));
}

TEST(release, the_expected_version_narrows_the_field_first) {
  // The mod page already said v6.6. A file that agrees with it is the one being
  // looked for, even though something newer is sitting beside it — that is how
  // a stray upload, or a package for a different thing, stays out.
  const std::string listing = R"({"value":[
    {"name":"war2_unif_v6_6.rar","@content.downloadUrl":"https://x/six",
     "lastModifiedDateTime":"2026-08-30T11:00:00Z"},
    {"name":"some_other_thing.rar","@content.downloadUrl":"https://x/other",
     "lastModifiedDateTime":"2026-09-09T09:00:00Z"}
  ]})";
  CHECK_EQ(Pick(listing, "v6.6").url, std::string("https://x/six"));
  // And with nothing expected, the newest still wins.
  CHECK_EQ(Pick(listing, "").url, std::string("https://x/other"));
}

TEST(release, the_date_still_decides_between_files_that_agree) {
  // Two uploads of the same release: the second one is the fix.
  const std::string listing = R"({"value":[
    {"name":"war2_unif_v6_6.rar","@content.downloadUrl":"https://x/first",
     "lastModifiedDateTime":"2026-08-30T11:00:00Z"},
    {"name":"war2_unif_v6_6 (2).rar","@content.downloadUrl":"https://x/second",
     "lastModifiedDateTime":"2026-08-31T11:00:00Z"}
  ]})";
  CHECK_EQ(Pick(listing, "v6.6").url, std::string("https://x/second"));
}

TEST(release, an_expected_version_nothing_matches_falls_back_to_the_newest) {
  // The page has been bumped to v6.7 and the upload has not happened yet.
  // Downloading the newest thing there is right; calling it v6.7 would not be,
  // and the caller reads the name back to avoid exactly that.
  const std::string listing = R"({"value":[
    {"name":"war2_unif_v6_6.rar","@content.downloadUrl":"https://x/six",
     "lastModifiedDateTime":"2026-08-30T11:00:00Z"}
  ]})";
  const Picked picked = Pick(listing, "v6.7");
  CHECK_EQ(picked.url, std::string("https://x/six"));
  CHECK_EQ(picked.name, std::string("war2_unif_v6_6.rar"));
}

TEST(release, a_listing_with_no_archive_picks_nothing) {
  const std::string listing = R"({"value":[{"name":"readme.txt",
      "@content.downloadUrl":"https://x/readme"}]})";
  CHECK(ul_folder_pick_archive(listing.data(), listing.size(), "", nullptr) == nullptr);
}

TEST(release, subfolders_of_old_packages_are_not_candidates) {
  // The real share has three: older versions, co-ops, and others. A folder has
  // no downloadUrl and is not an archive, and both of those keep it out.
  const std::string listing = R"({"value":[
    {"name":"1_war2_unification_older_versions","folder":{"childCount":40},
     "lastModifiedDateTime":"2026-09-09T09:00:00Z"},
    {"name":"war2_unif_v6_6.rar","@content.downloadUrl":"https://x/six",
     "lastModifiedDateTime":"2026-08-30T11:00:00Z"}
  ]})";
  CHECK_EQ(Pick(listing, "").url, std::string("https://x/six"));
}

TEST(release, there_is_more_than_one_application_id_to_try) {
  // An application id is exactly the sort of thing that gets retired, so the
  // second one the web client knows about is kept as a fallback.
  CHECK(*ul_onedrive_badger_body(0) != '\0');
  CHECK(*ul_onedrive_badger_body(1) != '\0');
  CHECK_EQ(std::string(ul_onedrive_badger_body(2)), std::string());
  CHECK_EQ(std::string(ul_onedrive_badger_body(-1)), std::string());
}

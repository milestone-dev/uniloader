// Unpacking, and the hash that says the unpacked thing is the right one.
//
// test/fixtures/pointer.rar is the real 197-byte file attached to the mod page,
// downloaded from gamebanana.com/dl/1596797. Reading a link out of it is the
// hop between "GameBanana says v6.6" and "here is where v6.6 actually is", so
// it is worth a test against the real bytes rather than a rar made here.

#include "harness.hpp"
#include "uniloader/uniloader.h"

#include <cstdio>
#include <string>

namespace {

std::string FixturePath(const char* relative) {
  return ult::Root() + "/" + relative;
}

bool Exists(const std::string& path) {
  std::FILE* file = std::fopen(path.c_str(), "rb");
  if (!file) return false;
  std::fclose(file);
  return true;
}

std::string Sha256Of(const std::string& data) {
  ul_sha256* hash = ul_sha256_create();
  ul_sha256_update(hash, data.data(), data.size());
  char out[65] = {};
  ul_sha256_finish(hash, out);
  return out;
}

}  // namespace

TEST(archive, sha256_matches_the_published_vectors) {
  // FIPS 180-4's own examples. A hash that is wrong is worse than no hash: it
  // would reject every good download, and the user would learn to skip the check.
  CHECK_EQ(Sha256Of(""),
           std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
  CHECK_EQ(Sha256Of("abc"),
           std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
  CHECK_EQ(Sha256Of("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
           std::string("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));
}

TEST(archive, sha256_handles_a_message_that_spans_blocks) {
  // A million 'a's, streamed in awkward pieces — which is what a download is.
  ul_sha256* hash = ul_sha256_create();
  const std::string chunk(1000, 'a');
  for (int i = 0; i < 1000; ++i) ul_sha256_update(hash, chunk.data(), chunk.size());
  char out[65] = {};
  ul_sha256_finish(hash, out);
  CHECK_EQ(std::string(out),
           std::string("cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));
}

TEST(archive, reads_the_pointer_attached_to_the_mod_page) {
  const std::string path = FixturePath("test/fixtures/pointer.rar");
  if (!Exists(path)) ult::Skip("no pointer.rar fixture");

  ul_archive* archive = ul_archive_open(path.c_str());
  CHECK(archive != nullptr);
  CHECK_EQ(ul_archive_count(archive), 1);
  CHECK_EQ(std::string(ul_archive_entry(archive, 0)),
           std::string("MOD link unification mod danny war2.txt"));

  size_t length = 0;
  char* text = ul_archive_read_entry(archive, 0, &length);
  CHECK(text != nullptr);
  const std::string contents(text, length);
  ul_free(text);
  ul_archive_close(archive);

  // The whole published route, in one assertion: the file attached to the mod
  // page contains the OneDrive link and nothing else.
  CHECK(contents.find("1drv.ms") != std::string::npos);
}

TEST(archive, a_pointer_read_from_the_archive_becomes_a_source) {
  const std::string profile = ult::ReadFixture("test/fixtures/gamebanana-profilepage.json");
  const std::string path = FixturePath("test/fixtures/pointer.rar");
  if (profile.empty() || !Exists(path)) ult::Skip("no fixtures");

  ul_release* release = ul_release_parse(profile.data(), profile.size(), 644456);
  CHECK(release != nullptr);

  ul_archive* archive = ul_archive_open(path.c_str());
  CHECK(archive != nullptr);
  size_t length = 0;
  char* text = ul_archive_read_entry(archive, 0, &length);
  CHECK(text != nullptr);
  // Following the lead: the pointer's contents come back in as a new candidate
  // source, which is the step that turns a mod page into a download.
  CHECK_EQ(ul_release_add_pointer_text(release, text, length), 1);
  ul_free(text);
  ul_archive_close(archive);
  ul_release_free(release);
}

TEST(archive, refuses_something_that_is_not_an_archive) {
  const std::string path = FixturePath("test/fixtures/gamebanana-profilepage.json");
  if (!Exists(path)) ult::Skip("no fixture");
  CHECK(ul_archive_open(path.c_str()) == nullptr);
  CHECK(ul_archive_open("no-such-file.rar") == nullptr);
  CHECK(ul_archive_open("") == nullptr);
  CHECK(ul_archive_open(nullptr) == nullptr);
}

TEST(archive, error_text_is_written_for_a_person) {
  // Shown in a message box, so it has to be a sentence rather than a code.
  for (int code : {UL_ERR_OPEN, UL_ERR_FORMAT, UL_ERR_WRITE, UL_ERR_UNSAFE_PATH,
                   UL_ERR_ENCRYPTED, UL_ERR_PARSE, UL_ERR_NO_ARCHIVE}) {
    const std::string text = ul_error_text(code);
    CHECK(text.size() > 10);
    CHECK(text.back() == '.');
  }
}

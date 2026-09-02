// Editing War2Combat's own display settings.
//
// The file belongs to the game. The test that matters most here is
// leaves_everything_it_was_not_asked_to_change — a config full of comments and
// other games' sections has to come back byte for byte except for the one value
// that was set, or UniLoader is rewriting somebody else's work.

#include "harness.hpp"
#include "uniloader/uniloader.h"

#include <string>

namespace {

/// The shape of the real ddraw.ini: a commented header, the section the game
/// reads, and a run of per-game sections underneath that are none of our
/// business.
const char* const kReal =
    "; cnc-ddraw config\n"
    "; https://github.com/FunkyFr3sh/cnc-ddraw\n"
    "\n"
    "[ddraw]\n"
    "width=0\n"
    "height=0\n"
    "fullscreen=True\n"
    "windowed=True\n"
    "maintas=True\n"
    "boxing=false\n"
    "maxfps=60\n"
    "vsync=false\n"
    "shader=Shaders\\bilinear.glsl\n"
    "renderer=opengl   ; opengl, gdi or direct3d9\n"
    "\n"
    "[CARMA95]\n"
    "renderer=opengl\n"
    "maxgameticks=30\n"
    "\n"
    // The one that matters: cnc-ddraw applies the section named after the
    // running executable on top of [ddraw], and War2Combat's is right here.
    "[Warcraft II BNE]\n"
    "renderer=opengl\n"
    "windowed=True\n"
    "fullscreen=True\n";

int CountOf(const std::string& haystack, const std::string& needle) {
  int count = 0;
  for (size_t at = haystack.find(needle); at != std::string::npos;
       at = haystack.find(needle, at + needle.size())) {
    ++count;
  }
  return count;
}

std::string Written(const ul_ini* ini) {
  size_t length = 0;
  char* text = ul_ini_write(ini, &length);
  const std::string out(text ? text : "", length);
  ul_free(text);
  return out;
}

struct Ini {
  ul_ini* handle;
  explicit Ini(const char* text)
      : handle(ul_ini_parse(text, std::char_traits<char>::length(text))) {}
  ~Ini() { ul_ini_free(handle); }
  operator ul_ini*() const { return handle; }
};

}  // namespace

TEST(ini, reads_values) {
  Ini ini(kReal);
  CHECK_EQ(std::string(ul_ini_get(ini, "ddraw", "maxfps")), std::string("60"));
  // Section and key both match without regard to case, which is how the file is
  // written by hand and by the game's own configuration tool in turn.
  CHECK_EQ(std::string(ul_ini_get(ini, "DDRAW", "MaxFps")), std::string("60"));
  // Another game's section is a different key of the same name.
  CHECK_EQ(std::string(ul_ini_get(ini, "CARMA95", "maxgameticks")), std::string("30"));
  CHECK_EQ(std::string(ul_ini_get(ini, "ddraw", "maxgameticks")), std::string());
  CHECK_EQ(std::string(ul_ini_get(ini, "ddraw", "nosuchkey")), std::string());
}

TEST(ini, round_trips_untouched) {
  Ini ini(kReal);
  CHECK_EQ(Written(ini), std::string(kReal));
}

TEST(ini, leaves_everything_it_was_not_asked_to_change) {
  Ini ini(kReal);
  ul_ini_set(ini, "ddraw", "maxfps", "144");
  const std::string out = Written(ini);

  // The one value changed.
  CHECK(out.find("maxfps=144") != std::string::npos);
  // And nothing else did: the comments, the blank lines, the other game's
  // section, and the trailing comment on a line this did not touch.
  CHECK(out.find("; cnc-ddraw config") != std::string::npos);
  CHECK(out.find("[CARMA95]") != std::string::npos);
  CHECK(out.find("renderer=opengl   ; opengl, gdi or direct3d9") != std::string::npos);
  CHECK_EQ(static_cast<int>(out.size()),
           static_cast<int>(std::string(kReal).size() + 1));   // "60" -> "144"
}

TEST(ini, keeps_a_comment_that_follows_the_value) {
  Ini ini(kReal);
  ul_ini_set(ini, "ddraw", "renderer", "gdi");
  const std::string out = Written(ini);
  // The value is spliced in place, so what the author wrote after it survives.
  CHECK(out.find("renderer=gdi   ; opengl, gdi or direct3d9") != std::string::npos);
}

TEST(ini, a_new_key_goes_in_its_own_section) {
  Ini ini(kReal);
  ul_ini_set(ini, "ddraw", "adjmouse", "true");
  const std::string out = Written(ini);
  // Under [ddraw], not at the end of the file where [CARMA95] would claim it.
  const size_t added = out.find("adjmouse=true");
  const size_t other = out.find("[CARMA95]");
  CHECK(added != std::string::npos);
  CHECK(added < other);
}

TEST(ini, survives_a_file_with_crlf_endings) {
  const char* crlf = "[ddraw]\r\nfullscreen=True\r\nmaintas=True\r\n";
  Ini ini(crlf);
  ul_ini_set(ini, "ddraw", "maintas", "false");
  const std::string out = Written(ini);
  // Every line ending is where it was. Rewriting a CRLF file as LF is the kind
  // of change that shows up as "the whole file" in a diff.
  CHECK_EQ(out, std::string("[ddraw]\r\nfullscreen=True\r\nmaintas=false\r\n"));
}

TEST(display, reads_the_three_modes_out_of_two_booleans) {
  // The pairing nobody guesses: fullscreen *and* windowed is borderless.
  {
    Ini ini("[ddraw]\nfullscreen=true\nwindowed=true\n");
    CHECK_EQ(ul_display_mode(ini), UL_DISPLAY_BORDERLESS);
  }
  {
    Ini ini("[ddraw]\nfullscreen=true\nwindowed=false\n");
    CHECK_EQ(ul_display_mode(ini), UL_DISPLAY_FULLSCREEN);
  }
  {
    Ini ini("[ddraw]\nfullscreen=false\nwindowed=true\n");
    CHECK_EQ(ul_display_mode(ini), UL_DISPLAY_WINDOWED);
  }
  // The file is written "True" by the game's own tool and "yes" by hand.
  {
    Ini ini("[ddraw]\nfullscreen=Yes\nwindowed=No\n");
    CHECK_EQ(ul_display_mode(ini), UL_DISPLAY_FULLSCREEN);
  }
  // A file that says nothing is windowed, which is the safe answer: a wrong
  // guess at fullscreen on a machine that cannot do it is a black screen.
  {
    Ini ini("[ddraw]\n");
    CHECK_EQ(ul_display_mode(ini), UL_DISPLAY_WINDOWED);
  }
}

TEST(display, writing_a_mode_round_trips) {
  for (int mode : {UL_DISPLAY_FULLSCREEN, UL_DISPLAY_BORDERLESS, UL_DISPLAY_WINDOWED}) {
    Ini ini(kReal);
    ul_display_set_mode(ini, mode);
    CHECK_EQ(ul_display_mode(ini), mode);
  }
}

TEST(display, aspect_and_shader) {
  Ini ini(kReal);
  CHECK_EQ(ul_display_keep_aspect(ini), 1);
  ul_display_set_keep_aspect(ini, 0);
  CHECK_EQ(ul_display_keep_aspect(ini), 0);
  CHECK(Written(ini).find("maintas=false") != std::string::npos);

  CHECK_EQ(std::string(ul_display_shader(ini)), std::string("Shaders\\bilinear.glsl"));
  ul_display_set_shader(ini, "Shaders\\xbr-lv2.glsl");
  CHECK_EQ(std::string(ul_display_shader(ini)), std::string("Shaders\\xbr-lv2.glsl"));
  // Empty is a real choice: no filter, nearest-neighbour pixels.
  ul_display_set_shader(ini, "");
  CHECK_EQ(std::string(ul_display_shader(ini)), std::string());
}

TEST(display, the_per_game_section_is_what_the_game_actually_uses) {
  // The trap this exists for. cnc-ddraw reads [ddraw] for defaults and then a
  // section named after the running executable on top, and the real config has
  // one: [Warcraft II BNE] sets windowed and fullscreen all over again. Writing
  // only [ddraw] leaves the game running exactly as it was.
  Ini ini(kReal);
  ul_display_set_mode(ini, UL_DISPLAY_FULLSCREEN);
  const std::string out = Written(ini);

  // Both sections say the same thing, so it does not matter which one wins.
  CHECK_EQ(CountOf(out, "fullscreen=true"), 2);
  CHECK_EQ(CountOf(out, "windowed=false"), 2);
  CHECK_EQ(ul_display_mode(ini), UL_DISPLAY_FULLSCREEN);
}

TEST(display, an_override_that_disagrees_is_the_one_reported) {
  // Before UniLoader has written anything the two can disagree, and the answer
  // shown to a person has to be what they will see when they press play.
  Ini ini(
      "[ddraw]\nfullscreen=true\nwindowed=false\n"
      "\n[Warcraft II BNE]\nfullscreen=true\nwindowed=true\n");
  CHECK_EQ(ul_display_mode(ini), UL_DISPLAY_BORDERLESS);
}

TEST(display, a_setting_is_not_added_to_sections_that_never_had_it) {
  // [CARMA95] inherits maintas from [ddraw]. Writing the key into it would take
  // that inheritance away and pin another game to our answer forever.
  Ini ini(kReal);
  ul_display_set_keep_aspect(ini, 0);
  const std::string out = Written(ini);
  CHECK_EQ(CountOf(out, "maintas"), 1);
  // And a key in no section at all still lands somewhere the game reads.
  Ini bare("[ddraw]\n");
  ul_display_set_keep_aspect(bare, 1);
  CHECK_EQ(std::string(ul_ini_get(bare, "ddraw", "maintas")), std::string("true"));
}

TEST(ini, tolerates_rubbish) {
  CHECK(ul_ini_parse(nullptr, 0) != nullptr);   // an absent file is an empty one
  Ini empty("");
  CHECK_EQ(std::string(ul_ini_get(empty, "ddraw", "fullscreen")), std::string());
  // Setting into an empty document has to invent the section as well as the key.
  ul_ini_set(empty, "ddraw", "fullscreen", "true");
  CHECK_EQ(std::string(ul_ini_get(empty, "ddraw", "fullscreen")), std::string("true"));
}

TEST(display, the_real_war2combat_config) {
  // The file off this machine's War2Combat, captured whole. The fixtures above
  // are a model of it; this is the thing itself, and it is here because a config
  // the game does not read fails silently — it looks exactly like success.
  const std::string text = ult::ReadFixture("test/fixtures/ddraw.ini");
  if (text.empty()) ult::Skip("no fixtures");

  ul_ini* ini = ul_ini_parse(text.data(), text.size());
  // As found: borderless, 4:3 kept, bilinear.
  CHECK_EQ(ul_display_mode(ini), UL_DISPLAY_BORDERLESS);
  CHECK_EQ(ul_display_keep_aspect(ini), 1);
  CHECK_EQ(std::string(ul_display_shader(ini)), std::string("Shaders\\bilinear.glsl"));

  ul_display_set_mode(ini, UL_DISPLAY_FULLSCREEN);
  size_t length = 0;
  char* out = ul_ini_write(ini, &length);
  const std::string written(out ? out : "", length);
  ul_free(out);

  // [ddraw] and [Warcraft II BNE] both, or the game keeps its old window.
  CHECK_EQ(CountOf(written, "fullscreen=true"), 2);
  CHECK_EQ(CountOf(written, "windowed=false"), 2);
  // Twenty-three other games' sections, every comment and every blank line
  // still there. The file grows by exactly two characters: the two `windowed`
  // values went from "true" to "false" and nothing else moved at all.
  CHECK_EQ(static_cast<int>(written.size()), static_cast<int>(text.size()) + 2);
  CHECK_EQ(CountOf(written, "["), CountOf(text, "["));
  ul_ini_free(ini);
}

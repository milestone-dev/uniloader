// The game's own display settings, and the file they live in.
//
// `ddraw.ini` belongs to War2Combat, not to UniLoader and not to the mod. So it
// is edited the way you would edit somebody else's file: line by line, changing
// only the keys asked for and leaving every comment, every blank line, every
// other section and the order of all of it exactly as found. A tidy rewrite
// would be a rewrite of somebody else's work.
//
// The one that is live is cnc-ddraw's. War2Combat also ships `war2_ddraw.ini`
// for a different wrapper, and the two have different key names — writing the
// wrong one changes nothing at all, which is the worst kind of wrong. The host
// hands in whichever file it found the running ddraw.dll to belong to; this
// only knows the key names.

#include "uniloader/uniloader.h"

#include "util.hpp"

#include <new>
#include <string>
#include <vector>

namespace ul {

struct IniLine {
  std::string text;      // verbatim, as read
  std::string section;   // which section it is in, lowercased; "" before the first
  std::string key;       // lowercased, empty for comments, blanks and headings
  size_t value_at = 0;   // where the value starts in `text`
  size_t value_end = 0;
};

}  // namespace ul

struct ul_ini {
  std::vector<ul::IniLine> lines;
};

namespace ul {
namespace {

/// A heading's name, or "" when the line is not one.
std::string HeadingName(const std::string& line) {
  const std::string trimmed = Trim(line);
  if (trimmed.size() < 2 || trimmed.front() != '[' || trimmed.back() != ']') return {};
  return Lower(Trim(trimmed.substr(1, trimmed.size() - 2)));
}

/// Splits `key = value` in place, recording where the value sits so it can be
/// replaced without disturbing the spacing around it.
void SplitPair(IniLine& line) {
  const std::string& text = line.text;
  // A comment, not a setting. Both markers are in this file: cnc-ddraw writes
  // `;` and people leave `#`.
  const std::string trimmed = Trim(text);
  if (trimmed.empty() || trimmed[0] == ';' || trimmed[0] == '#') return;

  const size_t equals = text.find('=');
  if (equals == std::string::npos) return;
  line.key = Lower(Trim(text.substr(0, equals)));
  if (line.key.empty()) return;

  size_t start = equals + 1;
  while (start < text.size() && (text[start] == ' ' || text[start] == '\t')) ++start;

  // A comment after the value is not part of it. Recognised only when a space
  // or a tab comes first, because a bare ';' or '#' can sit inside a value and
  // a path certainly can — "Shaders\bilinear.glsl" must survive whole.
  size_t end = text.size();
  for (size_t i = start + 1; i < text.size(); ++i) {
    if (text[i] != ';' && text[i] != '#') continue;
    if (text[i - 1] != ' ' && text[i - 1] != '\t') continue;
    end = i;
    break;
  }
  // Trailing whitespace and the line ending are not part of the value, and
  // putting them back is what keeps a CRLF file a CRLF file.
  while (end > start && (text[end - 1] == '\r' || text[end - 1] == '\n' ||
                         text[end - 1] == ' ' || text[end - 1] == '\t')) {
    --end;
  }
  line.value_at = start;
  line.value_end = end;
}

}  // namespace
}  // namespace ul

// ------------------------------------------------------------------- the ABI

extern "C" {

ul_ini* ul_ini_parse(const char* text, size_t length) {
  auto ini = new (std::nothrow) ul_ini();
  if (!ini) return nullptr;
  if (!text) return ini;

  const std::string body(text, length);
  std::string section;
  size_t at = 0;
  while (at <= body.size()) {
    size_t end = body.find('\n', at);
    const bool last = end == std::string::npos;
    if (last) end = body.size();

    ul::IniLine line;
    // The newline is kept on the line it terminates, so writing the file back
    // out is a concatenation and nothing has to remember what the endings were.
    line.text = body.substr(at, (last ? end : end + 1) - at);
    const std::string heading = ul::HeadingName(line.text);
    if (!heading.empty()) {
      section = heading;
    } else {
      ul::SplitPair(line);
    }
    line.section = section;
    ini->lines.push_back(std::move(line));

    if (last) break;
    at = end + 1;
  }
  // A file ending in a newline produces a final empty line, which is real: it is
  // where the next appended key goes.
  return ini;
}

void ul_ini_free(ul_ini* ini) { delete ini; }

const char* ul_ini_get(const ul_ini* ini, const char* section, const char* key) {
  if (!ini || !key) return "";
  const std::string want_section = ul::Lower(section ? section : "");
  const std::string want_key = ul::Lower(key);
  for (const ul::IniLine& line : ini->lines) {
    if (line.key != want_key || line.section != want_section) continue;
    // Returned as a pointer into the line itself, which lives as long as the
    // document — the ABI's rule for every getter here.
    static std::string scratch;
    scratch = line.text.substr(line.value_at, line.value_end - line.value_at);
    return scratch.c_str();
  }
  return "";
}

void ul_ini_set(ul_ini* ini, const char* section, const char* key, const char* value) {
  if (!ini || !key || !value) return;
  const std::string want_section = ul::Lower(section ? section : "");
  const std::string want_key = ul::Lower(key);

  for (ul::IniLine& line : ini->lines) {
    if (line.key != want_key || line.section != want_section) continue;
    // Spliced into the existing line: the key keeps its own spelling and its own
    // spacing, and any comment after the value on the same line survives.
    line.text = line.text.substr(0, line.value_at) + value +
                line.text.substr(line.value_end);
    line.value_end = line.value_at + std::string(value).size();
    return;
  }

  // Not there. Appended at the end of its section rather than the end of the
  // file, or the key would land under whichever section happened to be last.
  size_t insert_at = ini->lines.size();
  bool found_section = want_section.empty();
  for (size_t i = 0; i < ini->lines.size(); ++i) {
    if (ini->lines[i].section == want_section) {
      found_section = true;
      insert_at = i + 1;
    }
  }
  ul::IniLine added;
  added.section = want_section;
  added.key = want_key;
  if (!found_section) {
    ul::IniLine heading;
    heading.section = want_section;
    heading.text = "[" + std::string(section ? section : "") + "]\n";
    ini->lines.push_back(std::move(heading));
    insert_at = ini->lines.size();
  }
  const std::string line = std::string(key) + "=" + value + "\n";
  added.text = line;
  added.value_at = std::string(key).size() + 1;
  added.value_end = added.value_at + std::string(value).size();
  ini->lines.insert(ini->lines.begin() + static_cast<long>(insert_at), std::move(added));
}

char* ul_ini_write(const ul_ini* ini, size_t* length) {
  std::string out;
  if (ini) {
    for (const ul::IniLine& line : ini->lines) out += line.text;
  }
  if (length) *length = out.size();
  return ul::Duplicate(out);
}

const char* ul_ini_effective(const ul_ini* ini, const char* key) {
  if (!ini || !key) return "";
  const std::string want = ul::Lower(key);
  const char* answer = "";
  static std::string scratch;
  for (const ul::IniLine& line : ini->lines) {
    if (line.key != want) continue;
    // Later wins: the defaults come first and the per-game section that
    // overrides them comes after, which is the order the wrapper resolves in.
    scratch = line.text.substr(line.value_at, line.value_end - line.value_at);
    answer = scratch.c_str();
  }
  return answer;
}

void ul_ini_set_everywhere(ul_ini* ini, const char* key, const char* value) {
  if (!ini || !key || !value) return;
  const std::string want = ul::Lower(key);
  bool any = false;
  for (const ul::IniLine& line : ini->lines) {
    if (line.key == want) { any = true; break; }
  }
  if (!any) {
    ul_ini_set(ini, "ddraw", key, value);
    return;
  }
  // Every section that already states it, gathered first: setting inside the
  // loop would rewrite lines while walking them.
  std::vector<std::string> sections;
  for (const ul::IniLine& line : ini->lines) {
    if (line.key != want) continue;
    bool seen = false;
    for (const std::string& s : sections) {
      if (s == line.section) { seen = true; break; }
    }
    if (!seen) sections.push_back(line.section);
  }
  for (const std::string& section : sections) {
    ul_ini_set(ini, section.c_str(), key, value);
  }
}

// ------------------------------------------------------- the display settings
//
// cnc-ddraw spells the display mode as two booleans, and the pair does not read
// the way anyone expects: `fullscreen` on its own is exclusive fullscreen, and
// `fullscreen` *with* `windowed` is a borderless window filling the screen.
// Three named modes go in front of a person; the pair stays in here.

namespace {
constexpr char kSection[] = "ddraw";

bool Truthy(const char* value) {
  const std::string text = ul::Lower(ul::Trim(value ? value : ""));
  return text == "true" || text == "yes" || text == "1" || text == "on";
}
}  // namespace

int ul_display_mode(const ul_ini* ini) {
  if (!Truthy(ul_ini_effective(ini, "fullscreen"))) return UL_DISPLAY_WINDOWED;
  return Truthy(ul_ini_effective(ini, "windowed")) ? UL_DISPLAY_BORDERLESS
                                                   : UL_DISPLAY_FULLSCREEN;
}

void ul_display_set_mode(ul_ini* ini, int mode) {
  switch (mode) {
    case UL_DISPLAY_FULLSCREEN:
      ul_ini_set_everywhere(ini, "fullscreen", "true");
      ul_ini_set_everywhere(ini, "windowed", "false");
      break;
    case UL_DISPLAY_WINDOWED:
      ul_ini_set_everywhere(ini, "fullscreen", "false");
      ul_ini_set_everywhere(ini, "windowed", "true");
      break;
    default:
      ul_ini_set_everywhere(ini, "fullscreen", "true");
      ul_ini_set_everywhere(ini, "windowed", "true");
      break;
  }
}

int ul_display_keep_aspect(const ul_ini* ini) {
  return Truthy(ul_ini_effective(ini, "maintas")) ? 1 : 0;
}

void ul_display_set_keep_aspect(ul_ini* ini, int on) {
  ul_ini_set_everywhere(ini, "maintas", on ? "true" : "false");
}

const char* ul_display_shader(const ul_ini* ini) {
  return ul_ini_effective(ini, "shader");
}

void ul_display_set_shader(ul_ini* ini, const char* file) {
  ul_ini_set_everywhere(ini, "shader", file ? file : "");
}

}  // extern "C"

// What was written, so that what was written can be taken back.
//
// The receipt is the whole reason uninstall is safe. A War2Combat folder
// already contains .w2p files that are not the mod's — AutoWarLat.w2p,
// CpuSaveC.w2p, PlaySound.w2p, lobby_map.w2p ship with the game — and a
// "remove the mod" that worked by pattern or by extension would take them too.
// Nothing is removed unless this file says the manager put it there.
//
// It is kept beside the manager's own store rather than in the game folder:
// someone who deletes the game folder has uninstalled already, and someone who
// reinstalls the game over the top has not.

#include "uniloader/uniloader.h"

#include "json.hpp"
#include "util.hpp"

#include <new>
#include <string>
#include <vector>

namespace ul {

struct ReceiptEntry {
  std::string dest;
  std::string backup;   // "" when nothing was there before
};

}  // namespace ul

struct ul_receipt {
  std::string mod_version;
  std::string plugin;
  int variant = 0;
  std::string package_dir;
  std::string game_dir;
  std::vector<ul::ReceiptEntry> entries;
};

namespace ul {
namespace {

/// The schema number. Bumped only for a change a previous build cannot read;
/// a receipt from a newer build is refused rather than half-understood, because
/// half-understanding a receipt means an uninstall that misses files.
constexpr int64_t kSchema = 1;

/// One JSON string, quotes and all. The escaping itself is shared with the
/// package stamp, which writes Windows paths for the same reason this does.
void AppendJsonString(std::string& out, const std::string& value) {
  out.push_back('"');
  out += JsonEscaped(value);
  out.push_back('"');
}

void AppendField(std::string& out, const char* key, const std::string& value,
                 bool comma = true) {
  out += "  ";
  AppendJsonString(out, key);
  out += ": ";
  AppendJsonString(out, value);
  if (comma) out += ",";
  out += "\n";
}

}  // namespace
}  // namespace ul

// ------------------------------------------------------------------- the ABI

extern "C" {

ul_receipt* ul_receipt_create(const char* mod_version) {
  auto receipt = new (std::nothrow) ul_receipt();
  if (!receipt) return nullptr;
  if (mod_version) receipt->mod_version = mod_version;
  return receipt;
}

void ul_receipt_free(ul_receipt* r) { delete r; }

void ul_receipt_add(ul_receipt* r, const char* dest, const char* backup) {
  if (!r || !dest || !*dest) return;
  ul::ReceiptEntry entry;
  entry.dest = dest;
  if (backup) entry.backup = backup;
  r->entries.push_back(std::move(entry));
}

void ul_receipt_set_plugin(ul_receipt* r, const char* plugin_id) {
  if (!r) return;
  r->plugin = plugin_id ? plugin_id : "";
}

void ul_receipt_set_variant(ul_receipt* r, int variant) {
  if (!r) return;
  r->variant = variant < 0 ? 0 : variant;
}

void ul_receipt_set_package_dir(ul_receipt* r, const char* dir) {
  if (!r) return;
  r->package_dir = dir ? dir : "";
}

void ul_receipt_set_game_dir(ul_receipt* r, const char* dir) {
  if (!r) return;
  r->game_dir = dir ? dir : "";
}

char* ul_receipt_to_json(const ul_receipt* r, size_t* length) {
  if (!r) return nullptr;
  std::string out;
  out += "{\n";
  out += "  \"uniloader_receipt\": " + std::to_string(ul::kSchema) + ",\n";
  ul::AppendField(out, "mod_version", r->mod_version);
  ul::AppendField(out, "plugin", r->plugin);
  out += "  \"variant\": " + std::to_string(r->variant) + ",\n";
  ul::AppendField(out, "package_dir", r->package_dir);
  ul::AppendField(out, "game_dir", r->game_dir);
  out += "  \"files\": [\n";
  for (size_t i = 0; i < r->entries.size(); ++i) {
    out += "    {\"dest\": ";
    ul::AppendJsonString(out, r->entries[i].dest);
    out += ", \"backup\": ";
    ul::AppendJsonString(out, r->entries[i].backup);
    out += "}";
    if (i + 1 != r->entries.size()) out += ",";
    out += "\n";
  }
  out += "  ]\n}\n";
  if (length) *length = out.size();
  return ul::Duplicate(out);
}

ul_receipt* ul_receipt_parse(const char* json, size_t length) {
  if (!json) return nullptr;
  ul::Json document;
  if (!ul::Json::Parse(json, length, document) || document.type != ul::Json::Type::Object) {
    ul::SetLastError("The install record is not readable.");
    return nullptr;
  }
  const int64_t schema = document.Int("uniloader_receipt", 0);
  if (schema == 0) {
    ul::SetLastError("That file is not a UniLoader install record.");
    return nullptr;
  }
  if (schema > ul::kSchema) {
    // Refused rather than read as far as it goes. A newer receipt may name
    // files in a way this build does not understand, and an uninstall that
    // silently skips those leaves the game folder modified and unclaimed.
    ul::SetLastError(
        "That install record was written by a newer version of UniLoader. "
        "Update UniLoader to remove this install.");
    return nullptr;
  }

  auto receipt = new (std::nothrow) ul_receipt();
  if (!receipt) return nullptr;
  receipt->mod_version = document.Str("mod_version");
  receipt->plugin = document.Str("plugin");
  receipt->variant = static_cast<int>(document.Int("variant", 0));
  receipt->package_dir = document.Str("package_dir");
  receipt->game_dir = document.Str("game_dir");
  for (const ul::Json& file : document.Array("files")) {
    const std::string dest = file.Str("dest");
    if (dest.empty()) continue;
    receipt->entries.push_back(ul::ReceiptEntry{dest, file.Str("backup")});
  }
  return receipt;
}

const char* ul_receipt_mod_version(const ul_receipt* r) {
  return r ? r->mod_version.c_str() : "";
}
const char* ul_receipt_plugin(const ul_receipt* r) { return r ? r->plugin.c_str() : ""; }
int ul_receipt_variant(const ul_receipt* r) { return r ? r->variant : 0; }
const char* ul_receipt_package_dir(const ul_receipt* r) {
  return r ? r->package_dir.c_str() : "";
}
const char* ul_receipt_game_dir(const ul_receipt* r) { return r ? r->game_dir.c_str() : ""; }

int ul_receipt_count(const ul_receipt* r) {
  return r ? static_cast<int>(r->entries.size()) : 0;
}

const char* ul_receipt_dest(const ul_receipt* r, int index) {
  if (!r || index < 0 || index >= static_cast<int>(r->entries.size())) return "";
  return r->entries[static_cast<size_t>(index)].dest.c_str();
}

const char* ul_receipt_backup(const ul_receipt* r, int index) {
  if (!r || index < 0 || index >= static_cast<int>(r->entries.size())) return "";
  return r->entries[static_cast<size_t>(index)].backup.c_str();
}

}  // extern "C"

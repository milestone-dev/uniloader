#include "json.hpp"

#include <cmath>
#include <cstdlib>
#include <cstring>

namespace ul {
namespace {

struct Reader {
  const char* p;
  const char* end;
  bool ok = true;

  void Skip() {
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p;
  }
  bool Eat(char c) {
    Skip();
    if (p < end && *p == c) { ++p; return true; }
    return false;
  }
  bool Literal(const char* word) {
    const size_t n = std::strlen(word);
    if (static_cast<size_t>(end - p) < n || std::memcmp(p, word, n) != 0) return false;
    p += n;
    return true;
  }
};

/// One \uXXXX escape, appended as UTF-8.
///
/// Surrogate pairs are joined, because GameBanana's notes are written by people
/// and people use emoji — a lone high surrogate emitted as three bytes of UTF-8
/// is not text any Windows control will draw. An unpaired surrogate becomes
/// U+FFFD rather than failing the parse: the response is still a mod page.
void AppendEscape(Reader& r, std::string& out) {
  auto hex4 = [&](unsigned& value) -> bool {
    if (r.end - r.p < 4) return false;
    value = 0;
    for (int i = 0; i < 4; ++i) {
      const char c = r.p[i];
      value <<= 4;
      if (c >= '0' && c <= '9') value |= static_cast<unsigned>(c - '0');
      else if (c >= 'a' && c <= 'f') value |= static_cast<unsigned>(c - 'a' + 10);
      else if (c >= 'A' && c <= 'F') value |= static_cast<unsigned>(c - 'A' + 10);
      else return false;
    }
    r.p += 4;
    return true;
  };

  unsigned cp = 0;
  if (!hex4(cp)) { r.ok = false; return; }
  if (cp >= 0xD800 && cp <= 0xDBFF) {
    unsigned low = 0;
    const char* save = r.p;
    if (r.end - r.p >= 6 && r.p[0] == '\\' && r.p[1] == 'u') {
      r.p += 2;
      if (hex4(low) && low >= 0xDC00 && low <= 0xDFFF) {
        cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
      } else {
        r.p = save;
        cp = 0xFFFD;
      }
    } else {
      cp = 0xFFFD;
    }
  } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
    cp = 0xFFFD;
  }

  if (cp < 0x80) {
    out.push_back(static_cast<char>(cp));
  } else if (cp < 0x800) {
    out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp < 0x10000) {
    out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}

bool ParseString(Reader& r, std::string& out) {
  if (!r.Eat('"')) return false;
  out.clear();
  while (r.p < r.end) {
    const char c = *r.p++;
    if (c == '"') return true;
    if (c != '\\') { out.push_back(c); continue; }
    if (r.p >= r.end) return false;
    switch (const char esc = *r.p++) {
      case '"': case '\\': case '/': out.push_back(esc); break;
      case 'b': out.push_back('\b'); break;
      case 'f': out.push_back('\f'); break;
      case 'n': out.push_back('\n'); break;
      case 'r': out.push_back('\r'); break;
      case 't': out.push_back('\t'); break;
      case 'u': AppendEscape(r, out); if (!r.ok) return false; break;
      default: return false;
    }
  }
  return false;
}

bool ParseValue(Reader& r, Json& out, int depth);

bool ParseNumber(Reader& r, Json& out) {
  const char* start = r.p;
  if (r.p < r.end && (*r.p == '-' || *r.p == '+')) ++r.p;
  bool digits = false;
  while (r.p < r.end && *r.p >= '0' && *r.p <= '9') { ++r.p; digits = true; }
  if (r.p < r.end && *r.p == '.') {
    ++r.p;
    while (r.p < r.end && *r.p >= '0' && *r.p <= '9') { ++r.p; digits = true; }
  }
  if (!digits) return false;
  if (r.p < r.end && (*r.p == 'e' || *r.p == 'E')) {
    ++r.p;
    if (r.p < r.end && (*r.p == '-' || *r.p == '+')) ++r.p;
    while (r.p < r.end && *r.p >= '0' && *r.p <= '9') ++r.p;
  }
  // Through a bounded copy rather than strtod on the buffer: the document is
  // not NUL-terminated, and strtod would read past its end looking for one.
  const std::string token(start, static_cast<size_t>(r.p - start));
  out.type = Json::Type::Number;
  out.number = std::strtod(token.c_str(), nullptr);
  return true;
}

bool ParseValue(Reader& r, Json& out, int depth) {
  if (depth <= 0) return false;
  r.Skip();
  if (r.p >= r.end) return false;
  switch (*r.p) {
    case '"':
      out.type = Json::Type::String;
      return ParseString(r, out.text);
    case '{': {
      ++r.p;
      out.type = Json::Type::Object;
      r.Skip();
      if (r.Eat('}')) return true;
      for (;;) {
        std::string key;
        r.Skip();
        if (!ParseString(r, key)) return false;
        if (!r.Eat(':')) return false;
        Json value;
        if (!ParseValue(r, value, depth - 1)) return false;
        out.fields.emplace_back(std::move(key), std::move(value));
        if (r.Eat(',')) continue;
        return r.Eat('}');
      }
    }
    case '[': {
      ++r.p;
      out.type = Json::Type::Array;
      r.Skip();
      if (r.Eat(']')) return true;
      for (;;) {
        Json value;
        if (!ParseValue(r, value, depth - 1)) return false;
        out.items.push_back(std::move(value));
        if (r.Eat(',')) continue;
        return r.Eat(']');
      }
    }
    case 't':
      if (!r.Literal("true")) return false;
      out.type = Json::Type::Bool;
      out.boolean = true;
      return true;
    case 'f':
      if (!r.Literal("false")) return false;
      out.type = Json::Type::Bool;
      out.boolean = false;
      return true;
    case 'n':
      if (!r.Literal("null")) return false;
      out.type = Json::Type::Null;
      return true;
    default:
      return ParseNumber(r, out);
  }
}

const std::string& Empty() {
  static const std::string empty;
  return empty;
}

}  // namespace

bool Json::Parse(const char* data, size_t length, Json& out) {
  if (!data) return false;
  Reader r{data, data + length};
  Json parsed;
  if (!ParseValue(r, parsed, kMaxDepth)) return false;
  r.Skip();
  if (r.p != r.end) return false;   // trailing rubbish is a truncated read
  out = std::move(parsed);
  return true;
}

const Json* Json::Find(const char* key) const {
  if (type != Type::Object || !key) return nullptr;
  for (const auto& field : fields) {
    if (field.first == key) return &field.second;
  }
  return nullptr;
}

const std::string& Json::Str(const char* key) const {
  const Json* value = Find(key);
  return (value && value->type == Type::String) ? value->text : Empty();
}

int64_t Json::AsInt(int64_t fallback) const {
  if (type == Type::Number) return static_cast<int64_t>(number);
  // GameBanana sends some counters as quoted strings and others bare, in the
  // same document. Coercing here beats making every call site ask which.
  if (type == Type::String && !text.empty()) {
    char* stop = nullptr;
    const long long parsed = std::strtoll(text.c_str(), &stop, 10);
    if (stop != text.c_str()) return static_cast<int64_t>(parsed);
  }
  if (type == Type::Bool) return boolean ? 1 : 0;
  return fallback;
}

int64_t Json::Int(const char* key, int64_t fallback) const {
  const Json* value = Find(key);
  return value ? value->AsInt(fallback) : fallback;
}

bool Json::Bool(const char* key, bool fallback) const {
  const Json* value = Find(key);
  if (!value) return fallback;
  if (value->type == Type::Bool) return value->boolean;
  if (value->type == Type::Number) return value->number != 0;
  if (value->type == Type::String) return value->text == "true" || value->text == "1";
  return fallback;
}

const std::vector<Json>& Json::Array(const char* key) const {
  static const std::vector<Json> empty;
  const Json* value = Find(key);
  return (value && value->type == Type::Array) ? value->items : empty;
}

}  // namespace ul

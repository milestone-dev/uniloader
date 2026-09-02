// A small JSON reader, sized for the documents this program actually reads.
//
// Not a general library: no writer (the one thing UniLoader writes is a receipt,
// and receipt.cpp emits that directly), no number formatting, no duplicate-key
// policy beyond first-wins. What it does have is the tolerance the real
// responses need — GameBanana returns "_nStatus":"0" and "_idRow":644456 for
// fields of the same kind, so Int() coerces from a string rather than making
// every caller check which shape today's response used.
//
// Depth is capped. A JSON document is attacker-controlled the moment it arrives
// over the network, and a recursive-descent parser with no limit turns a few
// hundred bytes of "[[[[[..." into a stack overflow.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace ul {

class Json {
 public:
  enum class Type { Null, Bool, Number, String, Array, Object };

  Type type = Type::Null;
  bool boolean = false;
  double number = 0;
  std::string text;
  std::vector<Json> items;                          // Array
  std::vector<std::pair<std::string, Json>> fields; // Object

  /// False on malformed input, on trailing rubbish, or on a document nested
  /// deeper than kMaxDepth. `out` is untouched-but-valid on failure.
  static bool Parse(const char* data, size_t length, Json& out);

  /// The member named `key`, or null. Safe on a non-object.
  const Json* Find(const char* key) const;

  /// A member's value, coerced. Missing, or present with the wrong shape,
  /// yields the default rather than an error: every field GameBanana documents
  /// is optional in practice, and a mod page with no screenshots is a mod page.
  const std::string& Str(const char* key) const;
  int64_t Int(const char* key, int64_t fallback = 0) const;
  bool Bool(const char* key, bool fallback = false) const;
  /// The member named `key` when it is an array, else an empty one — so a
  /// caller can write a range-for over it without a null check.
  const std::vector<Json>& Array(const char* key) const;

  /// This value coerced, for elements of an array.
  int64_t AsInt(int64_t fallback = 0) const;

 private:
  static constexpr int kMaxDepth = 64;
};

}  // namespace ul

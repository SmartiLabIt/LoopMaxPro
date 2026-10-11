// LoopMaxPro - JSON reader without heap and without a document tree.
// A JsonValue is a view over the original text: the text must outlive it.
// Lookups scan the text, which is fine for the small documents exchanged at the boundaries.
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "base/fixed_string.h"
#include "base/string_view.h"

namespace lmp {

enum class JsonType : uint8_t { Invalid, Null, Bool, Number, String, Array, Object };

class JsonValue {
 public:
  JsonValue() = default;

  // Validates the whole text. Returns an invalid value when it is not well formed JSON.
  static JsonValue parse(StringView text);

  JsonType type() const;
  bool valid() const { return begin_ != nullptr; }
  bool isNull() const { return type() == JsonType::Null; }
  bool isBool() const { return type() == JsonType::Bool; }
  bool isNumber() const { return type() == JsonType::Number; }
  bool isString() const { return type() == JsonType::String; }
  bool isArray() const { return type() == JsonType::Array; }
  bool isObject() const { return type() == JsonType::Object; }

  // Text of the value exactly as written in the document.
  StringView raw() const { return StringView(begin_, static_cast<size_t>(end_ - begin_)); }

  // ---- containers. Missing members and out of range indexes give an invalid value.
  JsonValue get(const char* key) const;
  JsonValue operator[](const char* key) const { return get(key); }
  JsonValue at(size_t index) const;
  size_t size() const;  // members of an object, elements of an array

  class Iterator;
  Iterator begin() const;
  Iterator end() const;

  // ---- conversions. The get* forms return false when the type does not match.
  bool getBool(bool& out) const;
  bool getInt(int64_t& out) const;      // also accepts numbers such as 3.0
  bool getDouble(double& out) const;
  bool asBool(bool fallback = false) const;
  int64_t asInt(int64_t fallback = 0) const;
  double asDouble(double fallback = 0) const;

  // Decodes a string into a NUL terminated buffer.
  // Returns false when the value is not a string or does not fit.
  bool getString(char* out, size_t cap) const;
  template <size_t N>
  bool getString(FixedString<N>& out) const { return getString(out.data(), N + 1); }
  // Length of the decoded string, without terminator.
  size_t stringLength() const;
  // Compares the decoded string with a plain text.
  bool stringEquals(StringView text) const;

 private:
  JsonValue(const char* b, const char* e) : begin_(b), end_(e) {}

  const char* begin_ = nullptr;
  const char* end_ = nullptr;
};

// One step of an iteration. For arrays the key is empty.
struct JsonMember {
  JsonValue key;    // a string value, invalid for array elements
  JsonValue value;
};

class JsonValue::Iterator {
 public:
  JsonMember operator*() const { return current_; }
  Iterator& operator++();
  bool operator!=(const Iterator& o) const { return pos_ != o.pos_; }

 private:
  friend class JsonValue;
  Iterator(const char* pos, const char* end, bool object) : pos_(pos), end_(end), object_(object) {
    load();
  }
  void load();

  const char* pos_;   // start of the current member, or null at the end
  const char* end_;
  const char* next_ = nullptr;
  bool object_;
  JsonMember current_;
};

}  // namespace lmp

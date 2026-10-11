#include "kernel/json/json_reader.h"

#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

namespace lmp {
namespace {

constexpr int kMaxDepth = 24;

inline bool isWs(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
inline bool isDigit(char c) { return c >= '0' && c <= '9'; }

inline int hexValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

const char* skipWs(const char* p, const char* e) {
  while (p < e && isWs(*p)) ++p;
  return p;
}

// p points to the opening quote. Returns the position after the closing quote.
const char* skipString(const char* p, const char* e) {
  ++p;
  while (p < e) {
    const char c = *p;
    if (c == '"') return p + 1;
    if (static_cast<unsigned char>(c) < 0x20) return nullptr;
    if (c == '\\') {
      ++p;
      if (p >= e) return nullptr;
      if (*p == 'u') {
        if (e - p < 5) return nullptr;
        for (int i = 1; i <= 4; ++i) {
          if (hexValue(p[i]) < 0) return nullptr;
        }
        p += 4;
      } else if (!strchr("\"\\/bfnrt", *p)) {
        return nullptr;
      }
    }
    ++p;
  }
  return nullptr;
}

const char* skipNumber(const char* p, const char* e) {
  if (p < e && *p == '-') ++p;
  if (p >= e) return nullptr;
  if (*p == '0') {
    ++p;
  } else if (isDigit(*p)) {
    while (p < e && isDigit(*p)) ++p;
  } else {
    return nullptr;
  }
  if (p < e && *p == '.') {
    ++p;
    if (p >= e || !isDigit(*p)) return nullptr;
    while (p < e && isDigit(*p)) ++p;
  }
  if (p < e && (*p == 'e' || *p == 'E')) {
    ++p;
    if (p < e && (*p == '+' || *p == '-')) ++p;
    if (p >= e || !isDigit(*p)) return nullptr;
    while (p < e && isDigit(*p)) ++p;
  }
  return p;
}

const char* skipLiteral(const char* p, const char* e, const char* literal) {
  const size_t n = strlen(literal);
  if (static_cast<size_t>(e - p) < n || memcmp(p, literal, n) != 0) return nullptr;
  return p + n;
}

// Returns the position after the value that starts at p, or null when it is malformed.
const char* skipValue(const char* p, const char* e, int depth) {
  if (p >= e || depth > kMaxDepth) return nullptr;
  switch (*p) {
    case '"': return skipString(p, e);
    case 't': return skipLiteral(p, e, "true");
    case 'f': return skipLiteral(p, e, "false");
    case 'n': return skipLiteral(p, e, "null");
    case '{': {
      p = skipWs(p + 1, e);
      if (p < e && *p == '}') return p + 1;
      for (;;) {
        if (p >= e || *p != '"') return nullptr;
        p = skipString(p, e);
        if (!p) return nullptr;
        p = skipWs(p, e);
        if (p >= e || *p != ':') return nullptr;
        p = skipValue(skipWs(p + 1, e), e, depth + 1);
        if (!p) return nullptr;
        p = skipWs(p, e);
        if (p >= e) return nullptr;
        if (*p == '}') return p + 1;
        if (*p != ',') return nullptr;
        p = skipWs(p + 1, e);
      }
    }
    case '[': {
      p = skipWs(p + 1, e);
      if (p < e && *p == ']') return p + 1;
      for (;;) {
        p = skipValue(p, e, depth + 1);
        if (!p) return nullptr;
        p = skipWs(p, e);
        if (p >= e) return nullptr;
        if (*p == ']') return p + 1;
        if (*p != ',') return nullptr;
        p = skipWs(p + 1, e);
      }
    }
    default:
      return skipNumber(p, e);
  }
}

// Appends a code point as UTF-8. Returns the number of bytes, 0 when it does not fit.
size_t putUtf8(uint32_t cp, char* out, size_t room) {
  if (cp < 0x80) {
    if (room < 1) return 0;
    out[0] = static_cast<char>(cp);
    return 1;
  }
  if (cp < 0x800) {
    if (room < 2) return 0;
    out[0] = static_cast<char>(0xC0 | (cp >> 6));
    out[1] = static_cast<char>(0x80 | (cp & 0x3F));
    return 2;
  }
  if (cp < 0x10000) {
    if (room < 3) return 0;
    out[0] = static_cast<char>(0xE0 | (cp >> 12));
    out[1] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out[2] = static_cast<char>(0x80 | (cp & 0x3F));
    return 3;
  }
  if (room < 4) return 0;
  out[0] = static_cast<char>(0xF0 | (cp >> 18));
  out[1] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
  out[2] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
  out[3] = static_cast<char>(0x80 | (cp & 0x3F));
  return 4;
}

uint32_t readHex4(const char* p) {
  return static_cast<uint32_t>((hexValue(p[0]) << 12) | (hexValue(p[1]) << 8) |
                               (hexValue(p[2]) << 4) | hexValue(p[3]));
}

// Decodes the string between the quotes. With out == null only measures.
// Returns the decoded length, or npos when the buffer is too small.
size_t decodeString(const char* begin, const char* end, char* out, size_t cap) {
  const char* p = begin + 1;
  const char* e = end - 1;
  size_t len = 0;
  char tmp[4];
  while (p < e) {
    size_t n = 1;
    tmp[0] = *p;
    if (*p == '\\') {
      ++p;
      switch (*p) {
        case 'n': tmp[0] = '\n'; break;
        case 'r': tmp[0] = '\r'; break;
        case 't': tmp[0] = '\t'; break;
        case 'b': tmp[0] = '\b'; break;
        case 'f': tmp[0] = '\f'; break;
        case 'u': {
          uint32_t cp = readHex4(p + 1);
          p += 4;
          // Surrogate pair: two escapes make one code point.
          if (cp >= 0xD800 && cp <= 0xDBFF && e - p >= 7 && p[1] == '\\' && p[2] == 'u') {
            const uint32_t low = readHex4(p + 3);
            if (low >= 0xDC00 && low <= 0xDFFF) {
              cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
              p += 6;
            }
          }
          n = putUtf8(cp, tmp, sizeof(tmp));
          break;
        }
        default: tmp[0] = *p; break;  // quote, backslash, slash
      }
    }
    ++p;
    if (out) {
      if (len + n >= cap) return StringView::npos;
      memcpy(out + len, tmp, n);
    }
    len += n;
  }
  if (out) out[len] = '\0';
  return len;
}

}  // namespace

// ---------------------------------------------------------------- JsonValue

JsonValue JsonValue::parse(StringView text) {
  if (!text.data) return JsonValue();
  const char* e = text.end();
  const char* b = skipWs(text.begin(), e);
  const char* after = skipValue(b, e, 0);
  if (!after) return JsonValue();
  if (skipWs(after, e) != e) return JsonValue();
  return JsonValue(b, after);
}

JsonType JsonValue::type() const {
  if (!begin_) return JsonType::Invalid;
  switch (*begin_) {
    case '{': return JsonType::Object;
    case '[': return JsonType::Array;
    case '"': return JsonType::String;
    case 't':
    case 'f': return JsonType::Bool;
    case 'n': return JsonType::Null;
    default:  return JsonType::Number;
  }
}

JsonValue::Iterator JsonValue::begin() const {
  const JsonType t = type();
  if (t != JsonType::Object && t != JsonType::Array) return end();
  const char* p = skipWs(begin_ + 1, end_);
  if (p >= end_ || *p == '}' || *p == ']') return end();
  return Iterator(p, end_, t == JsonType::Object);
}

JsonValue::Iterator JsonValue::end() const { return Iterator(nullptr, end_, false); }

void JsonValue::Iterator::load() {
  current_ = JsonMember();
  next_ = nullptr;
  if (!pos_) return;
  const char* p = pos_;
  if (object_) {
    const char* keyEnd = skipString(p, end_);
    if (!keyEnd) {
      pos_ = nullptr;
      return;
    }
    current_.key = JsonValue(p, keyEnd);
    p = skipWs(keyEnd, end_);
    p = skipWs(p + 1, end_);  // colon
  }
  const char* valueEnd = skipValue(p, end_, 1);
  if (!valueEnd) {
    pos_ = nullptr;
    return;
  }
  current_.value = JsonValue(p, valueEnd);
  p = skipWs(valueEnd, end_);
  if (p < end_ && *p == ',') next_ = skipWs(p + 1, end_);
}

JsonValue::Iterator& JsonValue::Iterator::operator++() {
  pos_ = next_;
  load();
  return *this;
}

JsonValue JsonValue::get(const char* key) const {
  if (!isObject() || !key) return JsonValue();
  const StringView wanted(key);
  for (const JsonMember m : *this) {
    if (m.key.stringEquals(wanted)) return m.value;
  }
  return JsonValue();
}

JsonValue JsonValue::at(size_t index) const {
  if (!isArray()) return JsonValue();
  size_t i = 0;
  for (const JsonMember m : *this) {
    if (i++ == index) return m.value;
  }
  return JsonValue();
}

size_t JsonValue::size() const {
  size_t n = 0;
  for (const JsonMember m : *this) {
    (void)m;
    ++n;
  }
  return n;
}

bool JsonValue::getBool(bool& out) const {
  if (!isBool()) return false;
  out = (*begin_ == 't');
  return true;
}

bool JsonValue::getDouble(double& out) const {
  if (!isNumber()) return false;
  char buf[48];
  if (raw().size >= sizeof(buf)) return false;
  raw().copyTo(buf, sizeof(buf));
  char* endp = nullptr;
  const double v = strtod(buf, &endp);
  if (endp == buf || isnan(v) || isinf(v)) return false;
  out = v;
  return true;
}

bool JsonValue::getInt(int64_t& out) const {
  if (!isNumber()) return false;
  char buf[48];
  const StringView text = raw();
  if (text.size >= sizeof(buf)) return false;
  text.copyTo(buf, sizeof(buf));

  const bool plain = text.find('.') == StringView::npos && text.find('e') == StringView::npos &&
                     text.find('E') == StringView::npos;
  if (plain) {
    errno = 0;
    char* endp = nullptr;
    const long long v = strtoll(buf, &endp, 10);
    if (endp == buf || errno == ERANGE) return false;
    out = static_cast<int64_t>(v);
    return true;
  }
  // Written with a fraction or an exponent: accepted only when the value is whole.
  double d = 0;
  if (!getDouble(d)) return false;
  if (d != floor(d) || d < -9007199254740992.0 || d > 9007199254740992.0) return false;
  out = static_cast<int64_t>(d);
  return true;
}

bool JsonValue::asBool(bool fallback) const {
  bool v = fallback;
  getBool(v);
  return v;
}

int64_t JsonValue::asInt(int64_t fallback) const {
  int64_t v = fallback;
  getInt(v);
  return v;
}

double JsonValue::asDouble(double fallback) const {
  double v = fallback;
  getDouble(v);
  return v;
}

bool JsonValue::getString(char* out, size_t cap) const {
  if (!isString() || !out || cap == 0) return false;
  if (decodeString(begin_, end_, out, cap) == StringView::npos) {
    out[0] = '\0';
    return false;
  }
  return true;
}

size_t JsonValue::stringLength() const {
  if (!isString()) return 0;
  return decodeString(begin_, end_, nullptr, 0);
}

bool JsonValue::stringEquals(StringView text) const {
  if (!isString()) return false;
  const StringView inner(begin_ + 1, static_cast<size_t>(end_ - begin_) - 2);
  // Fast path: no escapes, compare the bytes as they are.
  if (inner.find('\\') == StringView::npos) return inner.equals(text);
  char buf[96];
  if (decodeString(begin_, end_, buf, sizeof(buf)) == StringView::npos) return false;
  return StringView(buf).equals(text);
}

}  // namespace lmp

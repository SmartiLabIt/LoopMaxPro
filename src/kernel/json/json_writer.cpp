#include "kernel/json/json_writer.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace lmp {

bool BufferSink::write(const char* data, size_t len) {
  if (cap_ == 0) {
    overflow_ = true;
    return false;
  }
  const size_t room = cap_ - 1 - len_;
  if (len > room) {
    // Keep what fits so the caller can still inspect the beginning of the text.
    if (room) memcpy(buf_ + len_, data, room);
    len_ += room;
    buf_[len_] = '\0';
    overflow_ = true;
    return false;
  }
  if (len) memcpy(buf_ + len_, data, len);
  len_ += len;
  buf_[len_] = '\0';
  return true;
}

void JsonWriter::put(const char* data, size_t len) {
  if (!ok_) return;
  if (!sink_.write(data, len)) ok_ = false;
}

void JsonWriter::beforeValue() {
  if (afterKey_) {
    afterKey_ = false;
    return;
  }
  if (depth_ == 0) return;
  const uint32_t bit = 1u << depth_;
  if (needComma_ & bit) put(',');
  needComma_ |= bit;
}

JsonWriter& JsonWriter::key_(const char* k) {
  if (afterKey_ || depth_ == 0) {
    ok_ = false;  // key after key, or key outside an object
    return *this;
  }
  const uint32_t bit = 1u << depth_;
  if (needComma_ & bit) put(',');
  needComma_ |= bit;
  put('"');
  putEscaped(StringView(k));
  put("\":", 2);
  afterKey_ = true;
  return *this;
}

void JsonWriter::open(char c) {
  beforeValue();
  if (depth_ + 1 >= kMaxDepth) {
    ok_ = false;
    return;
  }
  put(c);
  ++depth_;
  needComma_ &= ~(1u << depth_);
}

void JsonWriter::close(char c) {
  if (depth_ == 0 || afterKey_) {
    ok_ = false;
    return;
  }
  --depth_;
  put(c);
}

JsonWriter& JsonWriter::beginObject() {
  open('{');
  return *this;
}
JsonWriter& JsonWriter::endObject() {
  close('}');
  return *this;
}
JsonWriter& JsonWriter::beginArray() {
  open('[');
  return *this;
}
JsonWriter& JsonWriter::endArray() {
  close(']');
  return *this;
}

void JsonWriter::putEscaped(StringView s) {
  static const char hex[] = "0123456789abcdef";
  size_t start = 0;
  for (size_t i = 0; i < s.size; ++i) {
    const unsigned char c = static_cast<unsigned char>(s.data[i]);
    const char* esc = nullptr;
    char unicode[6];
    size_t escLen = 2;
    switch (c) {
      case '"':  esc = "\\\""; break;
      case '\\': esc = "\\\\"; break;
      case '\n': esc = "\\n"; break;
      case '\r': esc = "\\r"; break;
      case '\t': esc = "\\t"; break;
      case '\b': esc = "\\b"; break;
      case '\f': esc = "\\f"; break;
      default:
        if (c < 0x20) {
          unicode[0] = '\\';
          unicode[1] = 'u';
          unicode[2] = '0';
          unicode[3] = '0';
          unicode[4] = hex[c >> 4];
          unicode[5] = hex[c & 0x0F];
          esc = unicode;
          escLen = 6;
        }
        break;
    }
    if (esc) {
      if (i > start) put(s.data + start, i - start);
      put(esc, escLen);
      start = i + 1;
    }
  }
  if (s.size > start) put(s.data + start, s.size - start);
}

JsonWriter& JsonWriter::str(StringView v) {
  beforeValue();
  put('"');
  putEscaped(v);
  put('"');
  return *this;
}

JsonWriter& JsonWriter::u64(uint64_t v) {
  beforeValue();
  char buf[24];
  char* p = buf + sizeof(buf);
  do {
    *--p = static_cast<char>('0' + (v % 10));
    v /= 10;
  } while (v);
  put(p, static_cast<size_t>(buf + sizeof(buf) - p));
  return *this;
}

JsonWriter& JsonWriter::i64(int64_t v) {
  if (v >= 0) return u64(static_cast<uint64_t>(v));
  beforeValue();
  char buf[24];
  char* p = buf + sizeof(buf);
  // Negate in unsigned space: works for the most negative value too.
  uint64_t u = static_cast<uint64_t>(0) - static_cast<uint64_t>(v);
  do {
    *--p = static_cast<char>('0' + (u % 10));
    u /= 10;
  } while (u);
  *--p = '-';
  put(p, static_cast<size_t>(buf + sizeof(buf) - p));
  return *this;
}

JsonWriter& JsonWriter::f64(double v, uint8_t decimals) {
  if (isnan(v) || isinf(v)) return null();
  beforeValue();
  if (decimals > 9) decimals = 9;
  char buf[40];
  int n = snprintf(buf, sizeof(buf), "%.*f", static_cast<int>(decimals), v);
  if (n < 0 || static_cast<size_t>(n) >= sizeof(buf)) {
    // Too large for fixed notation.
    n = snprintf(buf, sizeof(buf), "%.9e", v);
    if (n < 0 || static_cast<size_t>(n) >= sizeof(buf)) {
      put("null", 4);
      return *this;
    }
    put(buf, static_cast<size_t>(n));
    return *this;
  }
  size_t len = static_cast<size_t>(n);
  if (memchr(buf, '.', len)) {
    while (len > 1 && buf[len - 1] == '0') --len;
    if (buf[len - 1] == '.') --len;
  }
  if (len == 2 && buf[0] == '-' && buf[1] == '0') {
    buf[0] = '0';
    len = 1;
  }
  put(buf, len);
  return *this;
}

JsonWriter& JsonWriter::boolean(bool v) {
  beforeValue();
  if (v) {
    put("true", 4);
  } else {
    put("false", 5);
  }
  return *this;
}

JsonWriter& JsonWriter::null() {
  beforeValue();
  put("null", 4);
  return *this;
}

JsonWriter& JsonWriter::raw(StringView json) {
  beforeValue();
  if (json.empty()) {
    put("null", 4);
  } else {
    put(json.data, json.size);
  }
  return *this;
}

}  // namespace lmp

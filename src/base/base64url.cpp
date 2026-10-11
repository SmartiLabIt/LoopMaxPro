#include "base/base64url.h"

namespace lmp {
namespace {

constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

int valueOf(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '-') return 62;
  if (c == '_') return 63;
  return -1;
}

}  // namespace

bool base64UrlDecode(StringView text, uint8_t* out, size_t cap, size_t& length) {
  length = 0;
  // One character alone carries less than a byte: no text ends like that.
  if (text.size % 4 == 1) return false;
  uint32_t bits = 0;
  uint8_t count = 0;
  for (size_t i = 0; i < text.size; ++i) {
    const int v = valueOf(text.data[i]);
    if (v < 0) return false;
    bits = (bits << 6) | static_cast<uint32_t>(v);
    count += 6;
    if (count >= 8) {
      count -= 8;
      if (length >= cap) return false;
      out[length++] = static_cast<uint8_t>(bits >> count);
    }
  }
  return true;
}

bool base64UrlEncode(const uint8_t* data, size_t length, char* out, size_t cap) {
  size_t at = 0;
  uint32_t bits = 0;
  uint8_t count = 0;
  for (size_t i = 0; i < length; ++i) {
    bits = (bits << 8) | data[i];
    count += 8;
    while (count >= 6) {
      count -= 6;
      if (at + 1 >= cap) return false;
      out[at++] = kAlphabet[(bits >> count) & 63];
    }
  }
  if (count) {
    if (at + 1 >= cap) return false;
    out[at++] = kAlphabet[(bits << (6 - count)) & 63];
  }
  if (at >= cap) return false;
  out[at] = '\0';
  return true;
}

}  // namespace lmp

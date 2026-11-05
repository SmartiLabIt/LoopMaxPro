#include "base/sha256.h"

#include <string.h>

namespace lmp {
namespace {

const uint32_t kRound[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
    0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
    0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
    0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
    0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
    0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
    0xc67178f2};

inline uint32_t rotr(uint32_t v, unsigned n) { return (v >> n) | (v << (32 - n)); }

int hexValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

}  // namespace

void Sha256::reset() {
  static const uint32_t start[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  memcpy(state_, start, sizeof(state_));
  total_ = 0;
  pendingLen_ = 0;
}

void Sha256::block(const uint8_t* data) {
  uint32_t w[64];
  for (size_t i = 0; i < 16; ++i) {
    w[i] = (static_cast<uint32_t>(data[i * 4]) << 24) |
           (static_cast<uint32_t>(data[i * 4 + 1]) << 16) |
           (static_cast<uint32_t>(data[i * 4 + 2]) << 8) | static_cast<uint32_t>(data[i * 4 + 3]);
  }
  for (size_t i = 16; i < 64; ++i) {
    const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
    const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }

  uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
  uint32_t e = state_[4], f = state_[5], g = state_[6], h = state_[7];
  for (size_t i = 0; i < 64; ++i) {
    const uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
    const uint32_t choice = (e & f) ^ (~e & g);
    const uint32_t t1 = h + s1 + choice + kRound[i] + w[i];
    const uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
    const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
    const uint32_t t2 = s0 + majority;
    h = g;
    g = f;
    f = e;
    e = d + t1;
    d = c;
    c = b;
    b = a;
    a = t1 + t2;
  }
  state_[0] += a;
  state_[1] += b;
  state_[2] += c;
  state_[3] += d;
  state_[4] += e;
  state_[5] += f;
  state_[6] += g;
  state_[7] += h;
}

void Sha256::update(const uint8_t* data, size_t len) {
  if (!data || len == 0) return;
  total_ += len;
  if (pendingLen_) {
    const size_t room = sizeof(pending_) - pendingLen_;
    const size_t n = len < room ? len : room;
    memcpy(pending_ + pendingLen_, data, n);
    pendingLen_ += n;
    data += n;
    len -= n;
    if (pendingLen_ < sizeof(pending_)) return;
    block(pending_);
    pendingLen_ = 0;
  }
  while (len >= sizeof(pending_)) {
    block(data);
    data += sizeof(pending_);
    len -= sizeof(pending_);
  }
  if (len) {
    memcpy(pending_, data, len);
    pendingLen_ = len;
  }
}

void Sha256::finish(uint8_t out[kSize]) {
  const uint64_t bits = total_ * 8;
  pending_[pendingLen_++] = 0x80;
  if (pendingLen_ > 56) {
    memset(pending_ + pendingLen_, 0, sizeof(pending_) - pendingLen_);
    block(pending_);
    pendingLen_ = 0;
  }
  memset(pending_ + pendingLen_, 0, 56 - pendingLen_);
  for (size_t i = 0; i < 8; ++i) {
    pending_[56 + i] = static_cast<uint8_t>(bits >> (56 - 8 * i));
  }
  block(pending_);
  for (size_t i = 0; i < 8; ++i) {
    out[i * 4] = static_cast<uint8_t>(state_[i] >> 24);
    out[i * 4 + 1] = static_cast<uint8_t>(state_[i] >> 16);
    out[i * 4 + 2] = static_cast<uint8_t>(state_[i] >> 8);
    out[i * 4 + 3] = static_cast<uint8_t>(state_[i]);
  }
  pendingLen_ = 0;
}

void sha256Hex(const uint8_t digest[Sha256::kSize], char* out) {
  static const char digits[] = "0123456789abcdef";
  for (size_t i = 0; i < Sha256::kSize; ++i) {
    out[i * 2] = digits[digest[i] >> 4];
    out[i * 2 + 1] = digits[digest[i] & 0x0F];
  }
  out[Sha256::kSize * 2] = '\0';
}

bool sha256Parse(StringView hex, uint8_t out[Sha256::kSize]) {
  if (hex.size != Sha256::kSize * 2) return false;
  for (size_t i = 0; i < Sha256::kSize; ++i) {
    const int high = hexValue(hex[i * 2]);
    const int low = hexValue(hex[i * 2 + 1]);
    if (high < 0 || low < 0) return false;
    out[i] = static_cast<uint8_t>(high * 16 + low);
  }
  return true;
}

}  // namespace lmp

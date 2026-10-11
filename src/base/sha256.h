// LoopMaxPro - SHA-256, the fingerprint of a firmware image.
// Plain C++: the same on every board and on the PC, no heap.
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "base/string_view.h"

namespace lmp {

class Sha256 {
 public:
  static constexpr size_t kSize = 32;

  Sha256() { reset(); }

  void reset();
  void update(const uint8_t* data, size_t len);
  // Closes the computation. Call reset() before using the object again.
  void finish(uint8_t out[kSize]);

 private:
  void block(const uint8_t* data);

  uint32_t state_[8];
  uint64_t total_ = 0;
  uint8_t pending_[64];
  size_t pendingLen_ = 0;
};

// Writes 64 lower case digits and the terminator. The buffer holds at least 65 characters.
void sha256Hex(const uint8_t digest[Sha256::kSize], char* out);
// Reads 64 hexadecimal digits, upper or lower case. False for anything else.
bool sha256Parse(StringView hex, uint8_t out[Sha256::kSize]);

}  // namespace lmp

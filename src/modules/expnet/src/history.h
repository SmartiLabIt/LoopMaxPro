// ExpNet - what a watched service did in the last 7 days, in blocks of 15 minutes: how many
// checks, how many answered, their average time. 672 blocks, 4 bytes each, kept in a file of
// the service and never all in memory: the block that is filling stays in memory, the closed
// ones go to the file. The file is a ring: the block of a time goes to the place of its number
// in the ring, and the header says the last block written, so that what is older than 7 days,
// or never written, is told apart from what is there.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace expnet {

constexpr uint32_t kBlockSec = 900;    // 15 minutes
constexpr uint16_t kBlocks = 672;      // 7 days
constexpr uint16_t kBlocksDay = 96;
constexpr size_t kHistoryHead = 8;     // "DBH1", then the number of the last block
constexpr size_t kHistoryBytes = kHistoryHead + static_cast<size_t>(kBlocks) * 4;

struct Block {
  uint16_t avgMs = 0;   // average time of the checks that answered
  uint8_t ok = 0;       // checks that answered
  uint8_t total = 0;    // checks made: 0, nothing known of that time
};

// The number of the block of a time: the 15 minutes since 1970.
inline uint32_t blockOf(int64_t unix) { return static_cast<uint32_t>(unix / kBlockSec); }

// The block that is filling, in memory.
class Filling {
 public:
  void add(bool ok, uint32_t ms) {
    if (total_ == 255) return;
    ++total_;
    if (!ok) return;
    ++ok_;
    sumMs_ += ms > 60000 ? 60000 : ms;
  }
  bool empty() const { return total_ == 0; }
  Block block() const {
    Block b;
    b.total = total_;
    b.ok = ok_;
    b.avgMs = ok_ ? static_cast<uint16_t>(sumMs_ / ok_) : 0;
    return b;
  }
  void clear() { *this = Filling(); }

 private:
  uint32_t sumMs_ = 0;
  uint8_t ok_ = 0;
  uint8_t total_ = 0;
};

// The file of a service, read and written whole: kHistoryBytes. A buffer of that size that
// does not start with the mark is an empty history.
class HistoryFile {
 public:
  explicit HistoryFile(uint8_t* bytes) : b_(bytes) {}

  void clear() {
    memset(b_, 0, kHistoryBytes);
    memcpy(b_, "DBH1", 4);
  }
  bool valid() const { return memcmp(b_, "DBH1", 4) == 0; }
  uint32_t last() const {
    uint32_t n = 0;
    memcpy(&n, b_ + 4, 4);
    return n;
  }

  // Writes the block number n. The blocks between the last one written and n were not seen:
  // they are emptied, not left with what they had 7 days before.
  void put(uint32_t n, const Block& block) {
    if (!valid()) clear();
    const uint32_t was = last();
    if (was && n > was) {
      const uint32_t gap = n - was - 1;
      for (uint32_t i = 1; i <= gap && i <= kBlocks; ++i) write(was + i, Block());
    }
    write(n, block);
    if (n > was) memcpy(b_ + 4, &n, 4);
  }

  // The block number n, when it is within the 7 days before now and was written.
  Block get(uint32_t n, uint32_t now) const {
    const uint32_t was = last();
    if (!valid() || !was || n > was || n > now || now - n >= kBlocks) return Block();
    Block b;
    const uint8_t* at = b_ + kHistoryHead + static_cast<size_t>(n % kBlocks) * 4;
    memcpy(&b.avgMs, at, 2);
    b.ok = at[2];
    b.total = at[3];
    return b;
  }

 private:
  void write(uint32_t n, const Block& block) {
    uint8_t* at = b_ + kHistoryHead + static_cast<size_t>(n % kBlocks) * 4;
    memcpy(at, &block.avgMs, 2);
    at[2] = block.ok;
    at[3] = block.total;
  }

  uint8_t* b_;
};

// What a stretch of blocks says: checks, answers, average time of the answers.
struct Summary {
  uint32_t ok = 0;
  uint32_t total = 0;
  uint32_t avgMs = 0;
  // Per thousand: 1000 always answered. -1: no check in that time.
  int32_t uptime() const { return total ? static_cast<int32_t>(ok * 1000ull / total) : -1; }
};

inline Summary summarize(const HistoryFile& f, uint32_t now, uint16_t blocks,
                         const Filling* filling = nullptr) {
  Summary s;
  uint64_t sumMs = 0;
  for (uint16_t i = 0; i < blocks; ++i) {
    const Block b = (i == 0 && filling) ? filling->block() : f.get(now - i, now);
    s.total += b.total;
    s.ok += b.ok;
    sumMs += static_cast<uint64_t>(b.avgMs) * b.ok;
  }
  s.avgMs = s.ok ? static_cast<uint32_t>(sumMs / s.ok) : 0;
  return s;
}

}  // namespace expnet

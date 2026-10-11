// LoopMaxPro - system service: chip, memory, uptime, restart, the starts of the board.
#pragma once
#include "kernel/kernel.h"

namespace lmp {

class SystemService : public Service {
 public:
  const ServiceInfo& info() const override;
  Status start(Kernel& kernel) override;
  void onEvent(const Event& event) override;
  void describe(JsonWriter& w) const override;
  void commands(CommandSet& set) override;
  const char* reportPart() const override { return "system"; }

 private:
  // The starts of the board, kept where a loss of power does not reach: how many, and of the
  // last ones why the board started and when. The log and the last words live in memories a
  // power cut clears: a board that was off, or that started again alone, tells it with these.
  // Written once a start, and once more when the clock gets the hour.
  static constexpr uint8_t kStartsKept = 6;
  struct StartNote {
    uint32_t at = 0;      // Unix time of the start; 0: the clock never had the hour in it
    uint32_t ranS = 0;    // how long it lasted; 0: the next start could not tell (power lost)
    uint8_t reason = 0;   // hal::ResetReason
  };
  void countStart();
  void saveStarts();

  Kernel* kernel_ = nullptr;
  bool counted_ = false;
  uint32_t starts_ = 0;
  uint8_t kept_ = 0;
  StartNote last_[kStartsKept];   // the newest first
};

}  // namespace lmp

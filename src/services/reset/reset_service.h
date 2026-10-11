// LoopMaxPro - reset button.
// Held for LMP_RESET_HOLD_MS: factory reset. Nothing blocks: the pin is sampled in the tick.
#pragma once
#include "kernel/kernel.h"
#include "services/gpio/gpio_service.h"

namespace lmp {

class ResetService : public Service {
 public:
  // pin < 0 disables the button. The button connects the pin to ground.
  explicit ResetService(int16_t pin = LMP_RESET_PIN, uint32_t holdMs = LMP_RESET_HOLD_MS)
      : pin_(pin), holdMs_(holdMs) {}

  const ServiceInfo& info() const override;
  Status start(Kernel& kernel) override;
  void stop() override;
  void tick(uint32_t nowMs) override;
  void describe(JsonWriter& w) const override;

  bool pressed() const { return pressed_; }

 private:
  static constexpr uint32_t kDebounceMs = 60;

  Kernel* kernel_ = nullptr;
  IGpio* gpio_ = nullptr;
  int16_t pin_;
  uint32_t holdMs_;
  bool raw_ = false;          // last sample
  bool pressed_ = false;      // after debounce
  bool fired_ = false;        // the reset was already requested for this press
  uint32_t rawSinceMs_ = 0;
  uint32_t pressedSinceMs_ = 0;
};

}  // namespace lmp

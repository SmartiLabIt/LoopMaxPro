#include "services/reset/reset_service.h"

namespace lmp {
namespace {
constexpr const char* kOwner = "reset";
}

const ServiceInfo& ResetService::info() const {
  static const ServiceInfo kInfo{ServiceId::Reset, "reset", "\xF0\x9F\x94\x84", Mode::Any,
                                 RestartPolicy::Retry, 20,
                                 DepList(ServiceId::Gpio, ServiceId::Config, ServiceId::Log)};
  return kInfo;
}

Status ResetService::start(Kernel& kernel) {
  kernel_ = &kernel;
  raw_ = pressed_ = fired_ = false;
  if (pin_ < 0) {
    degrade("no reset button on this board");
    return Status::Ok;
  }
  gpio_ = kernel.api<IGpio>();
  if (!gpio_) return Status::Unsupported;
  const Status claimed = gpio_->claim(static_cast<uint8_t>(pin_), hal::PinMode::InputPullUp,
                                      kOwner, "Reset button");
  if (claimed != Status::Ok) {
    fail("reset pin not available");
    return claimed;
  }
  return Status::Ok;
}

void ResetService::stop() {
  if (gpio_ && pin_ >= 0) gpio_->release(static_cast<uint8_t>(pin_), kOwner);
  gpio_ = nullptr;
}

void ResetService::tick(uint32_t nowMs) {
  if (!gpio_ || pin_ < 0) return;
  bool high = true;
  if (gpio_->read(static_cast<uint8_t>(pin_), high) != Status::Ok) return;
  const bool sample = !high;  // active low

  if (sample != raw_) {
    raw_ = sample;
    rawSinceMs_ = nowMs;
  }
  if (raw_ != pressed_ && nowMs - rawSinceMs_ >= kDebounceMs) {
    pressed_ = raw_;
    // The hold time counts from the first contact, not from the end of the debounce.
    pressedSinceMs_ = rawSinceMs_;
    fired_ = false;
  }

  if (pressed_ && !fired_ && nowMs - pressedSinceMs_ >= holdMs_) {
    fired_ = true;
    kernel_->requestRestart("reset button", true);
  }
}

void ResetService::describe(JsonWriter& w) const {
  w.i32("pin", pin_);
  w.u32("holdMs", holdMs_);
  w.boolean("pressed", pressed_);
  if (pressed_ && kernel_) w.u32("heldMs", kernel_->now() - pressedSinceMs_);
}

}  // namespace lmp

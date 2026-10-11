#include "sdk/tools.h"

#include "services/gpio/gpio_service.h"

namespace lmp::sdk {

// ---------------------------------------------------------------- button

Status Button::begin(IGpio& gpio, uint8_t pin, bool activeLow, const char* owner,
                     uint32_t longPressMs) {
  if (!owner || !*owner) return Status::Invalid;
  LMP_TRY(gpio.claim(pin, activeLow ? hal::PinMode::InputPullUp : hal::PinMode::InputPullDown,
                     owner, "button"));
  gpio_ = &gpio;
  owner_ = owner;
  pin_ = pin;
  activeLow_ = activeLow;
  longPressMs_ = longPressMs;
  raw_ = held_ = longTold_ = false;
  sinceMs_ = 0;
  return Status::Ok;
}

void Button::end() {
  if (gpio_) gpio_->release(pin_, owner_);
  gpio_ = nullptr;
}

ButtonEvent Button::poll(uint32_t nowMs) {
  if (!gpio_) return ButtonEvent::None;
  bool high = false;
  if (gpio_->read(pin_, high) != Status::Ok) return ButtonEvent::None;
  const bool down = high != activeLow_;

  if (down != raw_) {
    // The contacts moved: the level has to hold before it counts.
    raw_ = down;
    sinceMs_ = nowMs;
    return ButtonEvent::None;
  }
  if (raw_ != held_ && nowMs - sinceMs_ >= kNoiseMs) {
    held_ = raw_;
    if (held_) {
      longTold_ = false;
      return ButtonEvent::Pressed;
    }
    return longTold_ ? ButtonEvent::LongReleased : ButtonEvent::Released;
  }
  if (held_ && !longTold_ && nowMs - sinceMs_ >= longPressMs_) {
    longTold_ = true;
    return ButtonEvent::LongPress;
  }
  return ButtonEvent::None;
}

// ---------------------------------------------------------------- poller

void Poller::begin(uint32_t everyMs, uint8_t window, float threshold) {
  everyMs_ = everyMs ? everyMs : 1;
  window_ = window < 1 ? 1 : (window > kWindow ? kWindow : window);
  threshold_ = threshold < 0 ? -threshold : threshold;
  count_ = next_ = failures_ = 0;
  average_ = told_ = 0;
  started_ = toldOnce_ = false;
}

bool Poller::due(uint32_t nowMs) {
  if (started_ && static_cast<uint32_t>(nowMs - lastMs_) < everyMs_) return false;
  started_ = true;
  lastMs_ = nowMs;
  return true;
}

bool Poller::add(float sample) {
  const bool wasValid = valid();
  failures_ = 0;
  samples_[next_] = sample;
  next_ = static_cast<uint8_t>((next_ + 1) % window_);
  if (count_ < window_) ++count_;
  float sum = 0;
  for (uint8_t i = 0; i < count_; ++i) sum += samples_[i];
  average_ = sum / count_;

  const float moved = average_ > told_ ? average_ - told_ : told_ - average_;
  // The first good value, and the first after the readings came back, are always news.
  if (!toldOnce_ || !wasValid || moved >= threshold_) {
    toldOnce_ = true;
    told_ = average_;
    return true;
  }
  return false;
}

void Poller::fail() {
  if (failures_ < 255) ++failures_;
}

}  // namespace lmp::sdk

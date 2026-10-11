#include "services/gpio/gpio_service.h"

#include <string.h>

namespace lmp {

const char* pinModeName(hal::PinMode mode) {
  switch (mode) {
    case hal::PinMode::Input:           return "in";
    case hal::PinMode::InputPullUp:     return "in_pullup";
    case hal::PinMode::InputPullDown:   return "in_pulldown";
    case hal::PinMode::Output:          return "out";
    case hal::PinMode::OutputOpenDrain: return "out_opendrain";
    case hal::PinMode::Analog:          return "analog";
  }
  return "?";
}

const ServiceInfo& GpioService::info() const {
  static const ServiceInfo kInfo{ServiceId::Gpio, "gpio", "\xF0\x9F\x93\x8D", Mode::Any,
                                 RestartPolicy::Retry, 0, DepList(ServiceId::Log)};
  return kInfo;
}

Status GpioService::start(Kernel& kernel) {
  hal_ = kernel.board().gpio;
  pwm_ = kernel.board().pwm;
  pixels_ = kernel.board().pixels;
  clock_ = kernel.board().clock;
  return hal_ ? Status::Ok : Status::Unsupported;
}

GpioService::Claim* GpioService::find(uint8_t pin) {
  for (size_t i = 0; i < count_; ++i) {
    if (claims_[i].pin == pin) return &claims_[i];
  }
  return nullptr;
}

const GpioService::Claim* GpioService::find(uint8_t pin) const {
  return const_cast<GpioService*>(this)->find(pin);
}

Status GpioService::claim(uint8_t pin, hal::PinMode mode, const char* owner, const char* label) {
  if (!hal_) return Status::Unsupported;
  if (!owner || !*owner || !hal_->isValid(pin)) return Status::Invalid;
  const bool output = mode == hal::PinMode::Output || mode == hal::PinMode::OutputOpenDrain;
  if (output && !hal_->canOutput(pin)) return Status::Unsupported;

  Claim* c = find(pin);
  if (c && strcmp(c->owner, owner) != 0) return Status::Busy;
  if (!c) {
    if (count_ >= LMP_MAX_PINS) return Status::NoMem;
    c = &claims_[count_];
  }
  LMP_TRY(hal_->setMode(pin, mode));
  if (c == &claims_[count_]) ++count_;
  c->pin = pin;
  c->mode = mode;
  c->owner = owner;
  c->label = label ? label : "";
  return Status::Ok;
}

Status GpioService::release(uint8_t pin, const char* owner) {
  Claim* c = find(pin);
  if (!c) return Status::NotFound;
  if (!owner || strcmp(c->owner, owner) != 0) return Status::Denied;
  if (c->pwmBits && pwm_) pwm_->release(pin);
  if (c->pixels && pixels_) pixels_->end(pin);
  for (Pulse& p : pulses_) {
    if (p.pin != pin) continue;
    if (hal_) hal_->detachInterrupt(pin);
    p.pin = 0xFF;
  }
  for (Edges& e : edges_) {
    if (e.pin != pin) continue;
    if (hal_) hal_->detachInterrupt(pin);
    e.pin = 0xFF;
  }
  if (hal_) hal_->setMode(pin, hal::PinMode::Input);
  *c = claims_[count_ - 1];
  claims_[--count_] = Claim();
  return Status::Ok;
}

Status GpioService::write(uint8_t pin, bool high, const char* owner) {
  if (!hal_) return Status::Unsupported;
  const Claim* c = find(pin);
  if (!c) return Status::NotFound;
  if (!owner || strcmp(c->owner, owner) != 0) return Status::Denied;
  return hal_->write(pin, high ? hal::PinLevel::High : hal::PinLevel::Low);
}

Status GpioService::read(uint8_t pin, bool& high) const {
  if (!hal_) return Status::Unsupported;
  hal::PinLevel level = hal::PinLevel::Low;
  LMP_TRY(hal_->read(pin, level));
  high = level == hal::PinLevel::High;
  return Status::Ok;
}

Status GpioService::pwm(uint8_t pin, uint32_t frequencyHz, uint8_t resolutionBits,
                        const char* owner) {
  if (!pwm_ || !pwm_->supports(pin)) return Status::Unsupported;
  Claim* c = find(pin);
  if (!c) return Status::NotFound;
  if (!owner || strcmp(c->owner, owner) != 0) return Status::Denied;
  if (c->mode != hal::PinMode::Output) return Status::Invalid;
  LMP_TRY(pwm_->setup(pin, frequencyHz, resolutionBits));
  c->pwmBits = resolutionBits;
  c->duty = 0;
  return Status::Ok;
}

Status GpioService::duty(uint8_t pin, uint32_t value, const char* owner) {
  if (!pwm_) return Status::Unsupported;
  Claim* c = find(pin);
  if (!c) return Status::NotFound;
  if (!owner || strcmp(c->owner, owner) != 0) return Status::Denied;
  if (!c->pwmBits) return Status::Invalid;
  LMP_TRY(pwm_->write(pin, value));
  c->duty = value;
  return Status::Ok;
}

Status GpioService::pixels(uint8_t pin, uint16_t count, const char* owner) {
  if (!pixels_ || !pixels_->supports(pin)) return Status::Unsupported;
  Claim* c = find(pin);
  if (!c) return Status::NotFound;
  if (!owner || strcmp(c->owner, owner) != 0) return Status::Denied;
  if (c->mode != hal::PinMode::Output) return Status::Invalid;
  LMP_TRY(pixels_->begin(pin, count));
  c->pixels = count;
  return Status::Ok;
}

Status GpioService::show(uint8_t pin, const uint8_t* grb, size_t len, const char* owner) {
  if (!pixels_) return Status::Unsupported;
  Claim* c = find(pin);
  if (!c) return Status::NotFound;
  if (!owner || strcmp(c->owner, owner) != 0) return Status::Denied;
  if (!c->pixels) return Status::Invalid;
  return pixels_->show(pin, grb, len);
}

// ---------------------------------------------------------------- pulses

Status GpioService::pulseOut(uint8_t pin, uint32_t us, const char* owner) {
  if (!hal_ || !clock_) return Status::Unsupported;
  if (us == 0 || us > 1000) return Status::Invalid;
  const Claim* c = find(pin);
  if (!c) return Status::NotFound;
  if (!owner || strcmp(c->owner, owner) != 0) return Status::Denied;
  if (c->mode != hal::PinMode::Output) return Status::Invalid;
  LMP_TRY(hal_->write(pin, hal::PinLevel::High));
  // Too short for the scheduler: the time is watched here. The count ends the wait where the
  // clock does not move, as on the PC of the tests.
  const uint64_t start = clock_->micros();
  for (uint32_t guard = 0; clock_->micros() - start < us && guard < 200000; ++guard) {
  }
  return hal_->write(pin, hal::PinLevel::Low);
}

Status GpioService::pulseWatch(uint8_t pin, const char* owner) {
  if (!hal_ || !clock_) return Status::Unsupported;
  const Claim* c = find(pin);
  if (!c) return Status::NotFound;
  if (!owner || strcmp(c->owner, owner) != 0) return Status::Denied;
  const bool input = c->mode == hal::PinMode::Input || c->mode == hal::PinMode::InputPullUp ||
                     c->mode == hal::PinMode::InputPullDown;
  if (!input) return Status::Invalid;
  Pulse* slot = nullptr;
  for (Pulse& p : pulses_) {
    if (p.pin == pin) return Status::Ok;   // already watched
    if (p.pin == 0xFF && !slot) slot = &p;
  }
  if (!slot) return Status::NoMem;
  slot->rising = slot->ready = false;
  slot->pin = pin;
  const Status s = hal_->attachInterrupt(pin, hal::Edge::Change,
                                         [this](uint8_t p) { onPulseEdge(p); });
  if (s != Status::Ok) slot->pin = 0xFF;
  return s;
}

bool GpioService::pulseTake(uint8_t pin, uint32_t& us) {
  for (Pulse& p : pulses_) {
    if (p.pin != pin || !p.ready) continue;
    us = p.widthUs;
    p.ready = false;
    return true;
  }
  return false;
}

// Interrupt context: the level, the time, nothing else.
void GpioService::onPulseEdge(uint8_t pin) {
  for (Pulse& p : pulses_) {
    if (p.pin != pin) continue;
    hal::PinLevel level = hal::PinLevel::Low;
    hal_->read(pin, level);
    const uint64_t now = clock_->micros();
    if (level == hal::PinLevel::High) {
      p.riseUs = now;
      p.rising = true;
    } else if (p.rising) {
      p.widthUs = static_cast<uint32_t>(now - p.riseUs);
      p.rising = false;
      p.ready = true;
    }
    return;
  }
}

// ---------------------------------------------------------------- edges

Status GpioService::edgeWatch(uint8_t pin, hal::Edge edge, const char* owner, uint32_t holdUs) {
  if (!hal_ || (holdUs && !clock_)) return Status::Unsupported;
  if (edge == hal::Edge::Change) return Status::Invalid;
  const Claim* c = find(pin);
  if (!c) return Status::NotFound;
  if (!owner || strcmp(c->owner, owner) != 0) return Status::Denied;
  const bool input = c->mode == hal::PinMode::Input || c->mode == hal::PinMode::InputPullUp ||
                     c->mode == hal::PinMode::InputPullDown;
  if (!input) return Status::Invalid;
  Edges* slot = nullptr;
  for (Edges& e : edges_) {
    if (e.pin == pin) return Status::Ok;   // already watched
    if (e.pin == 0xFF && !slot) slot = &e;
  }
  if (!slot) return Status::NoMem;
  slot->count = 0;
  slot->held = false;
  slot->holdUs = holdUs;
  slot->low = edge == hal::Edge::Falling;
  slot->pin = pin;
  // With a hold both edges are watched: the second tells how long the level lasted.
  const Status s = hal_->attachInterrupt(pin, holdUs ? hal::Edge::Change : edge,
                                         [this](uint8_t p) { onEdge(p); });
  if (s != Status::Ok) slot->pin = 0xFF;
  return s;
}

uint32_t GpioService::edgeTake(uint8_t pin) {
  for (Edges& e : edges_) {
    if (e.pin != pin) continue;
    // A level that began long enough ago and is still there counts now, once.
    if (e.holdUs && e.held && clock_->micros() - e.sinceUs >= e.holdUs) {
      e.held = false;
      e.count = e.count + 1;
    }
    // The interrupt may add one between the two lines: it stays for the next call.
    const uint32_t seen = e.count;
    e.count = e.count - seen;
    return seen;
  }
  return 0;
}

// Interrupt context.
void GpioService::onEdge(uint8_t pin) {
  for (Edges& e : edges_) {
    if (e.pin != pin) continue;
    if (!e.holdUs) {
      e.count = e.count + 1;
      return;
    }
    hal::PinLevel level = hal::PinLevel::Low;
    hal_->read(pin, level);
    const uint64_t now = clock_->micros();
    if ((level == hal::PinLevel::Low) == e.low) {
      e.sinceUs = now;
      e.held = true;
    } else if (e.held) {
      // The level ended: it counts when it lasted, and was not counted while it was there.
      if (now - e.sinceUs >= e.holdUs) e.count = e.count + 1;
      e.held = false;
    }
    return;
  }
}

const char* GpioService::ownerOf(uint8_t pin) const {
  const Claim* c = find(pin);
  return c ? c->owner : nullptr;
}

void GpioService::describe(JsonWriter& w) const {
  w.u32("used", static_cast<uint32_t>(count_));
  w.u32("capacity", LMP_MAX_PINS);
  w.beginArray("pins");
  for (size_t i = 0; i < count_; ++i) {
    const Claim& c = claims_[i];
    bool high = false;
    read(c.pin, high);
    w.beginObject();
    w.u32("pin", c.pin);
    w.str("owner", c.owner);
    w.str("label", c.label);
    w.str("mode", c.pixels ? "pixels" : c.pwmBits ? "pwm" : pinModeName(c.mode));
    if (c.pixels) w.u32("count", c.pixels);
    w.u32("level", high ? 1 : 0);
    // PWM: the duty in thousandths of the full, whatever the resolution.
    if (c.pwmBits) w.u32("duty", static_cast<uint32_t>(c.duty * 1000ull / ((1u << c.pwmBits) - 1)));
    w.endObject();
  }
  w.endArray();
}

}  // namespace lmp

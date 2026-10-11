// LoopMaxPro - pin service.
// Every pin in use has an owner: two parts of the system cannot drive the same pin.
#pragma once
#include "kernel/kernel.h"

namespace lmp {

#ifndef LMP_MAX_PINS
#define LMP_MAX_PINS 24
#endif

class IGpio {
 public:
  static constexpr ServiceId kId = ServiceId::Gpio;
  virtual ~IGpio() = default;

  // Reserves a pin and sets its mode. owner and label must be texts that stay valid.
  // Busy when the pin belongs to someone else.
  virtual Status claim(uint8_t pin, hal::PinMode mode, const char* owner,
                       const char* label = "") = 0;
  virtual Status release(uint8_t pin, const char* owner) = 0;
  // Only the owner can write.
  virtual Status write(uint8_t pin, bool high, const char* owner) = 0;
  virtual Status read(uint8_t pin, bool& high) const = 0;
  // Owner of a pin, or null when it is free.
  virtual const char* ownerOf(uint8_t pin) const = 0;

  // A pin claimed as an output, driven with a duty instead of a level: frequency and
  // resolution once, then the duty, from 0 to 2^bits - 1. Unsupported where the board has no
  // PWM, or not on that pin. Only the owner. The release of the pin ends the PWM.
  virtual Status pwm(uint8_t pin, uint32_t frequencyHz, uint8_t resolutionBits,
                     const char* owner) = 0;
  virtual Status duty(uint8_t pin, uint32_t value, const char* owner) = 0;

  // A pin claimed as an output that drives a line of addressable LEDs (WS2812, "NeoPixel"):
  // count pixels once, then the frames, 3 bytes a pixel, green, red, blue. Unsupported where
  // the board has no driver. Only the owner. The release of the pin ends the line.
  virtual Status pixels(uint8_t pin, uint16_t count, const char* owner) = 0;
  virtual Status show(uint8_t pin, const uint8_t* grb, size_t len, const char* owner) = 0;

  // Pulses, for the sensors that answer with the length of one (ultrasonic distance: HC-SR04,
  // JSN-SR04T). Nothing waits: the pulse is timed by the interrupts of the pin.
  //   pulseOut    a pulse on a pin claimed as an output: high for us microseconds (1000 at
  //               most), then low. It returns when the pulse has ended.
  //   pulseWatch  begins to time the pulses on a pin claimed as an input: from when it goes
  //               high to when it comes back low. NoMem when the board watches as many as it can.
  //   pulseTake   true once for each pulse seen since the last call, with its length. A pulse
  //               that began and has not ended is not there yet.
  // Only the owner. The release of the pin ends the watch.
  virtual Status pulseOut(uint8_t pin, uint32_t us, const char* owner) = 0;
  virtual Status pulseWatch(uint8_t pin, const char* owner) = 0;
  virtual bool pulseTake(uint8_t pin, uint32_t& us) = 0;

  // Edges, for what happens in a moment and must not be missed between two rounds of the
  // system: a reed a magnet passes by, a counter. The interrupt of the pin counts them.
  //   edgeWatch  begins to count the edges of that kind (Rising or Falling) on a pin claimed
  //              as an input. holdUs: an edge counts only when the level it brings lasts at
  //              least that long. A long wire picks up spikes of a few microseconds, which
  //              are edges too: with a hold they are not counted. 0: every edge.
  //   edgeTake   how many since the last call.
  // Only the owner. The release of the pin ends the watch.
  virtual Status edgeWatch(uint8_t pin, hal::Edge edge, const char* owner,
                           uint32_t holdUs = 0) = 0;
  virtual uint32_t edgeTake(uint8_t pin) = 0;
};

class GpioService : public Service, public IGpio {
 public:
  const ServiceInfo& info() const override;
  Status start(Kernel& kernel) override;
  void describe(JsonWriter& w) const override;
  void* api() override { return static_cast<IGpio*>(this); }

  Status claim(uint8_t pin, hal::PinMode mode, const char* owner, const char* label) override;
  Status release(uint8_t pin, const char* owner) override;
  Status write(uint8_t pin, bool high, const char* owner) override;
  Status read(uint8_t pin, bool& high) const override;
  const char* ownerOf(uint8_t pin) const override;
  Status pwm(uint8_t pin, uint32_t frequencyHz, uint8_t resolutionBits, const char* owner) override;
  Status duty(uint8_t pin, uint32_t value, const char* owner) override;
  Status pixels(uint8_t pin, uint16_t count, const char* owner) override;
  Status show(uint8_t pin, const uint8_t* grb, size_t len, const char* owner) override;
  Status pulseOut(uint8_t pin, uint32_t us, const char* owner) override;
  Status pulseWatch(uint8_t pin, const char* owner) override;
  bool pulseTake(uint8_t pin, uint32_t& us) override;
  Status edgeWatch(uint8_t pin, hal::Edge edge, const char* owner, uint32_t holdUs) override;
  uint32_t edgeTake(uint8_t pin) override;

 private:
  struct Claim {
    const char* owner = nullptr;
    const char* label = "";
    hal::PinMode mode = hal::PinMode::Input;
    uint8_t pin = 0;
    uint8_t pwmBits = 0;   // 0: a level, not a PWM
    uint32_t duty = 0;
    uint16_t pixels = 0;   // 0: not a line of LEDs
  };

  // A pin whose pulses are timed. Written by the interrupt, read by the application task.
  struct Pulse {
    volatile uint64_t riseUs = 0;
    volatile uint32_t widthUs = 0;
    volatile bool rising = false;   // it went high and has not come back
    volatile bool ready = false;
    uint8_t pin = 0xFF;             // 0xFF: free
  };
  static constexpr size_t kPulses = 2;
  struct Edges {
    volatile uint32_t count = 0;
    volatile uint64_t sinceUs = 0;  // when the level of the edge began
    volatile bool held = false;     // the level of the edge is there, not counted yet
    uint32_t holdUs = 0;
    bool low = true;                // the level the edge brings
    uint8_t pin = 0xFF;             // 0xFF: free
  };
  static constexpr size_t kEdges = 4;

  Claim* find(uint8_t pin);
  const Claim* find(uint8_t pin) const;
  void onPulseEdge(uint8_t pin);
  void onEdge(uint8_t pin);

  hal::IClock* clock_ = nullptr;
  Pulse pulses_[kPulses];
  Edges edges_[kEdges];

  hal::IGpio* hal_ = nullptr;
  hal::IPwm* pwm_ = nullptr;
  hal::IPixels* pixels_ = nullptr;
  Claim claims_[LMP_MAX_PINS];
  size_t count_ = 0;
};

const char* pinModeName(hal::PinMode mode);

}  // namespace lmp

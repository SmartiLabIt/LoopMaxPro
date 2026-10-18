// LoopMaxPro SDK - a bank of pins: relays, lamps, contacts.
// Channels are counted from 0 and never change, whatever pin of the chip is behind: what is
// kept on file talks about channels, so a new map of the pins keeps names and schedules.
//
//   lmp::sdk::PinBank<2> pins_;
//   static const uint8_t map[2] = {22, 23};
//   LMP_TRY(pins_.declare(c, map, lmp::hal::PinMode::Output, "Line %u"));
//   LMP_TRY(use(pins_));
//
// Channels without a pin, for what the module switches by itself, as a light strip on PWM:
// the same names, restore, commands and schedules, and the module is told at every change.
//
//   LMP_TRY(power_.declareVirtual(c, "Strip", [this](uint8_t, bool on) { onPower(on); }));
#pragma once
#include "base/fixed_string.h"
#include "hal/api/types.h"
#include "sdk/module.h"

namespace lmp::sdk {

// Who decides a channel besides the hand: the schedules. While a schedule holds a channel,
// the commands leave it alone.
class ChannelGuard {
 public:
  virtual ~ChannelGuard() = default;
  // A schedule is in its span now: the channel is what the schedule wants.
  virtual bool held(uint8_t channel) const = 0;
  // The channel has schedules, in their span or not.
  virtual bool planned(uint8_t channel) const = 0;
};

struct PinEntry {
  uint8_t pin = 0;        // pin of the chip
  bool on = false;        // what the channel is: on or off
  bool inverted = false;  // the pin is low when the channel is on
  FixedString<24> name;
};

// A channel without a pin changed: on is what it is now.
using SwitchFn = Delegate<void(uint8_t channel, bool on)>;

class PinBankBase : public Component {
 public:
  // Takes the pins for the module. nameFormat gives the names the channels are born with:
  // "Line %u" makes "Line 1", "Line 2". Busy when a pin belongs to someone else.
  Status declare(ModuleContext& c, const uint8_t* map, hal::PinMode mode,
                 const char* nameFormat);
  // Channels without a pin: onSwitch does what on and off mean, at every change and when the
  // file brings a level back. No "invert", no pin in the document. names, when given, has
  // the name each channel is born with, in the place of nameFormat.
  Status declareVirtual(ModuleContext& c, const char* nameFormat, SwitchFn onSwitch,
                        const char* const* names = nullptr);
  bool isVirtual() const { return virtual_; }
  void release();

  uint8_t size() const { return count_; }
  bool output() const { return output_; }
  bool on(uint8_t channel) const { return channel < count_ && entries_[channel].on; }
  const PinEntry* entry(uint8_t channel) const {
    return channel < count_ ? &entries_[channel] : nullptr;
  }

  // Denied for a bank of inputs. These are for the code of the module and for the schedules:
  // they do not ask the guard. The commands do.
  Status set(uint8_t channel, bool on);
  Status toggle(uint8_t channel);
  Status setAll(bool on);
  // Every channel on or off, except those a schedule holds now: the command "all".
  Status setFree(bool on);
  // Every channel into the opposite of what it is, except those a schedule holds now.
  Status flipAll();
  // True when a schedule holds the channel now: the commands leave it alone.
  bool held(uint8_t channel) const { return guard_ && guard_->held(channel); }
  // What the channels are for a home system, in the words it knows (sdk/home.h): "outlet" for
  // the sockets of a power strip. Without it: plain switches, plain contacts.
  void kind(const char* k) { kind_ = k; }
  void guard(const ChannelGuard* g) { guard_ = g; }
  Status rename(uint8_t channel, const char* name);
  Status invert(uint8_t channel, bool inverted);
  // True: at the next start the channels go back to what they were.
  void setRestore(bool restore);
  bool restore() const { return restore_; }

  void commands(CommandSet& set) override;
  void describe(JsonWriter& w) const override;
  void save(JsonWriter& w) const override;
  void load(const JsonValue& doc) override;
  // Inputs are read here.
  void tick(uint32_t nowMs) override;
  // For a home system the channels are switches, "ch0", "ch1"; contacts to read for a bank
  // of inputs. Not the channels without a pin: who switches them tells what they are.
  void things(ThingSink& sink) const override;
  Status thingSet(const char* id, const ThingAsk& ask) override;
  void stop() override { release(); }

 protected:
  PinBankBase(PinEntry* entries, uint8_t count) : entries_(entries), count_(count) {}

 private:
  Status drive(uint8_t channel);

  PinEntry* entries_;
  uint8_t count_;
  const ChannelGuard* guard_ = nullptr;
  bool declared_ = false;
  bool virtual_ = false;
  SwitchFn onSwitch_;
  bool output_ = true;
  bool restore_ = true;
  const char* owner_ = "";
  const char* kind_ = nullptr;
};

template <size_t N>
class PinBank : public PinBankBase {
 public:
  PinBank() : PinBankBase(storage_, static_cast<uint8_t>(N)) {}

 private:
  PinEntry storage_[N];
};

}  // namespace lmp::sdk

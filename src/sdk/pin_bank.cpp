#include "sdk/pin_bank.h"

#include <stdio.h>
#include <string.h>

#include "services/gpio/gpio_service.h"

namespace lmp::sdk {
namespace {

constexpr ArgSpec kSetArgs[] = {
    Arg::u8("channel", 0, 63),
    Arg::boolean("value"),
};
constexpr ArgSpec kChannelArgs[] = {
    Arg::u8("channel", 0, 63),
};
constexpr ArgSpec kValueArgs[] = {
    Arg::boolean("value"),
};
constexpr ArgSpec kNameArgs[] = {
    Arg::u8("channel", 0, 63),
    Arg::str("name", 1, 24),
};

}  // namespace

Status PinBankBase::declare(ModuleContext& c, const uint8_t* map, hal::PinMode mode,
                            const char* nameFormat) {
  if (!c.gpio || !c.manifest) return Status::Unsupported;
  bind(c);
  if (!map || declared_) return Status::Invalid;
  owner_ = c.manifest->name;
  output_ = mode == hal::PinMode::Output || mode == hal::PinMode::OutputOpenDrain;

  for (uint8_t i = 0; i < count_; ++i) {
    PinEntry& e = entries_[i];
    e.pin = map[i];
    e.on = false;
    e.inverted = false;
    // printf with a format that is not a literal: the names come from the code of the module.
    char name[25];
    snprintf(name, sizeof(name), nameFormat ? nameFormat : "%u", static_cast<unsigned>(i + 1));
    e.name.assign(name);
    const Status claimed = c.gpio->claim(e.pin, mode, owner_, "");
    if (claimed != Status::Ok) {
      for (uint8_t k = 0; k < i; ++k) c.gpio->release(entries_[k].pin, owner_);
      return claimed;
    }
  }
  declared_ = true;
  // Every output starts off, whatever the pin was doing.
  if (output_) {
    for (uint8_t i = 0; i < count_; ++i) drive(i);
  }
  return Status::Ok;
}

Status PinBankBase::declareVirtual(ModuleContext& c, const char* nameFormat, SwitchFn onSwitch,
                                   const char* const* names) {
  if (!c.manifest) return Status::Unsupported;
  bind(c);
  if (declared_) return Status::Invalid;
  owner_ = c.manifest->name;
  output_ = true;
  virtual_ = true;
  onSwitch_ = onSwitch;
  for (uint8_t i = 0; i < count_; ++i) {
    PinEntry& e = entries_[i];
    e.pin = 0;
    e.on = false;
    e.inverted = false;
    char name[25];
    if (names && names[i]) {
      snprintf(name, sizeof(name), "%s", names[i]);
    } else {
      snprintf(name, sizeof(name), nameFormat ? nameFormat : "%u", static_cast<unsigned>(i + 1));
    }
    e.name.assign(name);
  }
  declared_ = true;
  return Status::Ok;
}

void PinBankBase::release() {
  if (virtual_) {
    declared_ = false;
    return;
  }
  if (!declared_ || !ctx() || !ctx()->gpio) return;
  for (uint8_t i = 0; i < count_; ++i) ctx()->gpio->release(entries_[i].pin, owner_);
  declared_ = false;
}

Status PinBankBase::drive(uint8_t channel) {
  if (virtual_) {
    if (!declared_) return Status::Denied;
    if (onSwitch_) onSwitch_(channel, entries_[channel].on);
    return Status::Ok;
  }
  IGpio* gpio = ctx() ? ctx()->gpio : nullptr;
  if (!gpio || !declared_) return Status::Denied;
  const PinEntry& e = entries_[channel];
  return gpio->write(e.pin, e.on != e.inverted, owner_);
}

void PinBankBase::things(ThingSink& sink) const {
  if (!declared_ || virtual_) return;
  char id[8];
  for (uint8_t i = 0; i < count_; ++i) {
    snprintf(id, sizeof(id), "ch%u", static_cast<unsigned>(i));
    if (output_) {
      sink.toggle(id, entries_[i].name.c_str(), entries_[i].on, kind_);
    } else {
      sink.flag(id, entries_[i].name.c_str(), entries_[i].on, kind_);
    }
  }
  // The commands "all" and "flip", for who presses a button instead of writing a command: the
  // commands of every day are buttons too, also "all" with its two values (F30 of the server).
  if (output_ && count_ > 1) {
    sink.button("all_on", {"All on", "Tutti accesi"});
    sink.button("all_off", {"All off", "Tutti spenti"});
    sink.button("flip", {"Invert", "Inverti"});
  }
}

Status PinBankBase::thingSet(const char* id, const ThingAsk& ask) {
  if (declared_ && !virtual_ && output_ && count_ > 1) {
    if (strcmp(id, "flip") == 0) return ask.press ? flipAll() : Status::Invalid;
    if (strcmp(id, "all_on") == 0) return ask.press ? setFree(true) : Status::Invalid;
    if (strcmp(id, "all_off") == 0) return ask.press ? setFree(false) : Status::Invalid;
  }
  if (!declared_ || virtual_ || id[0] != 'c' || id[1] != 'h' || id[2] < '0' || id[2] > '9') {
    return Status::NotFound;
  }
  unsigned channel = 0;
  for (const char* c = id + 2; *c; ++c) {
    if (*c < '0' || *c > '9' || channel > 99) return Status::NotFound;
    channel = channel * 10 + static_cast<unsigned>(*c - '0');
  }
  if (channel >= count_) return Status::NotFound;
  if (!output_ || !ask.hasOn) return Status::Invalid;
  // As a command by hand: a channel a schedule holds is left to the schedule.
  if (held(static_cast<uint8_t>(channel))) return Status::Denied;
  return set(static_cast<uint8_t>(channel), ask.on);
}

Status PinBankBase::set(uint8_t channel, bool on) {
  if (channel >= count_) return Status::Invalid;
  if (!output_) return Status::Denied;
  PinEntry& e = entries_[channel];
  const bool was = e.on;
  e.on = on;
  const Status s = drive(channel);
  if (s != Status::Ok) {
    e.on = was;
    return s;
  }
  if (was != on) {
    // The level is kept on file only when it has to come back at the next start.
    if (restore_) {
      unsaved();
    } else {
      changed();
    }
  }
  return Status::Ok;
}

Status PinBankBase::toggle(uint8_t channel) {
  if (channel >= count_) return Status::Invalid;
  return set(channel, !entries_[channel].on);
}

Status PinBankBase::setAll(bool on) {
  Status result = Status::Ok;
  for (uint8_t i = 0; i < count_; ++i) {
    const Status s = set(i, on);
    if (s != Status::Ok) result = s;
  }
  return result;
}

Status PinBankBase::setFree(bool on) {
  Status result = Status::Ok;
  for (uint8_t i = 0; i < count_; ++i) {
    if (held(i)) continue;
    const Status s = set(i, on);
    if (s != Status::Ok) result = s;
  }
  return result;
}

Status PinBankBase::flipAll() {
  Status result = Status::Ok;
  for (uint8_t i = 0; i < count_; ++i) {
    // As "all": only a channel a schedule holds now is left. Out of its span the hand decides.
    if (held(i)) continue;
    const Status s = toggle(i);
    if (s != Status::Ok) result = s;
  }
  return result;
}

Status PinBankBase::rename(uint8_t channel, const char* name) {
  if (channel >= count_ || !name || !*name || strlen(name) > 24) return Status::Invalid;
  entries_[channel].name.assign(name);
  unsaved();
  return Status::Ok;
}

Status PinBankBase::invert(uint8_t channel, bool inverted) {
  if (channel >= count_) return Status::Invalid;
  if (!output_ || virtual_) return Status::Denied;
  entries_[channel].inverted = inverted;
  unsaved();
  // The channel stays what it was: it is the pin that changes.
  return drive(channel);
}

void PinBankBase::setRestore(bool restore) {
  if (restore_ == restore) return;
  restore_ = restore;
  unsaved();
}

void PinBankBase::tick(uint32_t) {
  if (output_ || !declared_ || !ctx() || !ctx()->gpio) return;
  for (uint8_t i = 0; i < count_; ++i) {
    PinEntry& e = entries_[i];
    bool high = false;
    if (ctx()->gpio->read(e.pin, high) != Status::Ok) continue;
    const bool on = high != e.inverted;
    if (on != e.on) {
      e.on = on;
      changed();
    }
  }
}

// ---------------------------------------------------------------- documents and file

void PinBankBase::describe(JsonWriter& w) const {
  w.beginArray("pins");
  for (uint8_t i = 0; i < count_; ++i) {
    const PinEntry& e = entries_[i];
    w.beginObject();
    w.u32("i", i);
    w.str("name", e.name.c_str());
    w.u32("level", e.on ? 1 : 0);
    w.boolean("inverted", e.inverted);
    if (!virtual_) w.u32("gpio", e.pin);
    if (held(i)) w.boolean("locked", true);
    w.endObject();
  }
  w.endArray();
  w.boolean("output", output_);
  if (output_) w.boolean("restore", restore_);
}

void PinBankBase::save(JsonWriter& w) const {
  w.boolean("restore", restore_);
  w.beginArray("pins");
  for (uint8_t i = 0; i < count_; ++i) {
    const PinEntry& e = entries_[i];
    w.beginObject();
    w.u32("i", i);
    w.str("name", e.name.c_str());
    w.boolean("inverted", e.inverted);
    // Without restore the levels are not kept: every start finds the channels off.
    if (output_ && restore_) w.u32("level", e.on ? 1 : 0);
    w.endObject();
  }
  w.endArray();
}

void PinBankBase::load(const JsonValue& doc) {
  bool restore = restore_;
  if (doc.get("restore").getBool(restore)) restore_ = restore;
  const JsonValue pins = doc.get("pins");
  if (!pins.isArray()) return;
  for (const JsonMember m : pins) {
    int64_t index = -1;
    if (!m.value.get("i").getInt(index) || index < 0 || index >= count_) continue;
    PinEntry& e = entries_[index];
    char name[25];
    if (m.value.get("name").getString(name, sizeof(name)) && name[0]) e.name.assign(name);
    bool inverted = false;
    if (m.value.get("inverted").getBool(inverted)) e.inverted = inverted;
    int64_t level = 0;
    if (output_ && restore_ && m.value.get("level").getInt(level)) e.on = level != 0;
    if (output_ && declared_) drive(static_cast<uint8_t>(index));
  }
}

void PinBankBase::commands(CommandSet& set) {
  set.add("name", LMP_HELP("Gives a name to a channel"), kNameArgs,
          [this](const Args& a, CmdContext& ctx) -> Status {
            const Status s = rename(a.u8("channel"), a.str("name"));
            if (s == Status::Invalid) ctx.errorField = a.u8("channel") >= size() ? "channel" : "name";
            return s;
          });
  if (!output_) return;

  // By hand: a channel held by a schedule is Denied, the schedule wins in its span.
  set.add("set", LMP_HELP("Turns a channel on or off"), kSetArgs,
          [this](const Args& a, CmdContext& ctx) -> Status {
            if (held(a.u8("channel"))) {
              ctx.errorField = "channel";
              return Status::Denied;
            }
            const Status s = this->set(a.u8("channel"), a.boolean("value"));
            if (s == Status::Invalid) ctx.errorField = "channel";
            return s;
          },
          kCmdEveryday);
  set.add("toggle", LMP_HELP("Turns a channel into the opposite of what it is"), kChannelArgs,
          [this](const Args& a, CmdContext& ctx) -> Status {
            if (held(a.u8("channel"))) {
              ctx.errorField = "channel";
              return Status::Denied;
            }
            const Status s = toggle(a.u8("channel"));
            if (s == Status::Invalid) ctx.errorField = "channel";
            return s;
          },
          kCmdEveryday);
  set.add("all", LMP_HELP("Turns every channel on or off, except those a schedule holds"),
          kValueArgs, [this](const Args& a, CmdContext&) -> Status { return setFree(a.boolean("value")); },
          kCmdEveryday);
  if (count_ > 1) {
    set.add("flip", LMP_HELP("Every channel into the opposite, except those a schedule holds"),
            [this](const Args&, CmdContext&) -> Status { return flipAll(); }, kCmdEveryday);
  }
  if (!virtual_) {
    set.add("invert", LMP_HELP("A channel that is on keeps its pin low"), kSetArgs,
            [this](const Args& a, CmdContext& ctx) -> Status {
              const Status s = invert(a.u8("channel"), a.boolean("value"));
              if (s == Status::Invalid) ctx.errorField = "channel";
              return s;
            });
  }
  set.add("restore", LMP_HELP("At the next start the channels go back to what they were, or not"),
          kValueArgs, [this](const Args& a, CmdContext&) -> Status {
            setRestore(a.boolean("value"));
            return Status::Ok;
          });
}

}  // namespace lmp::sdk

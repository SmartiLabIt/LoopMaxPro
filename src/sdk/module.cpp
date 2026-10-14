#include "sdk/module.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace lmp::sdk {

namespace {

// Looks for one thing among those of a module, and keeps what it says of itself: enough to
// check a value somebody wants to set.
class ThingFind final : public ThingSink {
 public:
  ThingFind(const char* id, const char* text) : id_(id), text_(text) {}

  void part(const char* id, const char*) override {
    snprintf(part_, sizeof(part_), "%s", id ? id : "");
  }
  void settable(double min, double max, double step) override {
    settable_ = true;
    nextMin_ = min;
    nextMax_ = max;
    nextStep_ = step;
  }
  void range(double, double) override {}
  void toggle(const char* id, ThingName, bool, const char*) override { thing(id, Kind::Other); }
  void light(const char* id, ThingName, bool, uint8_t, uint8_t, uint8_t, uint8_t, bool) override {
    thing(id, Kind::Other);
  }
  void number(const char* id, ThingName, double, const char*, const char*, uint8_t,
              bool) override {
    thing(id, Kind::Number);
  }
  void flag(const char* id, ThingName, bool, const char*) override { thing(id, Kind::Other); }
  void text(const char* id, ThingName, const char*) override { thing(id, Kind::Text); }
  void button(const char* id, ThingName) override { thing(id, Kind::Other); }
  void choice(const char* id, ThingName, const char*, const ThingOption* options,
              uint8_t count) override {
    if (!thing(id, Kind::Choice) || !text_) return;
    for (uint8_t i = 0; i < count; ++i) {
      if (strcmp(options[i].id, text_) == 0) among_ = true;
    }
  }

  bool found() const { return kind_ != Kind::None; }

  // Brings what is asked to what the thing takes. False when it does not take it.
  bool check(ThingAsk& ask) const {
    if (kind_ == Kind::Other) {
      // A switch, a light, a button: a value to set is not for them.
      ask.hasNumber = ask.hasText = false;
      return ask.hasOn || ask.press || ask.hasBrightness || ask.hasColour;
    }
    if (!canSet_) return false;
    if (kind_ == Kind::Number) {
      if (!ask.hasNumber || !isfinite(ask.number)) return false;
      ask.hasText = false;
      const double slack = step_ > 0 ? step_ * 1e-6 : 1e-9;
      if (ask.number < min_ - slack || ask.number > max_ + slack) return false;
      double n = ask.number;
      if (step_ > 0) n = min_ + floor((n - min_) / step_ + 0.5) * step_;
      ask.number = n < min_ ? min_ : (n > max_ ? max_ : n);
      return true;
    }
    if (!ask.hasText || !ask.text) return false;
    ask.hasNumber = false;
    if (kind_ == Kind::Choice) return among_;
    const size_t length = strlen(ask.text);
    const double longest = max_ > 0 && max_ < kThingTextMax ? max_ : kThingTextMax;
    return length >= min_ && length <= longest;
  }

 private:
  enum class Kind : uint8_t { None, Other, Number, Text, Choice };

  // True when this is the thing looked for. What was said for the next thing is taken here.
  bool thing(const char* id, Kind kind) {
    const bool canSet = settable_;
    settable_ = false;
    if (found()) return false;
    const size_t partLength = strlen(part_);
    if (partLength) {
      if (strncmp(id_, part_, partLength) != 0 || id_[partLength] != '.' ||
          strcmp(id_ + partLength + 1, id) != 0) {
        return false;
      }
    } else if (strcmp(id_, id) != 0) {
      return false;
    }
    kind_ = kind;
    canSet_ = canSet;
    if (canSet) {
      min_ = nextMin_;
      max_ = nextMax_;
      step_ = nextStep_;
    }
    return true;
  }

  const char* id_;
  const char* text_;
  char part_[24] = "";
  Kind kind_ = Kind::None;
  bool settable_ = false;
  bool canSet_ = false;
  bool among_ = false;
  double nextMin_ = 0, nextMax_ = 0, nextStep_ = 0;
  double min_ = 0, max_ = 0, step_ = 0;
};

}  // namespace

const char* moduleKindName(ModuleKind kind) {
  switch (kind) {
    case ModuleKind::Hardware: return "hardware";
    case ModuleKind::Bridge:   return "bridge";
    case ModuleKind::Online:   return "online";
    case ModuleKind::Ui:       return "ui";
    case ModuleKind::Headless: return "headless";
  }
  return "?";
}

const char* moduleStateName(ModuleState state) {
  switch (state) {
    case ModuleState::Stopped:  return "Stopped";
    case ModuleState::Init:     return "Init";
    case ModuleState::Ready:    return "Ready";
    case ModuleState::Degraded: return "Degraded";
    case ModuleState::Error:    return "Error";
  }
  return "?";
}

// ---------------------------------------------------------------- component

bool Component::takeChanged() {
  const bool was = changed_;
  changed_ = false;
  return was;
}

bool Component::takeUnsaved() {
  const bool was = unsaved_;
  unsaved_ = false;
  return was;
}

// ---------------------------------------------------------------- module

Status Module::use(Component& part) {
  if (!ctx_ || !ctx_->commands) return Status::Denied;
  for (uint8_t i = 0; i < partCount_; ++i) {
    if (parts_[i] == &part) return Status::Ok;
  }
  if (partCount_ >= kParts) return Status::NoMem;
  parts_[partCount_++] = &part;
  part.ctx_ = ctx_;
  part.commands(*ctx_->commands);
  return ctx_->commands->ok() ? Status::Ok : Status::NoMem;
}

void Module::setState(ModuleState state, const char* reason) {
  if (state != state_) changed_ = true;
  state_ = state;
  reason_ = reason ? reason : "";
}

void Module::describeAll(JsonWriter& w) const {
  for (uint8_t i = 0; i < partCount_; ++i) parts_[i]->describe(w);
  describe(w);
}

void Module::saveAll(JsonWriter& w) const {
  for (uint8_t i = 0; i < partCount_; ++i) parts_[i]->save(w);
  save(w);
}

void Module::loadAll(const JsonValue& doc) {
  for (uint8_t i = 0; i < partCount_; ++i) parts_[i]->load(doc);
  load(doc);
}

void Module::tickAll(uint32_t nowMs) {
  for (uint8_t i = 0; i < partCount_; ++i) parts_[i]->tick(nowMs);
  onTick(nowMs);
}

void Module::eventAll(const Event& event) {
  for (uint8_t i = 0; i < partCount_; ++i) parts_[i]->onEvent(event);
  onEvent(event);
}

void Module::thingsAll(ThingSink& sink) const {
  for (uint8_t i = 0; i < partCount_; ++i) parts_[i]->things(sink);
  things(sink);
}

Status Module::thingSetAll(const char* id, const ThingAsk& asked) {
  if (!id) return Status::Invalid;
  ThingAsk ask = asked;
  if (ask.hasNumber || ask.hasText) {
    // A value to set is checked here, for every module, against what the thing says of itself.
    ThingFind find(id, ask.hasText ? ask.text : nullptr);
    thingsAll(find);
    if (find.found()) {
      if (!find.check(ask)) return Status::Invalid;
    } else {
      // Not among the things the module tells now. A value to set has nowhere to go; what is
      // turned or pressed is left to the module, which knows why it tells nothing.
      ask.hasNumber = ask.hasText = false;
      if (!ask.hasOn && !ask.press && !ask.hasBrightness && !ask.hasColour) return Status::NotFound;
    }
  }
  for (uint8_t i = 0; i < partCount_; ++i) {
    const Status s = parts_[i]->thingSet(id, ask);
    if (s != Status::NotFound) return s;
  }
  return thingSet(id, ask);
}

void Module::stopAll() {
  onStop();
  for (uint8_t i = 0; i < partCount_; ++i) parts_[i]->stop();
}

bool ModuleHost::takeChanged(Module& m) {
  bool any = m.changed_;
  m.changed_ = false;
  for (uint8_t i = 0; i < m.partCount_; ++i) {
    if (m.parts_[i]->takeChanged()) any = true;
  }
  return any;
}

bool ModuleHost::takeUnsaved(Module& m) {
  bool any = m.unsaved_;
  m.unsaved_ = false;
  for (uint8_t i = 0; i < m.partCount_; ++i) {
    if (m.parts_[i]->takeUnsaved()) any = true;
  }
  return any;
}

}  // namespace lmp::sdk

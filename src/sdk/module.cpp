#include "sdk/module.h"

namespace lmp::sdk {

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

Status Module::thingSetAll(const char* id, const ThingAsk& ask) {
  if (!id) return Status::Invalid;
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

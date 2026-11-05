// LoopMaxPro SDK - the state of a module and its settings, as native structs.
// One table of fields for each struct gives the document, the file, the checks and the
// schema the console builds its form from.
//
//   struct MeteoState { float celsius; uint32_t readAt; bool ok; };
//   LMP_FIELDS(MeteoState, LMP_F(celsius, "celsius", F32()), ...)
//   lmp::sdk::State<MeteoState> state_;       state_->celsius = 21.5f;  state_.touch();
//
//   struct MeteoCfg { lmp::FixedString<32> place; uint16_t everyMin; };
//   LMP_FIELDS(MeteoCfg, LMP_F(place, "place", Str(0, 32, "")), LMP_F(everyMin, "everyMin", U16(1, 1440, 30)))
//   lmp::sdk::ModuleConfig<MeteoCfg> cfg_;    useSettings(cfg_);  cfg_->everyMin
#pragma once
#include "kernel/fields/fields.h"
#include "sdk/module.h"

namespace lmp::sdk {

// What the module is doing now. Shown in its document, never kept on file.
template <class T>
class State : public Component {
 public:
  State() { setDefaults(value_); }

  T* operator->() { return &value_; }
  const T* operator->() const { return &value_; }
  T& value() { return value_; }
  const T& value() const { return value_; }

  // Call after changing a value: who looks at the device is told.
  void touch() { changed(); }

  void describe(JsonWriter& w) const override { writeFields(w, value_, Secrets::Flag); }

 private:
  T value_;
};

// The settings of a module, without their type: what the commands and the file work on.
class SettingsBase : public Component {
 public:
  void commands(CommandSet& set) override;
  void describe(JsonWriter& w) const override;
  void save(JsonWriter& w) const override;
  void load(const JsonValue& doc) override;

  // The members as the console reads them: secrets are replaced by "<key>Set".
  void describeConfig(JsonWriter& w) const;
  // [{"key":..,"type":..,"min":..,"max":..,"def":..}, ...]
  void schema(JsonWriter& w) const;
  // Applies the members found in the object. Nothing changes when one value is refused.
  Status apply(const JsonValue& json, FieldError* error = nullptr);
  void reset();

 protected:
  SettingsBase(void* object, Span<const FieldDesc> fields) : object_(object), fields_(fields) {}

  // Called after the settings changed, for the module that wants to follow them at once.
  virtual void applied() {}

 private:
  void* object_;
  Span<const FieldDesc> fields_;
};

template <class T>
class ModuleConfig : public SettingsBase {
 public:
  ModuleConfig() : SettingsBase(&value_, fieldsOf<T>()) { setDefaults(value_); }

  T* operator->() { return &value_; }
  const T* operator->() const { return &value_; }
  T& value() { return value_; }
  const T& value() const { return value_; }

  // Call after changing a value from the code of the module: the file follows.
  void touch() { unsaved(); }

 private:
  T value_;
};

}  // namespace lmp::sdk

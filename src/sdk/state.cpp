#include "sdk/state.h"

namespace lmp::sdk {
namespace {

constexpr const char* kMember = "config";

}  // namespace

Status Module::useSettings(SettingsBase& part) {
  LMP_TRY(use(part));
  settings_ = &part;
  return Status::Ok;
}

// In the document and on file the settings have a place of their own: their names are free
// to be the same as the ones of the state.
void SettingsBase::describe(JsonWriter& w) const {
  w.beginObject(kMember);
  describeConfig(w);
  w.endObject();
}

void SettingsBase::describeConfig(JsonWriter& w) const {
  fieldsWrite(w, object_, fields_, Secrets::Flag);
}

void SettingsBase::schema(JsonWriter& w) const { fieldsSchema(w, fields_); }

// On file the secrets are written too: it is where they live.
void SettingsBase::save(JsonWriter& w) const {
  w.beginObject(kMember);
  fieldsWrite(w, object_, fields_, Secrets::Show);
  w.endObject();
}

void SettingsBase::load(const JsonValue& doc) {
  const JsonValue mine = doc.get(kMember);
  if (!mine.isObject()) return;
  // A value that is no longer valid, after a change of the firmware, leaves the defaults.
  if (fieldsRead(mine, object_, fields_, true) != Status::Ok) fieldsDefaults(object_, fields_);
}

Status SettingsBase::apply(const JsonValue& json, FieldError* error) {
  if (!json.isObject() || json.size() == 0) return Status::Invalid;
  // Something must be for these settings: a request with other names only is a mistake.
  bool known = false;
  for (const FieldDesc& f : fields_) {
    if (!(f.spec.flags & (kFieldReadOnly | kFieldLocked)) && json.get(f.key).valid()) known = true;
  }
  if (!known) return Status::Invalid;
  LMP_TRY(fieldsRead(json, object_, fields_, true, error));
  unsaved();
  applied();
  return Status::Ok;
}

void SettingsBase::reset() {
  fieldsDefaults(object_, fields_);
  unsaved();
  applied();
}

void SettingsBase::commands(CommandSet& set) {
  set.add("config.set", LMP_HELP("Changes the settings of the module: one or more"),
          [this](const Args&, CmdContext& ctx) -> Status {
            if (!ctx.raw) return Status::Invalid;
            FieldError error;
            const Status s = apply(*ctx.raw, &error);
            if (s != Status::Ok) ctx.errorField = error.key;
            return s;
          },
          kCmdAnyArgs);

  set.add("config.reset",
          LMP_HELP("Takes the settings of the module back to the ones it was born with"),
          [this](const Args&, CmdContext&) -> Status {
            reset();
            return Status::Ok;
          },
          kCmdConfirm);
}

}  // namespace lmp::sdk

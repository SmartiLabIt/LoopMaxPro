// LoopMaxPro - field tables.
// One declaration per struct gives: JSON output, JSON input with validation, defaults and schema.
//
//   struct RelayCfg {
//     lmp::FixedString<32> deviceName;
//     uint16_t             saveDelayMs;
//     bool                 restoreOnBoot;
//   };
//   LMP_FIELDS(RelayCfg,
//     LMP_F(deviceName,    "deviceName",    Str(1, 32, "Relay")),
//     LMP_F(saveDelayMs,   "saveDelayMs",   U16(0, 60000, 5000)),
//     LMP_F(restoreOnBoot, "restoreOnBoot", Bool(true)))
//
// LMP_FIELDS must be used at global scope. The struct must be standard layout.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <type_traits>

#include "base/fixed_string.h"
#include "base/span.h"
#include "base/status.h"
#include "kernel/json/json_reader.h"
#include "kernel/json/json_writer.h"

namespace lmp {

enum class FieldKind : uint8_t { Bool, U8, U16, U32, I8, I16, I32, I64, F32, F64, Str };

enum FieldFlags : uint8_t {
  kFieldNone = 0,
  kFieldSecret = 1,    // accepted in input, never written in output
  kFieldReadOnly = 2,  // written in output, ignored in input
  kFieldLocked = 4     // a setting of a fuller version of the module: shown with its default,
                       // never taken, not even from the file
};

// Limits and default of one field. For strings min and max are lengths.
// Numbers are kept as double: whole values are exact up to 2^53.
struct FieldSpec {
  FieldKind kind;
  double min;
  double max;
  double def;
  const char* defStr;
  uint8_t flags;

  constexpr FieldSpec secret() const {
    return FieldSpec{kind, min, max, def, defStr, static_cast<uint8_t>(flags | kFieldSecret)};
  }
  constexpr FieldSpec readOnly() const {
    return FieldSpec{kind, min, max, def, defStr, static_cast<uint8_t>(flags | kFieldReadOnly)};
  }
  constexpr FieldSpec locked() const {
    return FieldSpec{kind, min, max, def, defStr, static_cast<uint8_t>(flags | kFieldLocked)};
  }
};

struct FieldDesc {
  const char* key;
  uint16_t offset;
  uint16_t size;
  FieldSpec spec;
};

// Builders used inside LMP_F.
namespace field {
constexpr FieldSpec Bool(bool def = false) {
  return {FieldKind::Bool, 0, 1, def ? 1.0 : 0.0, nullptr, kFieldNone};
}
constexpr FieldSpec U8(double min = 0, double max = 255, double def = 0) {
  return {FieldKind::U8, min, max, def, nullptr, kFieldNone};
}
constexpr FieldSpec U16(double min = 0, double max = 65535, double def = 0) {
  return {FieldKind::U16, min, max, def, nullptr, kFieldNone};
}
constexpr FieldSpec U32(double min = 0, double max = 4294967295.0, double def = 0) {
  return {FieldKind::U32, min, max, def, nullptr, kFieldNone};
}
constexpr FieldSpec I8(double min = -128, double max = 127, double def = 0) {
  return {FieldKind::I8, min, max, def, nullptr, kFieldNone};
}
constexpr FieldSpec I16(double min = -32768, double max = 32767, double def = 0) {
  return {FieldKind::I16, min, max, def, nullptr, kFieldNone};
}
constexpr FieldSpec I32(double min = -2147483648.0, double max = 2147483647.0, double def = 0) {
  return {FieldKind::I32, min, max, def, nullptr, kFieldNone};
}
constexpr FieldSpec I64(double min = -9007199254740992.0, double max = 9007199254740992.0,
                        double def = 0) {
  return {FieldKind::I64, min, max, def, nullptr, kFieldNone};
}
constexpr FieldSpec F32(double min = -3.4e38, double max = 3.4e38, double def = 0) {
  return {FieldKind::F32, min, max, def, nullptr, kFieldNone};
}
constexpr FieldSpec F64(double min = -1.7e308, double max = 1.7e308, double def = 0) {
  return {FieldKind::F64, min, max, def, nullptr, kFieldNone};
}
constexpr FieldSpec Str(double minLen = 0, double maxLen = 65535, const char* def = "") {
  return {FieldKind::Str, minLen, maxLen, 0, def, kFieldNone};
}
}  // namespace field

// Storage type expected for each kind: checked at compile time by LMP_F.
template <class T>
struct FieldStorage {
  static constexpr bool known = false;
};
#define LMP_FIELD_STORAGE_(Type, Kind)                       \
  template <>                                                \
  struct FieldStorage<Type> {                                \
    static constexpr bool known = true;                      \
    static constexpr FieldKind kind = FieldKind::Kind;       \
  }
LMP_FIELD_STORAGE_(bool, Bool);
LMP_FIELD_STORAGE_(uint8_t, U8);
LMP_FIELD_STORAGE_(uint16_t, U16);
LMP_FIELD_STORAGE_(uint32_t, U32);
LMP_FIELD_STORAGE_(int8_t, I8);
LMP_FIELD_STORAGE_(int16_t, I16);
LMP_FIELD_STORAGE_(int32_t, I32);
LMP_FIELD_STORAGE_(int64_t, I64);
LMP_FIELD_STORAGE_(float, F32);
LMP_FIELD_STORAGE_(double, F64);
#undef LMP_FIELD_STORAGE_
template <size_t N>
struct FieldStorage<FixedString<N>> {
  static constexpr bool known = true;
  static constexpr FieldKind kind = FieldKind::Str;
};

// Specialized by LMP_FIELDS for each struct.
template <class T>
Span<const FieldDesc> fieldsOf();

// ---------------------------------------------------------------- untyped engine

struct FieldError {
  const char* key = nullptr;   // field that failed
  Status status = Status::Ok;
};

// What to do with secret fields in output.
enum class Secrets : uint8_t {
  Hide,   // leave them out
  Flag,   // write "<key>Set": true or false, never the value
  Show    // write the value: for storage only
};

// Sets every field to its default.
void fieldsDefaults(void* object, Span<const FieldDesc> fields);

// Writes the members into the current object of the writer. Does not open or close it.
void fieldsWrite(JsonWriter& w, const void* object, Span<const FieldDesc> fields,
                 Secrets secrets = Secrets::Hide);

// Checks one JSON value against a spec. For numbers and booleans the value is returned in number.
// stringCapacity is the longest text the destination can hold, terminator excluded.
Status fieldValidate(const JsonValue& value, const FieldSpec& spec, size_t stringCapacity,
                     double& number);

// Reads the members of a JSON object. Nothing is changed unless every value is valid.
// With partial == true missing members keep their value, otherwise they get the default.
Status fieldsRead(const JsonValue& json, void* object, Span<const FieldDesc> fields,
                  bool partial = true, FieldError* error = nullptr);

// Writes the schema as an array: [{"key":..,"type":..,"min":..,"max":..,"def":..}, ...]
void fieldsSchema(JsonWriter& w, Span<const FieldDesc> fields);

// Writes type, limits and default of one spec into the current object.
void fieldSpecDescribe(JsonWriter& w, const FieldSpec& spec, size_t stringCapacity);

const char* fieldKindName(FieldKind kind);

// ---------------------------------------------------------------- typed helpers

template <class T>
void setDefaults(T& object) {
  fieldsDefaults(&object, fieldsOf<T>());
}

template <class T>
void writeFields(JsonWriter& w, const T& object, Secrets secrets = Secrets::Hide) {
  fieldsWrite(w, &object, fieldsOf<T>(), secrets);
}

template <class T>
Status readFields(const JsonValue& json, T& object, bool partial = true,
                  FieldError* error = nullptr) {
  return fieldsRead(json, &object, fieldsOf<T>(), partial, error);
}

template <class T>
void writeSchema(JsonWriter& w) {
  fieldsSchema(w, fieldsOf<T>());
}

}  // namespace lmp

#define LMP_FIELDS(Type, ...)                                                          \
  namespace lmp {                                                                      \
  template <>                                                                          \
  inline Span<const FieldDesc> fieldsOf<Type>() {                                      \
    static_assert(std::is_standard_layout<Type>::value,                                \
                  "field tables need a standard layout struct");                       \
    using LmpFieldOwner_ = Type;                                                       \
    using namespace ::lmp::field;                                                      \
    static constexpr FieldDesc table[] = {__VA_ARGS__};                                \
    return Span<const FieldDesc>(table, sizeof(table) / sizeof(table[0]));             \
  }                                                                                    \
  }

// The checks run at compile time: a wrong spec or an unsupported member type stops the build.
#define LMP_F(member, key, spec)                                                       \
  ([]() constexpr {                                                                    \
    using LmpMember_ = decltype(LmpFieldOwner_::member);                               \
    static_assert(::lmp::FieldStorage<LmpMember_>::known,                              \
                  "LMP_F: member type not supported by the field tables");             \
    static_assert((spec).kind == ::lmp::FieldStorage<LmpMember_>::kind,                \
                  "LMP_F: the spec does not match the type of the member");            \
    return ::lmp::FieldDesc{key, static_cast<uint16_t>(offsetof(LmpFieldOwner_, member)), \
                            static_cast<uint16_t>(sizeof(LmpFieldOwner_::member)), spec}; \
  }())

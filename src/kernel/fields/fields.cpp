#include "kernel/fields/fields.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace lmp {
namespace {

inline bool isInteger(FieldKind k) {
  return k != FieldKind::Bool && k != FieldKind::F32 && k != FieldKind::F64 && k != FieldKind::Str;
}

template <class T>
T load(const void* object, const FieldDesc& f) {
  T v;
  memcpy(&v, static_cast<const uint8_t*>(object) + f.offset, sizeof(T));
  return v;
}

template <class T>
void store(void* object, const FieldDesc& f, T v) {
  memcpy(static_cast<uint8_t*>(object) + f.offset, &v, sizeof(T));
}

double loadNumber(const void* object, const FieldDesc& f) {
  switch (f.spec.kind) {
    case FieldKind::Bool: return load<bool>(object, f) ? 1.0 : 0.0;
    case FieldKind::U8:   return load<uint8_t>(object, f);
    case FieldKind::U16:  return load<uint16_t>(object, f);
    case FieldKind::U32:  return load<uint32_t>(object, f);
    case FieldKind::I8:   return load<int8_t>(object, f);
    case FieldKind::I16:  return load<int16_t>(object, f);
    case FieldKind::I32:  return load<int32_t>(object, f);
    case FieldKind::I64:  return static_cast<double>(load<int64_t>(object, f));
    case FieldKind::F32:  return load<float>(object, f);
    case FieldKind::F64:  return load<double>(object, f);
    case FieldKind::Str:  break;
  }
  return 0;
}

void storeNumber(void* object, const FieldDesc& f, double v) {
  switch (f.spec.kind) {
    case FieldKind::Bool: store<bool>(object, f, v != 0); break;
    case FieldKind::U8:   store<uint8_t>(object, f, static_cast<uint8_t>(v)); break;
    case FieldKind::U16:  store<uint16_t>(object, f, static_cast<uint16_t>(v)); break;
    case FieldKind::U32:  store<uint32_t>(object, f, static_cast<uint32_t>(v)); break;
    case FieldKind::I8:   store<int8_t>(object, f, static_cast<int8_t>(v)); break;
    case FieldKind::I16:  store<int16_t>(object, f, static_cast<int16_t>(v)); break;
    case FieldKind::I32:  store<int32_t>(object, f, static_cast<int32_t>(v)); break;
    case FieldKind::I64:  store<int64_t>(object, f, static_cast<int64_t>(v)); break;
    case FieldKind::F32:  store<float>(object, f, static_cast<float>(v)); break;
    case FieldKind::F64:  store<double>(object, f, v); break;
    case FieldKind::Str:  break;
  }
}

char* stringAt(void* object, const FieldDesc& f) {
  return reinterpret_cast<char*>(static_cast<uint8_t*>(object) + f.offset);
}
const char* stringAt(const void* object, const FieldDesc& f) {
  return reinterpret_cast<const char*>(static_cast<const uint8_t*>(object) + f.offset);
}

void storeString(void* object, const FieldDesc& f, const char* text) {
  char* dst = stringAt(object, f);
  const size_t cap = f.size;  // capacity plus terminator
  const size_t n = strlen(text);
  const size_t len = n < cap - 1 ? n : cap - 1;
  memcpy(dst, text, len);
  dst[len] = '\0';
}

// Checks one JSON value against a field. On success the number is returned in out.
Status validate(const JsonValue& v, const FieldDesc& f, double& out) {
  const FieldSpec& s = f.spec;
  if (s.kind == FieldKind::Str) {
    if (!v.isString()) return Status::Invalid;
    const size_t len = v.stringLength();
    if (len >= f.size) return Status::Invalid;
    if (len < static_cast<size_t>(s.min) || static_cast<double>(len) > s.max) return Status::Invalid;
    return Status::Ok;
  }
  if (s.kind == FieldKind::Bool) {
    bool b = false;
    if (v.getBool(b)) {
      out = b ? 1 : 0;
      return Status::Ok;
    }
    // 0 and 1 are accepted too: form encoded inputs have no booleans.
    int64_t n = 0;
    if (v.getInt(n) && (n == 0 || n == 1)) {
      out = static_cast<double>(n);
      return Status::Ok;
    }
    return Status::Invalid;
  }
  double d = 0;
  if (isInteger(s.kind)) {
    int64_t n = 0;
    if (!v.getInt(n)) return Status::Invalid;
    d = static_cast<double>(n);
  } else if (!v.getDouble(d)) {
    return Status::Invalid;
  }
  if (d < s.min || d > s.max) return Status::Invalid;
  out = d;
  return Status::Ok;
}

void writeNumber(JsonWriter& w, const char* key, FieldKind kind, double v) {
  switch (kind) {
    case FieldKind::Bool: w.boolean(key, v != 0); break;
    case FieldKind::F32:  w.f64(key, v, 3); break;
    case FieldKind::F64:  w.f64(key, v, 6); break;
    default:
      if (v < 0) {
        w.i64(key, static_cast<int64_t>(v));
      } else {
        w.u64(key, static_cast<uint64_t>(v));
      }
      break;
  }
}

}  // namespace

const char* fieldKindName(FieldKind kind) {
  switch (kind) {
    case FieldKind::Bool: return "bool";
    case FieldKind::U8:   return "u8";
    case FieldKind::U16:  return "u16";
    case FieldKind::U32:  return "u32";
    case FieldKind::I8:   return "i8";
    case FieldKind::I16:  return "i16";
    case FieldKind::I32:  return "i32";
    case FieldKind::I64:  return "i64";
    case FieldKind::F32:  return "f32";
    case FieldKind::F64:  return "f64";
    case FieldKind::Str:  return "str";
  }
  return "?";
}

void fieldsDefaults(void* object, Span<const FieldDesc> fields) {
  for (const FieldDesc& f : fields) {
    if (f.spec.kind == FieldKind::Str) {
      storeString(object, f, f.spec.defStr ? f.spec.defStr : "");
    } else {
      storeNumber(object, f, f.spec.def);
    }
  }
}

Status fieldValidate(const JsonValue& value, const FieldSpec& spec, size_t stringCapacity,
                     double& number) {
  FieldDesc f{"", 0, static_cast<uint16_t>(stringCapacity + 1), spec};
  return validate(value, f, number);
}

void fieldsWrite(JsonWriter& w, const void* object, Span<const FieldDesc> fields,
                 Secrets secrets) {
  for (const FieldDesc& f : fields) {
    if ((f.spec.flags & kFieldSecret) && secrets != Secrets::Show) {
      if (secrets == Secrets::Flag) {
        // "<key>Set": tells whether a value exists, without revealing it.
        char name[40];
        snprintf(name, sizeof(name), "%sSet", f.key);
        const bool set = f.spec.kind == FieldKind::Str ? stringAt(object, f)[0] != '\0'
                                                       : loadNumber(object, f) != 0;
        w.boolean(name, set);
      }
      continue;
    }
    if (f.spec.kind == FieldKind::Str) {
      w.str(f.key, stringAt(object, f));
    } else if (f.spec.kind == FieldKind::I64) {
      w.i64(f.key, load<int64_t>(object, f));
    } else {
      writeNumber(w, f.key, f.spec.kind, loadNumber(object, f));
    }
  }
}

Status fieldsRead(const JsonValue& json, void* object, Span<const FieldDesc> fields, bool partial,
                  FieldError* error) {
  if (error) *error = FieldError();
  if (!json.isObject()) {
    if (error) error->status = Status::Invalid;
    return Status::Invalid;
  }

  // First pass: validate everything, so a bad value leaves the object untouched.
  for (const FieldDesc& f : fields) {
    if (f.spec.flags & (kFieldReadOnly | kFieldLocked)) continue;
    const JsonValue v = json.get(f.key);
    if (!v.valid()) continue;
    double unused = 0;
    const Status s = validate(v, f, unused);
    if (s != Status::Ok) {
      if (error) {
        error->key = f.key;
        error->status = s;
      }
      return s;
    }
  }

  // Second pass: apply.
  for (const FieldDesc& f : fields) {
    if (f.spec.flags & (kFieldReadOnly | kFieldLocked)) continue;
    const JsonValue v = json.get(f.key);
    if (!v.valid()) {
      if (!partial) {
        if (f.spec.kind == FieldKind::Str) {
          storeString(object, f, f.spec.defStr ? f.spec.defStr : "");
        } else {
          storeNumber(object, f, f.spec.def);
        }
      }
      continue;
    }
    if (f.spec.kind == FieldKind::Str) {
      v.getString(stringAt(object, f), f.size);
    } else if (f.spec.kind == FieldKind::I64) {
      store<int64_t>(object, f, v.asInt());
    } else {
      double d = 0;
      validate(v, f, d);
      storeNumber(object, f, d);
    }
  }
  return Status::Ok;
}

void fieldSpecDescribe(JsonWriter& w, const FieldSpec& s, size_t stringCapacity) {
  w.str("type", fieldKindName(s.kind));
  if (s.kind == FieldKind::Str) {
    const double cap = static_cast<double>(stringCapacity);
    w.u64("min", static_cast<uint64_t>(s.min));
    w.u64("max", static_cast<uint64_t>(s.max < cap ? s.max : cap));
    if (!(s.flags & kFieldSecret)) w.str("def", s.defStr ? s.defStr : "");
  } else if (s.kind == FieldKind::Bool) {
    w.boolean("def", s.def != 0);
  } else {
    const bool real = s.kind == FieldKind::F32 || s.kind == FieldKind::F64;
    // Full range limits of floating point types say nothing useful: leave them out.
    if (!real || fabs(s.min) < 1e30) writeNumber(w, "min", real ? s.kind : FieldKind::I64, s.min);
    if (!real || fabs(s.max) < 1e30) writeNumber(w, "max", real ? s.kind : FieldKind::I64, s.max);
    writeNumber(w, "def", real ? s.kind : FieldKind::I64, s.def);
  }
  if (s.flags & kFieldSecret) w.boolean("secret", true);
  if (s.flags & kFieldReadOnly) w.boolean("readOnly", true);
  if (s.flags & kFieldLocked) w.boolean("locked", true);
}

void fieldsSchema(JsonWriter& w, Span<const FieldDesc> fields) {
  w.beginArray();
  for (const FieldDesc& f : fields) {
    w.beginObject();
    w.str("key", f.key);
    fieldSpecDescribe(w, f.spec, f.size - 1);
    w.endObject();
  }
  w.endArray();
}

}  // namespace lmp

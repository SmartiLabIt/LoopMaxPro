#include "kernel/config/kv_fields.h"

#include <string.h>

#include "kernel/log/logger.h"

namespace lmp {
namespace {

inline size_t numberSize(FieldKind kind) {
  switch (kind) {
    case FieldKind::Bool:
    case FieldKind::U8:
    case FieldKind::I8:  return 1;
    case FieldKind::U16:
    case FieldKind::I16: return 2;
    case FieldKind::U32:
    case FieldKind::I32:
    case FieldKind::F32: return 4;
    case FieldKind::I64:
    case FieldKind::F64: return 8;
    case FieldKind::Str: break;
  }
  return 0;
}

inline uint8_t* at(void* object, const FieldDesc& f) {
  return static_cast<uint8_t*>(object) + f.offset;
}
inline const uint8_t* at(const void* object, const FieldDesc& f) {
  return static_cast<const uint8_t*>(object) + f.offset;
}

// Length of the value as stored: strings keep their terminator, so it is never zero.
inline size_t storedSize(const void* object, const FieldDesc& f) {
  if (f.spec.kind == FieldKind::Str) {
    return strlen(reinterpret_cast<const char*>(at(object, f))) + 1;
  }
  return numberSize(f.spec.kind);
}

}  // namespace

Status kvLoadFields(hal::IKv& kv, const char* ns, void* object, Span<const FieldDesc> fields) {
  for (const FieldDesc& f : fields) {
    uint8_t buffer[kKvFieldMax];
    size_t len = 0;
    const Status s = kv.get(ns, f.key, buffer, sizeof(buffer), len);
    if (s == Status::NotFound) continue;      // keeps what the object has
    if (s != Status::Ok) {
      LMP_LOGW(kLogSys, "config: cannot read %s.%s (%s)", ns, f.key, statusName(s));
      continue;
    }
    if (f.spec.kind == FieldKind::Str) {
      // Stored with the terminator. Anything that does not fit is ignored.
      if (len == 0 || len > f.size || buffer[len - 1] != '\0') continue;
      memcpy(at(object, f), buffer, len);
    } else {
      if (len != numberSize(f.spec.kind)) continue;
      memcpy(at(object, f), buffer, len);
    }
  }
  return Status::Ok;
}

Status kvSaveFields(hal::IKv& kv, const char* ns, const void* object,
                    Span<const FieldDesc> fields) {
  Status result = Status::Ok;
  for (const FieldDesc& f : fields) {
    const size_t size = storedSize(object, f);
    if (size == 0 || size > kKvFieldMax) continue;

    uint8_t stored[kKvFieldMax];
    size_t len = 0;
    const Status got = kv.get(ns, f.key, stored, sizeof(stored), len);
    if (got == Status::Ok && len == size && memcmp(stored, at(object, f), size) == 0) continue;

    const Status s = kv.set(ns, f.key, at(object, f), size);
    if (s != Status::Ok) {
      LMP_LOGE(kLogSys, "config: cannot write %s.%s (%s)", ns, f.key, statusName(s));
      result = s;
    }
  }
  return result;
}

}  // namespace lmp

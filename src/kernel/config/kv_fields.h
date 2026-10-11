// LoopMaxPro - a struct with a field table kept in the key/value store.
// One entry for each field, under the namespace of the owner. Used by the system settings
// and by the services that keep settings of their own.
#pragma once
#include "base/span.h"
#include "base/status.h"
#include "hal/api/istorage.h"
#include "kernel/fields/fields.h"

namespace lmp {

// Longest stored value, terminator included.
constexpr size_t kKvFieldMax = 128;

// Reads the stored values into the object. Missing or unusable values keep what the object has.
Status kvLoadFields(hal::IKv& kv, const char* ns, void* object, Span<const FieldDesc> fields);

// Writes the values that differ from the stored ones: flash wears out.
Status kvSaveFields(hal::IKv& kv, const char* ns, const void* object,
                    Span<const FieldDesc> fields);

}  // namespace lmp

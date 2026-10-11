// LoopMaxPro - events. Small native values: they become JSON only when they leave the device.
#pragma once
#include <stdint.h>

namespace lmp {

enum class EventId : uint8_t {
  None = 0,
  ServiceState,     // a = service id, b = new state
  ConfigChanged,    // a = section index
  ModeChanged,      // a = new mode
  NetApUp,
  NetApDown,
  NetStaConnected,
  NetStaLost,
  NetFallback,
  NetJoinResult,    // b = 1 when the new network was accepted
  InternetUp,
  InternetDown,
  TimeSynced,       // a = source, value = unix time
  ModuleState,      // a = module id, b = new state
  ModuleData,       // a = module id
  CommandDone,      // value = command reference, b = status
  OtaProgress,      // b = percent
  OtaDone,          // b = status
  ResetRequested,   // a = 0 restart, 1 factory reset
  MqttConnected,
  MqttLost,
  CloudConnected,
  CloudLost,
  LoraReceived,     // a = length in bytes, value = sequence number of the packet
  LoraSent,         // b = status, value = sequence number of the packet
  CloudUpdate,      // what the server offers as update changed: the document of sys.cloud
  ProductChanged,   // the service of the product: a = its state, b = 1 when the device is blocked
  NetNear,          // the other devices of the system found on the network changed: b = how many
  Count
};

const char* eventName(EventId id);
// The name of ProductChanged is of the service of the product: given once, before the start.
void setProductEventName(const char* name);

struct Event {
  EventId id = EventId::None;
  uint8_t a = 0;
  uint8_t b = 0;
  uint32_t value = 0;

  constexpr Event() = default;
  constexpr Event(EventId i, uint8_t a_ = 0, uint8_t b_ = 0, uint32_t v = 0)
      : id(i), a(a_), b(b_), value(v) {}
};

}  // namespace lmp

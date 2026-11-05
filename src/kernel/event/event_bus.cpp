#include "kernel/event/event_bus.h"

namespace lmp {

namespace {
const char* productEvent = "product";
}  // namespace

void setProductEventName(const char* name) {
  if (name && name[0]) productEvent = name;
}

const char* eventName(EventId id) {
  switch (id) {
    case EventId::ServiceState:    return "svc";
    case EventId::ConfigChanged:   return "config";
    case EventId::ModeChanged:     return "mode";
    case EventId::NetApUp:         return "net.ap.up";
    case EventId::NetApDown:       return "net.ap.down";
    case EventId::NetStaConnected: return "net.sta.up";
    case EventId::NetStaLost:      return "net.sta.lost";
    case EventId::NetFallback:     return "net.fallback";
    case EventId::NetJoinResult:   return "net.join";
    case EventId::InternetUp:      return "internet.up";
    case EventId::InternetDown:    return "internet.down";
    case EventId::TimeSynced:      return "time";
    case EventId::ModuleState:     return "module.state";
    case EventId::ModuleData:      return "module.data";
    case EventId::CommandDone:     return "cmd";
    case EventId::OtaProgress:     return "ota.progress";
    case EventId::OtaDone:         return "ota.done";
    case EventId::ResetRequested:  return "reset";
    case EventId::MqttConnected:   return "mqtt.up";
    case EventId::MqttLost:        return "mqtt.lost";
    case EventId::CloudConnected:  return "cloud.up";
    case EventId::CloudLost:       return "cloud.lost";
    case EventId::LoraReceived:    return "lora.rx";
    case EventId::LoraSent:        return "lora.tx";
    case EventId::CloudUpdate:     return "cloud.update";
    case EventId::ProductChanged:  return productEvent;
    case EventId::NetNear:         return "net.near";
    case EventId::None:
    case EventId::Count:           break;
  }
  return "none";
}

bool EventBus::subscribe(EventHandler handler) {
  if (!handler) return false;
  hal::LockGuard guard(mutex_);
  if (count_ >= LMP_MAX_SUBSCRIBERS) return false;
  handlers_[count_++] = handler;
  return true;
}

bool EventBus::publish(const Event& event) {
  hal::LockGuard guard(mutex_);
  if (size_ >= LMP_EVENT_QUEUE) {
    ++lost_;
    return false;
  }
  queue_[(head_ + size_) % LMP_EVENT_QUEUE] = event;
  ++size_;
  return true;
}

size_t EventBus::pending() const {
  hal::LockGuard guard(mutex_);
  return size_;
}

size_t EventBus::dispatch() {
  // Only the events already queued: what the handlers publish waits for the next call.
  size_t budget;
  {
    hal::LockGuard guard(mutex_);
    budget = size_;
  }
  size_t delivered = 0;
  while (budget--) {
    Event event;
    size_t handlers;
    {
      hal::LockGuard guard(mutex_);
      if (size_ == 0) break;
      event = queue_[head_];
      head_ = (head_ + 1) % LMP_EVENT_QUEUE;
      --size_;
      handlers = count_;
    }
    for (size_t i = 0; i < handlers; ++i) handlers_[i](event);
    ++delivered;
  }
  return delivered;
}

}  // namespace lmp

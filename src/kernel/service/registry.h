// LoopMaxPro - service registry.
// Computes the start order from the declared dependencies, starts what can start,
// stops what lost a dependency or is not allowed in the current mode.
#pragma once
#include <stddef.h>

#include "hal/api/iclock.h"
#include "kernel/event/event_bus.h"
#include "kernel/service/service.h"

namespace lmp {

class ServiceRegistry {
 public:
  static constexpr uint32_t kStartTimeoutMs = 30000;
  static constexpr uint32_t kRetryFirstMs = 2000;
  static constexpr uint32_t kRetryMaxMs = 60000;
  static constexpr uint8_t kCriticalAttempts = 5;

  ServiceRegistry(hal::IClock& clock, EventBus& events) : clock_(clock), events_(events) {}

  // One service for each id. Call before resolve().
  Status add(Service& service);

  // Computes the order. Invalid when the dependencies form a cycle.
  Status resolve();

  // One step of the state machines, then the ticks that are due.
  void tick(Kernel& kernel, uint32_t nowMs, Mode mode);

  // Stops everything, in reverse order. Used before a restart.
  void stopAll();

  void deliver(const Event& event);

  Service* find(ServiceId id) const;
  Service* find(const char* name) const;
  size_t count() const { return count_; }
  // Services in start order. Valid after resolve().
  Service* at(size_t index) const { return index < count_ ? order_[index] : nullptr; }

  // True when every service that can run in this mode has finished starting,
  // successfully or not. Marks the end of the boot phase.
  bool settled(Mode mode) const;

  // True when a critical service keeps failing: the kernel restarts the board.
  bool criticalFailure() const { return criticalFailure_ != nullptr; }
  const Service* failedService() const { return criticalFailure_; }

  // Writes the list used by the report: [{"name":..,"icon":..,"state":..,...}]
  void describe(JsonWriter& w) const;

 private:
  bool depsRunning(const Service& s) const;
  static bool modeAllowed(const Service& s, Mode mode);
  void startService(Kernel& kernel, Service& s, uint32_t nowMs);
  void stopService(Service& s, ServiceState next);
  void scheduleRetry(Service& s, uint32_t nowMs);
  bool visit(Service& s, uint8_t* marks, size_t& placed);

  hal::IClock& clock_;
  EventBus& events_;
  Service* byId_[kServiceCount] = {};
  Service* order_[kServiceCount] = {};
  size_t count_ = 0;
  bool resolved_ = false;
  const Service* criticalFailure_ = nullptr;
};

}  // namespace lmp

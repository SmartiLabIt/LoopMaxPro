#include "kernel/service/registry.h"

#include <string.h>

#include "kernel/kernel.h"
#include "kernel/log/logger.h"

namespace lmp {

const char* serviceStateName(ServiceState state) {
  switch (state) {
    case ServiceState::Stopped:  return "Stopped";
    case ServiceState::Waiting:  return "Waiting";
    case ServiceState::Starting: return "Starting";
    case ServiceState::Ready:    return "Ready";
    case ServiceState::Degraded: return "Degraded";
    case ServiceState::Error:    return "Error";
  }
  return "?";
}

const char* modeName(Mode mode) {
  switch (mode) {
    case Mode::Ap:  return "AP";
    case Mode::Lan: return "LAN";
    case Mode::Any: break;
  }
  return "ANY";
}

// ---------------------------------------------------------------- Service

void Service::ready() {
  if (state_ == ServiceState::Starting) {
    state_ = ServiceState::Ready;
    reason_ = "";
    attempts_ = 0;
  }
}

void Service::fail(const char* reason) {
  state_ = ServiceState::Error;
  reason_ = reason ? reason : "";
  ++stats_.failures;
  retryAtMs_ = 0;  // the registry schedules the retry at the next tick
}

void Service::degrade(const char* reason) {
  // Also accepted while starting: the service comes up with reduced functions.
  if (state_ == ServiceState::Ready || state_ == ServiceState::Degraded ||
      state_ == ServiceState::Starting) {
    state_ = ServiceState::Degraded;
    reason_ = reason ? reason : "";
  }
}

void Service::recover() {
  if (state_ == ServiceState::Degraded) {
    state_ = ServiceState::Ready;
    reason_ = "";
  }
}

// ---------------------------------------------------------------- registry

Status ServiceRegistry::add(Service& service) {
  const size_t index = static_cast<size_t>(service.info().id);
  if (index >= kServiceCount) return Status::Invalid;
  if (byId_[index]) return Status::Busy;
  if (service.info().deps.count > DepList::kMax) return Status::Invalid;
  byId_[index] = &service;
  order_[count_++] = &service;
  resolved_ = false;
  return Status::Ok;
}

Service* ServiceRegistry::find(ServiceId id) const {
  const size_t index = static_cast<size_t>(id);
  return index < kServiceCount ? byId_[index] : nullptr;
}

Service* ServiceRegistry::find(const char* name) const {
  if (!name) return nullptr;
  for (size_t i = 0; i < count_; ++i) {
    if (strcmp(order_[i]->info().name, name) == 0) return order_[i];
  }
  return nullptr;
}

// Depth first visit. marks: 0 not seen, 1 in progress, 2 placed.
bool ServiceRegistry::visit(Service& s, uint8_t* marks, size_t& placed) {
  const size_t index = static_cast<size_t>(s.info().id);
  if (marks[index] == 2) return true;
  if (marks[index] == 1) return false;  // cycle
  marks[index] = 1;
  const DepList& deps = s.info().deps;
  for (uint8_t i = 0; i < deps.count; ++i) {
    Service* dep = find(deps.ids[i]);
    if (!dep) {
      s.blocked_ = true;
      continue;
    }
    if (!visit(*dep, marks, placed)) return false;
    if (dep->blocked_) s.blocked_ = true;
  }
  marks[index] = 2;
  order_[placed++] = &s;
  return true;
}

Status ServiceRegistry::resolve() {
  Service* registered[kServiceCount];
  const size_t total = count_;
  for (size_t i = 0; i < total; ++i) {
    registered[i] = order_[i];
    registered[i]->blocked_ = false;
  }

  uint8_t marks[kServiceCount] = {};
  size_t placed = 0;
  for (size_t i = 0; i < total; ++i) {
    if (!visit(*registered[i], marks, placed)) {
      // Restore the registration order: the registry stays usable for diagnostics.
      for (size_t k = 0; k < total; ++k) order_[k] = registered[k];
      LMP_LOGE(kLogSys, "services: dependency cycle at '%s'", registered[i]->info().name);
      return Status::Invalid;
    }
  }

  for (size_t i = 0; i < count_; ++i) {
    Service& s = *order_[i];
    if (s.blocked_) {
      s.state_ = ServiceState::Stopped;
      s.reason_ = "missing dependency";
      LMP_LOGW(kLogSys, "service '%s' cannot start: a dependency is not in this build",
               s.info().name);
    } else {
      s.state_ = ServiceState::Waiting;
      s.reason_ = "";
    }
    s.published_ = s.state_;
  }
  resolved_ = true;
  return Status::Ok;
}

bool ServiceRegistry::depsRunning(const Service& s) const {
  const DepList& deps = s.info().deps;
  for (uint8_t i = 0; i < deps.count; ++i) {
    const Service* dep = find(deps.ids[i]);
    if (!dep || !dep->running()) return false;
  }
  return true;
}

bool ServiceRegistry::modeAllowed(const Service& s, Mode mode) {
  return (static_cast<uint8_t>(s.info().modes) & static_cast<uint8_t>(mode)) != 0;
}

void ServiceRegistry::scheduleRetry(Service& s, uint32_t nowMs) {
  uint32_t delay = kRetryFirstMs;
  for (uint8_t i = 1; i < s.attempts_ && delay < kRetryMaxMs; ++i) delay *= 2;
  if (delay > kRetryMaxMs) delay = kRetryMaxMs;
  s.retryAtMs_ = nowMs + delay;
  if (s.retryAtMs_ == 0) s.retryAtMs_ = 1;  // 0 means "not scheduled"
}

void ServiceRegistry::startService(Kernel& kernel, Service& s, uint32_t nowMs) {
  s.state_ = ServiceState::Starting;
  s.reason_ = "";
  s.startedMs_ = nowMs;
  ++s.attempts_;
  ++s.stats_.starts;
  const hal::ISystem* sys = kernel.board().sys;
  const uint32_t freeBefore = sys ? sys->heapFree() : 0;
  const Status result = s.start(kernel);
  if (sys) s.stats_.heapAtStart = static_cast<int32_t>(freeBefore) - static_cast<int32_t>(sys->heapFree());
  if (result == Status::Ok) {
    if (s.state_ == ServiceState::Starting) s.state_ = ServiceState::Ready;
    if (s.running()) s.attempts_ = 0;
    s.nextTickMs_ = nowMs;
  } else if (result != Status::Pending) {
    if (s.state_ != ServiceState::Error) {
      s.state_ = ServiceState::Error;
      ++s.stats_.failures;
    }
    if (!s.reason_ || !*s.reason_) s.reason_ = statusName(result);
  }
}

void ServiceRegistry::stopService(Service& s, ServiceState next) {
  s.stop();
  s.state_ = next;
  s.attempts_ = 0;
}

void ServiceRegistry::tick(Kernel& kernel, uint32_t nowMs, Mode mode) {
  if (!resolved_) return;

  for (size_t i = 0; i < count_; ++i) {
    Service& s = *order_[i];
    if (s.blocked_) continue;
    const bool allowed = modeAllowed(s, mode) && depsRunning(s);

    switch (s.state_) {
      case ServiceState::Stopped:
      case ServiceState::Waiting:
        if (allowed) startService(kernel, s, nowMs);
        break;

      case ServiceState::Starting:
        if (!allowed) {
          stopService(s, ServiceState::Waiting);
        } else if (nowMs - s.startedMs_ > kStartTimeoutMs) {
          s.stop();
          s.fail("start timeout");
        }
        break;

      case ServiceState::Ready:
      case ServiceState::Degraded:
        if (!allowed) {
          stopService(s, ServiceState::Waiting);
          s.reason_ = modeAllowed(s, mode) ? "waiting for a dependency" : "not used in this mode";
        } else if (s.info().tickMs && static_cast<int32_t>(nowMs - s.nextTickMs_) >= 0) {
          const uint64_t t0 = clock_.micros();
          s.tick(nowMs);
          const uint32_t spent = static_cast<uint32_t>(clock_.micros() - t0);
          s.stats_.lastTickUs = spent;
          if (spent > s.stats_.maxTickUs) s.stats_.maxTickUs = spent;
          if (spent > static_cast<uint32_t>(LMP_TICK_WARN_MS) * 1000u) {
            LMP_LOGW(kLogSys, "service '%s' tick took %lu ms", s.info().name,
                     static_cast<unsigned long>(spent / 1000));
          }
          s.nextTickMs_ = nowMs + s.info().tickMs;
        }
        break;

      case ServiceState::Error:
        if (s.info().policy == RestartPolicy::Never) break;
        if (s.info().policy == RestartPolicy::Critical && s.attempts_ >= kCriticalAttempts) {
          if (!criticalFailure_) {
            criticalFailure_ = &s;
            LMP_LOGE(kLogSys, "critical service '%s' keeps failing: %s", s.info().name, s.reason_);
          }
          break;
        }
        if (s.retryAtMs_ == 0) {
          scheduleRetry(s, nowMs);
        } else if (allowed && static_cast<int32_t>(nowMs - s.retryAtMs_) >= 0) {
          s.retryAtMs_ = 0;
          startService(kernel, s, nowMs);
        }
        break;
    }

    if (s.state_ != s.published_) {
      s.published_ = s.state_;
      if (s.state_ == ServiceState::Error) {
        LMP_LOGE(kLogSys, "service '%s' error: %s", s.info().name, s.reason_);
      } else {
        LMP_LOGD(kLogSys, "service '%s' %s", s.info().name, serviceStateName(s.state_));
      }
      events_.publish(Event(EventId::ServiceState, static_cast<uint8_t>(s.info().id),
                            static_cast<uint8_t>(s.state_)));
    }
  }
}

bool ServiceRegistry::settled(Mode mode) const {
  if (!resolved_) return false;
  for (size_t i = 0; i < count_; ++i) {
    const Service& s = *order_[i];
    if (s.blocked_ || !modeAllowed(s, mode)) continue;
    if (s.state_ == ServiceState::Starting) return false;
    // Waiting for a dependency that failed is a final state too.
    if ((s.state_ == ServiceState::Waiting || s.state_ == ServiceState::Stopped) &&
        depsRunning(s)) {
      return false;
    }
  }
  return true;
}

void ServiceRegistry::stopAll() {
  for (size_t i = count_; i-- > 0;) {
    Service& s = *order_[i];
    if (s.running() || s.state_ == ServiceState::Starting) stopService(s, ServiceState::Stopped);
  }
}

void ServiceRegistry::deliver(const Event& event) {
  for (size_t i = 0; i < count_; ++i) {
    Service& s = *order_[i];
    if (s.running()) s.onEvent(event);
  }
}

void ServiceRegistry::describe(JsonWriter& w) const {
  w.beginArray();
  for (size_t i = 0; i < count_; ++i) {
    const Service& s = *order_[i];
    const ServiceInfo& info = s.info();
    w.beginObject();
    w.str("name", info.name);
    w.str("icon", info.icon);
    w.str("state", serviceStateName(s.state_));
    w.str("reason", s.reason_);
    w.beginArray("deps");
    for (uint8_t d = 0; d < info.deps.count; ++d) {
      const Service* dep = find(info.deps.ids[d]);
      if (dep) w.str(dep->info().name);
    }
    w.endArray();
    w.u32("tickMs", info.tickMs);
    w.u32("starts", s.stats_.starts);
    w.u32("failures", s.stats_.failures);
    w.u32("tickUs", s.stats_.lastTickUs);
    w.u32("tickMaxUs", s.stats_.maxTickUs);
    w.i64("heap", s.stats_.heapAtStart);
    w.endObject();
  }
  w.endArray();
}

}  // namespace lmp

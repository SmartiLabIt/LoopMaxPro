// LoopMaxPro - base class of every service.
// A service describes itself: state document, commands, dependencies.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <type_traits>

#include "base/status.h"
#include "kernel/event/event.h"
#include "kernel/json/json_writer.h"

namespace lmp {

class Kernel;
class CommandSet;

enum class ServiceId : uint8_t {
  System, Log, Serial, Storage, Config, Gpio, Bus, Clock, Net, Web,
  HttpClient, Mqtt, Ota, Reset, Geo, Modules, Cloud, Lora, Product, Probe,
  Count
};
constexpr size_t kServiceCount = static_cast<size_t>(ServiceId::Count);

enum class ServiceState : uint8_t { Stopped, Waiting, Starting, Ready, Degraded, Error };
const char* serviceStateName(ServiceState state);

// Operating modes of the device. Values can be combined as a mask.
enum class Mode : uint8_t { Ap = 1, Lan = 2, Any = 3 };
const char* modeName(Mode mode);

enum class RestartPolicy : uint8_t {
  Never,     // stays in error
  Retry,     // started again with growing delays
  Critical   // as Retry, and the board restarts when it keeps failing
};

struct DepList {
  static constexpr size_t kMax = 8;
  ServiceId ids[kMax] = {};
  uint8_t count = 0;

  constexpr DepList() = default;
  // Accepts only service ids, so it never competes with the copy constructor.
  template <class... T,
            class = typename std::enable_if<(std::is_same<T, ServiceId>::value && ...)>::type>
  constexpr DepList(ServiceId first, T... rest)
      : ids{first, rest...}, count(static_cast<uint8_t>(1 + sizeof...(T))) {}
};

struct ServiceInfo {
  ServiceId id;
  const char* name;      // short and lower case: it is part of the target "sys.<name>"
  const char* icon;
  Mode modes;
  RestartPolicy policy;
  uint16_t tickMs;       // 0 = never ticked
  DepList deps;
};

struct ServiceStats {
  uint32_t starts = 0;
  uint32_t failures = 0;
  uint32_t lastTickUs = 0;
  uint32_t maxTickUs = 0;
  int32_t heapAtStart = 0;   // bytes of heap the last start took; negative when it gave back
};

class Service {
 public:
  virtual ~Service() = default;

  virtual const ServiceInfo& info() const = 0;

  // Must not block. Ok: the service is ready. Pending: it will call ready() or fail() later.
  virtual Status start(Kernel& kernel) = 0;
  virtual void stop() {}
  virtual void tick(uint32_t nowMs) {}
  virtual void onEvent(const Event& event) {}

  // State document: GET /api/doc/sys.<name> and the matching part of the report.
  // The object is already open: write the members only.
  virtual void describe(JsonWriter& w) const {}
  // Commands of the target sys.<name>.
  virtual void commands(CommandSet& set) {}
  // Name of the part of the Device Report filled by describe(), or null.
  virtual const char* reportPart() const { return nullptr; }
  // Interface offered to other services and to modules, or null.
  virtual void* api() { return nullptr; }

  ServiceState state() const { return state_; }
  const char* reason() const { return reason_; }
  const ServiceStats& stats() const { return stats_; }
  bool running() const {
    return state_ == ServiceState::Ready || state_ == ServiceState::Degraded;
  }

 protected:
  // For services that started with Status::Pending.
  void ready();
  void fail(const char* reason);
  // Partial operation. The reason must be a literal or a text that stays valid.
  void degrade(const char* reason);
  void recover();

 private:
  friend class ServiceRegistry;

  ServiceState state_ = ServiceState::Stopped;
  const char* reason_ = "";
  ServiceStats stats_;
  // Used by the registry.
  ServiceState published_ = ServiceState::Stopped;
  uint32_t nextTickMs_ = 0;
  uint32_t startedMs_ = 0;
  uint32_t retryAtMs_ = 0;
  uint8_t attempts_ = 0;
  bool blocked_ = false;   // a dependency is not registered: it can never start
};

}  // namespace lmp

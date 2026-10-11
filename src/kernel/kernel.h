// LoopMaxPro - the kernel.
// Owns logger, events, commands, settings and the service registry.
// Everything that changes the state of services and modules runs on one context:
// the application task. Other tasks enter it through invoke().
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "base/delegate.h"
#include "base/status.h"
#include "hal/api/board.h"
#include "kernel/command/command.h"
#include "kernel/config/config.h"
#include "kernel/event/event_bus.h"
#include "kernel/json/json_writer.h"
#include "kernel/log/last_words_sink.h"
#include "kernel/log/logger.h"
#include "kernel/service/registry.h"
#include "kernel/service/service.h"
#include "kernel/worker/worker.h"

namespace lmp {

class Kernel {
 public:
  explicit Kernel(const hal::Board& board);

  // ---- composition, before begin()
  Status add(Service& service);

  // Computes the start order and registers documents and commands of every service.
  Status begin();

  // ---- execution
  // One step: pending calls, services, events. Call it in a loop on boards without tasks.
  void tick();
  // Runs tick() until stop() is called. The calling context becomes the application task.
  void run();
  // Starts the application task. On boards without tasks returns Unsupported: call tick().
  Status startTask();
  void stop() { stop_ = true; }
  // Stops the services in reverse order and saves the settings. Does not restart.
  void shutdown();

  // Runs a function on the application task and waits for it.
  // From the application task itself the call is direct.
  Status invoke(Delegate<void()> fn, uint32_t timeoutMs = 2000);
  bool onAppTask() const;

  // ---- parts
  const hal::Board& board() const { return board_; }
  Logger& log() { return log_; }
  EventBus& events() { return events_; }
  CommandBus& commands() { return commands_; }
  Config& config() { return config_; }
  ServiceRegistry& services() { return registry_; }
  // Where the long operations run: requests to a server, downloads.
  Worker& worker() { return worker_; }
  uint32_t now() const { return board_.clock->millis(); }

  // Interface of a service, or null when the service is not in this build.
  // The interface type declares which service offers it:  static constexpr ServiceId kId
  template <class Interface>
  Interface* api() {
    Service* s = registry_.find(Interface::kId);
    return s ? static_cast<Interface*>(s->api()) : nullptr;
  }

  // ---- operating mode
  Mode mode() const { return mode_; }
  void setMode(Mode mode);

  // ---- documents
  // Device Report. parts is a comma separated list of part names; empty means everything.
  // withKey adds the device key: only for authenticated requests and for the cloud.
  void report(JsonWriter& w, StringView parts = StringView(), bool withKey = false);
  // An event as it leaves the device, to the console and to the server:
  // {"id":"svc","a":..,"b":..,"value":..}, with the name and state of the service for "svc".
  void describeEvent(const Event& event, JsonWriter& w);
  // ---- restart
  // The restart happens a moment later, so the answer to the request can leave.
  // The reason goes into the last words of the board, and the next start tells it.
  void requestRestart(const char* reason, bool factoryReset = false, uint32_t delayMs = 800);
  bool restartPending() const { return restartAtMs_ != 0; }
  // A restart because something is stuck and only a restart frees it. The next start writes in
  // the log the last entries before it.
  void guardRestart(const char* why);
  static constexpr const char* kGuardPrefix = "guard: ";
  // Why the previous start ended on purpose, as requestRestart() was told. Empty when the board
  // does not know: a loss of power, a panic, a board without the last words.
  const char* previousWhy() const;

  // Writes in the log how the previous start ended, then begins to keep the last words of this
  // one. Once, at the start, after the first lines about the firmware and the chip.
  void tellPreviousStart();

 private:
  enum class Slot : uint8_t { Idle, Queued, Running };

  void runInvoke();
  void doRestart();
  static void taskEntry(void* arg);

  const hal::Board& board_;
  Logger log_;
  LastWordsSink lastWords_;
  bool toldPrevious_ = false;
  bool guardAsked_ = false;
  EventBus events_;
  CommandBus commands_;
  Config config_;
  ServiceRegistry registry_;
  Worker worker_;

  Mode mode_ = Mode::Ap;
  bool begun_ = false;
  bool bootDone_ = false;
  bool halted_ = false;
  volatile bool stop_ = false;

  // invoke()
  hal::IMutex* invokeMutex_ = nullptr;   // one caller at a time
  hal::IMutex* slotMutex_ = nullptr;
  hal::ISemaphore* doneSem_ = nullptr;
  hal::ISemaphore* wakeSem_ = nullptr;
  Delegate<void()> slotFn_;
  Slot slot_ = Slot::Idle;
  uintptr_t appTask_ = 0;
  bool appTaskKnown_ = false;

  // restart
  uint32_t restartAtMs_ = 0;
  bool restartFactory_ = false;
};

}  // namespace lmp

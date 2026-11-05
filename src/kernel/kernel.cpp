#include "kernel/kernel.h"

#include <stdio.h>
#include <string.h>

#include "kernel/service/product.h"
#include "lmp_version.h"

namespace lmp {
namespace {

constexpr uint32_t kIdleWaitMs = 5;

// True when name is in the comma separated list, or the list is empty.
bool partWanted(StringView list, const char* name) {
  if (list.empty()) return true;
  const StringView wanted(name);
  size_t start = 0;
  while (start <= list.size) {
    size_t end = list.find(',', start);
    if (end == StringView::npos) end = list.size;
    StringView item = list.substr(start, end - start);
    while (!item.empty() && item[0] == ' ') item = item.substr(1);
    while (!item.empty() && item[item.size - 1] == ' ') item = item.substr(0, item.size - 1);
    if (item.equals(wanted)) return true;
    start = end + 1;
  }
  return false;
}

}  // namespace

Kernel::Kernel(const hal::Board& board)
    : board_(board),
      log_(*board.clock, board.rtos ? board.rtos->createMutex() : nullptr),
      events_(board.rtos ? board.rtos->createMutex() : nullptr),
      config_(board.kv, board.rng, board.sys),
      registry_(*board.clock, events_) {
  if (board.rtos) {
    invokeMutex_ = board.rtos->createMutex();
    slotMutex_ = board.rtos->createMutex();
    doneSem_ = board.rtos->createSemaphore();
    wakeSem_ = board.rtos->createSemaphore();
  }
  worker_.begin(board.rtos);
  if (!Logger::instance()) Logger::setInstance(&log_);
  events_.subscribe([this](const Event& e) { registry_.deliver(e); });
}

Status Kernel::add(Service& service) {
  if (begun_) return Status::Denied;
  return registry_.add(service);
}

Status Kernel::begin() {
  if (begun_) return Status::Busy;
  LMP_TRY(registry_.resolve());

  for (size_t i = 0; i < registry_.count(); ++i) {
    Service* s = registry_.at(i);
    uint8_t target = CommandBus::kNoTarget;
    const Status added = commands_.addTarget(
        "sys", s->info().name, [s](JsonWriter& w) { s->describe(w); }, target);
    if (added != Status::Ok) {
      LMP_LOGE(kLogSys, "kernel: cannot register target sys.%s (%s)", s->info().name,
               statusName(added));
      return added;
    }
    CommandSet set = commands_.commandsFor(target);
    s->commands(set);
    if (!set.ok()) {
      LMP_LOGE(kLogSys, "kernel: a command of sys.%s was rejected", s->info().name);
      return Status::NoMem;
    }
  }
  begun_ = true;
  tellPreviousStart();
  if (board_.sys) board_.sys->stallGuard(LMP_STALL_GUARD_S);
  LMP_LOGD(kLogSys, "kernel: %u services, %u targets, %u commands",
           static_cast<unsigned>(registry_.count()), static_cast<unsigned>(commands_.targetCount()),
           static_cast<unsigned>(commands_.commandCount()));
  return Status::Ok;
}

// ---------------------------------------------------------------- execution

void Kernel::tick() {
  if (halted_) return;
  if (!appTaskKnown_ && board_.rtos) {
    appTask_ = board_.rtos->currentTaskId();
    appTaskKnown_ = true;
  }
  const uint32_t nowMs = now();

  runInvoke();
  worker_.poll();
  registry_.tick(*this, nowMs, mode_);
  events_.dispatch();

  if (!bootDone_ && registry_.settled(mode_)) {
    bootDone_ = true;
    log_.markBootDone();
    if (board_.sys) {
      LMP_LOGI(kLogSys, "boot done in %lu ms, heap %lu KB free, lowest %lu KB",
               static_cast<unsigned long>(log_.bootDoneMs()),
               static_cast<unsigned long>(board_.sys->heapFree() / 1024),
               static_cast<unsigned long>(board_.sys->heapMin() / 1024));
    }
  }

  if (registry_.criticalFailure() && !restartPending()) {
    requestRestart("critical service failed", false, 3000);
  }
  if (restartAtMs_ && static_cast<int32_t>(nowMs - restartAtMs_) >= 0) doRestart();

  if (board_.sys) board_.sys->watchdogFeed();
}

void Kernel::run() {
  stop_ = false;
  while (!stop_) {
    tick();
    // Wakes up at once when another task asks for a call.
    if (wakeSem_) {
      wakeSem_->take(kIdleWaitMs);
    } else if (board_.rtos) {
      board_.rtos->delayMs(kIdleWaitMs);
    }
  }
}

void Kernel::taskEntry(void* arg) {
  static_cast<Kernel*>(arg)->run();
  // A task must never return: park it.
  Kernel* self = static_cast<Kernel*>(arg);
  for (;;) self->board_.rtos->delayMs(1000);
}

Status Kernel::startTask() {
  if (!board_.rtos || !board_.rtos->preemptive()) return Status::Unsupported;
  hal::TaskConfig cfg;
  cfg.name = "lmp_app";
  cfg.stackBytes = LMP_APP_STACK;
  cfg.priority = 5;
  cfg.core = board_.sys && board_.sys->info().cores > 1 ? 1 : 0;
  return board_.rtos->createTask(cfg, &Kernel::taskEntry, this);
}

bool Kernel::onAppTask() const {
  if (!board_.rtos || !appTaskKnown_) return true;
  return board_.rtos->currentTaskId() == appTask_;
}

Status Kernel::invoke(Delegate<void()> fn, uint32_t timeoutMs) {
  if (!fn) return Status::Invalid;
  if (onAppTask() || !invokeMutex_) {
    fn();
    return Status::Ok;
  }

  hal::LockGuard oneCaller(invokeMutex_);
  while (doneSem_->take(0)) {
    // Drops a signal left by a call that gave up.
  }
  {
    hal::LockGuard guard(slotMutex_);
    slotFn_ = fn;
    slot_ = Slot::Queued;
  }
  wakeSem_->give();

  if (doneSem_->take(timeoutMs)) return Status::Ok;

  {
    hal::LockGuard guard(slotMutex_);
    if (slot_ == Slot::Queued) {
      slot_ = Slot::Idle;
      slotFn_.reset();
      return Status::Timeout;
    }
  }
  // Already running: the function may use data of the caller, so wait for its end.
  while (!doneSem_->take(1000)) {
  }
  return Status::Ok;
}

void Kernel::runInvoke() {
  if (!slotMutex_) return;
  Delegate<void()> fn;
  {
    hal::LockGuard guard(slotMutex_);
    if (slot_ != Slot::Queued) return;
    slot_ = Slot::Running;
    fn = slotFn_;
  }
  fn();
  {
    hal::LockGuard guard(slotMutex_);
    slot_ = Slot::Idle;
    slotFn_.reset();
  }
  doneSem_->give();
}

// ---------------------------------------------------------------- mode

void Kernel::setMode(Mode mode) {
  if (mode != Mode::Ap && mode != Mode::Lan) return;
  if (mode == mode_) return;
  mode_ = mode;
  LMP_LOGI(kLogSys, "mode %s", modeName(mode));
  events_.publish(Event(EventId::ModeChanged, static_cast<uint8_t>(mode)));
}

// ---------------------------------------------------------------- documents

void Kernel::report(JsonWriter& w, StringView parts, bool withKey) {
  w.beginObject();
  w.u32("v", 2);

  if (partWanted(parts, "device")) {
    w.beginObject("device");
    if (withKey) w.str("key", config_.sys.key.c_str());
    w.str("code", LMP_DEVICE_CODE);
    w.str("name", config_.sys.deviceName.c_str());
    w.str("host", config_.sys.hostname.c_str());
    w.str("board", board_.name);
    w.str("fw", LMP_VERSION);
    w.str("os", LMP_VERSION);
    w.str("build", LMP_BUILD_DATE);
    w.str("lang", config_.sys.lang.c_str());
    w.str("mode", modeName(mode_));
    // What the server compares to know whether an update of the program alone fits.
    if (board_.sys && board_.sys->layout()[0]) {
      w.str("layout", board_.sys->layout());
      w.str("base", board_.sys->base());
    }
    // What the service of the product adds of its own, in a firmware that has one.
    if (const IProduct* product = api<IProduct>()) product->describeDevice(w);
    w.endObject();
  }

  for (size_t i = 0; i < registry_.count(); ++i) {
    const Service* s = registry_.at(i);
    const char* part = s->reportPart();
    if (!part || !partWanted(parts, part)) continue;
    w.beginObject(part);
    s->describe(w);
    w.endObject();
  }

  if (partWanted(parts, "services")) {
    w.key("services");
    registry_.describe(w);
  }
  w.endObject();
}

void Kernel::describeEvent(const Event& event, JsonWriter& w) {
  w.beginObject();
  w.str("id", eventName(event.id));
  w.u32("a", event.a);
  w.u32("b", event.b);
  w.u32("value", event.value);
  if (event.id == EventId::ServiceState) {
    const Service* s = registry_.find(static_cast<ServiceId>(event.a));
    if (s) {
      w.str("service", s->info().name);
      w.str("state", serviceStateName(static_cast<ServiceState>(event.b)));
    }
  }
  w.endObject();
}

// ---------------------------------------------------------------- restart

void Kernel::requestRestart(const char* reason, bool factoryReset, uint32_t delayMs) {
  LMP_LOGW(kLogSys, "%s requested: %s", factoryReset ? "factory reset" : "restart",
           reason ? reason : "");
  // A guard that asked first keeps its reason: it is what the next start must tell.
  if (board_.sys && !guardAsked_) board_.sys->lastWordsWhy(reason ? reason : "");
  restartFactory_ = restartFactory_ || factoryReset;
  restartAtMs_ = now() + delayMs;
  if (restartAtMs_ == 0) restartAtMs_ = 1;
  events_.publish(Event(EventId::ResetRequested, factoryReset ? 1 : 0));
}

void Kernel::guardRestart(const char* why) {
  if (guardAsked_) return;
  char reason[sizeof(hal::LastWords::why)];
  snprintf(reason, sizeof(reason), "%s%s", kGuardPrefix, why ? why : "");
  LMP_LOGE(kLogSys, "%s", reason);
  requestRestart(reason, false, 1000);
  guardAsked_ = true;
}

const char* Kernel::previousWhy() const {
  const hal::LastWords* words = board_.sys ? board_.sys->lastWordsPrevious() : nullptr;
  return words ? words->why : "";
}

void Kernel::tellPreviousStart() {
  if (toldPrevious_) return;
  toldPrevious_ = true;
  hal::ISystem* sys = board_.sys;
  if (!sys) return;
  const hal::LastWords* words = sys->lastWordsPrevious();
  if (words) {
    LMP_LOGI(kLogSys, "previous start ended after %lu s%s%s",
             static_cast<unsigned long>(words->uptimeS), words->why[0] ? ": " : "", words->why);
    // A restart asked by somebody has told its reason. The other ones - a panic, a watchdog, a
    // guard, the reset button pressed because the board did not answer - show what came before.
    const bool asked = sys->resetReason() == hal::ResetReason::Software && words->why[0] &&
                       strncmp(words->why, kGuardPrefix, strlen(kGuardPrefix)) != 0;
    if (!asked) {
      for (uint8_t i = 0; i < words->count; ++i) {
        LMP_LOGI(kLogSys, "  before: %s", words->lines[i]);
      }
    }
  }
  lastWords_.setSystem(sys);
  log_.addSink(&lastWords_);
}

void Kernel::shutdown() {
  registry_.stopAll();
  if (config_.dirty()) config_.save();
  if (board_.serial) board_.serial->flush();
}

void Kernel::doRestart() {
  restartAtMs_ = 0;
  registry_.stopAll();
  if (restartFactory_) {
    config_.factoryReset();
    restartFactory_ = false;
  } else if (config_.dirty()) {
    config_.save();
  }
  if (board_.serial) board_.serial->flush();
  // On a real board restart() does not return. Where it does, nothing runs after it.
  halted_ = true;
  if (board_.sys) board_.sys->restart();
}

}  // namespace lmp

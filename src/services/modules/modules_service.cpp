#include "services/modules/modules_service.h"

#include "hal/api/memory.h"

#if LMP_FEATURE_MODULES

#include <stdio.h>
#include <string.h>

#include <new>

#include "sdk/state.h"
#include "services/clock/clock_service.h"
#include "services/gpio/gpio_service.h"
#include "services/storage/storage_service.h"
#if LMP_FEATURE_BUS
#include "services/bus/bus_service.h"
#endif
#if LMP_FEATURE_NET
#include "services/net/net_service.h"
#endif
#if LMP_FEATURE_HTTPCLIENT
#include "services/httpclient/httpclient_service.h"
#endif
#if LMP_FEATURE_PROBE
#include "services/probe/probe_service.h"
#endif
#if LMP_FEATURE_MQTT
#include "services/mqtt/mqtt_service.h"
#endif
#if LMP_FEATURE_GEO
#include "services/geo/geo_service.h"
#endif
#include "kernel/service/product.h"

#define LMP_MODULE_JOIN_(a, b) a##b
#define LMP_MODULE_JOIN(a, b) LMP_MODULE_JOIN_(a, b)

// Every module gives the system a function with its name: lmp_module_<name>.
#define LMP_MODULE_SLOT(name) ::lmp::sdk::Module& LMP_MODULE_JOIN(lmp_module_, name)();
#include "services/modules/module_slots.h"
#undef LMP_MODULE_SLOT

namespace lmp {
namespace {

constexpr const char* kFolder = "/cfg/m";
constexpr uint16_t kFileVersion = 1;
constexpr ArgSpec kModuleArgs[] = {
    Arg::str("module", 1, 24),
};

#if LMP_FEATURE_THINGS
constexpr ArgSpec kThingsArgs[] = {
    Arg::u16("from").optional(),
};

constexpr ArgSpec kThingValuesArgs[] = {
    Arg::u32("since").optional(),
    Arg::u16("from").optional(),
};

// value: "ON", "OFF", "PRESS". A light takes brightness, from 1 to 100, and its colour too.
constexpr ArgSpec kThingSetArgs[] = {
    Arg::str("thing", 1, 40),
    Arg::str("value", 0, 8).optional(),
    Arg::u8("brightness", 1, 100).optional(),
    Arg::u8("r").optional(),
    Arg::u8("g").optional(),
    Arg::u8("b").optional(),
};
#endif

using ModuleGetter = sdk::Module& (*)();

// One entry more than the modules: an array cannot be empty.
const ModuleGetter kBuilt[] = {
#define LMP_MODULE_SLOT(name) &::LMP_MODULE_JOIN(lmp_module_, name),
#include "services/modules/module_slots.h"
#undef LMP_MODULE_SLOT
    nullptr,
};

// Short and lower case, letters digits and underscore: it goes in addresses and file names.
bool validName(const char* name) {
  if (!name || !*name || strlen(name) > 24) return false;
  for (const char* p = name; *p; ++p) {
    const bool ok = (*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '_';
    if (!ok) return false;
  }
  return true;
}

}  // namespace

const ServiceInfo& ModulesService::info() const {
  static const ServiceInfo kInfo{ServiceId::Modules, "modules", "\xF0\x9F\xA7\xA9", Mode::Any,
                                 RestartPolicy::Retry, 20,
                                 DepList(ServiceId::Config, ServiceId::Gpio,
#if LMP_FEATURE_BUS
                                         ServiceId::Bus,
#endif
                                         ServiceId::Clock, ServiceId::Storage, ServiceId::Log)};
  // With a service of the product: whether the device is blocked is known before a module
  // takes its pins.
  static const ServiceInfo kAfterProduct{ServiceId::Modules, "modules", "\xF0\x9F\xA7\xA9", Mode::Any,
                                         RestartPolicy::Retry, 20,
                                         DepList(ServiceId::Config, ServiceId::Gpio,
#if LMP_FEATURE_BUS
                                                 ServiceId::Bus,
#endif
                                                 ServiceId::Product, ServiceId::Clock,
                                                 ServiceId::Storage, ServiceId::Log)};
  return afterProduct_ ? kAfterProduct : kInfo;
}

// ---------------------------------------------------------------- the modules of the build

Status ModulesService::add(sdk::Module& module) {
  if (!byHand_) {
    // The first one given by hand sends away the ones of the build.
    count_ = 0;
    byHand_ = true;
    collected_ = true;
  }
  if (count_ >= LMP_MAX_MODULES) return Status::NoMem;
  if (!validName(module.manifest().name)) return Status::Invalid;
  for (size_t i = 0; i < count_; ++i) {
    if (strcmp(slots_[i].module->manifest().name, module.manifest().name) == 0) {
      return Status::Busy;
    }
  }
  slots_[count_] = Slot();
  slots_[count_].module = &module;
  ++count_;
  return Status::Ok;
}

void ModulesService::collect() {
  if (collected_) return;
  collected_ = true;
  for (const ModuleGetter get : kBuilt) {
    if (!get) break;
    sdk::Module& m = get();
    if (count_ >= LMP_MAX_MODULES) {
      LMP_LOGE(src_, "module %s left out: no room", m.manifest().name);
      continue;
    }
    if (!validName(m.manifest().name)) {
      LMP_LOGE(src_, "module left out: its name is not valid");
      continue;
    }
    slots_[count_] = Slot();
    slots_[count_].module = &m;
    ++count_;
  }
}

int ModulesService::indexOf(StringView name) const {
  for (size_t i = 0; i < count_; ++i) {
    if (name == StringView(slots_[i].module->manifest().name)) return static_cast<int>(i);
  }
  return -1;
}

sdk::Module* ModulesService::find(const char* name) {
  const int i = name ? indexOf(StringView(name)) : -1;
  return i < 0 ? nullptr : slots_[i].module;
}

sdk::Module* ModulesService::httpOwner(StringView word) {
  if (word.empty()) return nullptr;
  for (size_t i = 0; i < count_; ++i) {
    const char* own = slots_[i].module->httpPrefix();
    if (own && word == StringView(own) && works(slots_[i])) return slots_[i].module;
  }
  return nullptr;
}

// ---------------------------------------------------------------- life cycle

ModulesService::~ModulesService() {
  settleWrite();
  for (size_t i = count_; i > 0; --i) {
    Slot& slot = slots_[i - 1];
    release(slot);
    set(*slot.module, sdk::ModuleState::Stopped, "");
    forget(*slot.module);
    attach(*slot.module, nullptr);
    delete slot.commands;
  }
  hal::freeLarge(file_);
  delete[] pending_;
}

Status ModulesService::start(Kernel& kernel) {
  kernel_ = &kernel;
  src_ = kernel.log().addSource(info().name, info().icon);
  collect();
  // A copy for the worker only where the worker has a task of its own: on the boards without
  // tasks the file is written in the round anyway, and 2 KB matter there.
  hal::IRtos* rtos = kernel.board().rtos;
  fileKept_ = rtos && rtos->preemptive();
  if (fileKept_ && !file_ && count_) file_ = static_cast<char*>(hal::allocLarge(LMP_MODULE_CFG_MAX));
  if (!pending_ && count_ && fileKept_) {
    pending_ = new (std::nothrow) char[LMP_MODULE_CFG_MAX];
  }

  bool failed = false;
  for (size_t i = 0; i < count_; ++i) {
    if (held(slots_[i])) {
      // Nothing of it starts: no pin is taken, no output moves.
      set(*slots_[i].module, sdk::ModuleState::Stopped, blockReason());
      LMP_LOGW(src_, "%s not started: the %s is over", slots_[i].module->manifest().name,
               blockReason());
      continue;
    }
    if (bring(slots_[i]) != Status::Ok) failed = true;
  }
  if (failed) degrade("a module did not start");
  if (count_) LMP_LOGI(src_, "%u modules", static_cast<unsigned>(count_));
  return Status::Ok;
}

void ModulesService::stop() {
  settleWrite();
  for (size_t i = count_; i > 0; --i) end(slots_[i - 1]);
}

// Target, commands, settings: everything a module needs before the network.
Status ModulesService::bring(Slot& slot) {
  sdk::Module& m = *slot.module;
  const sdk::ModuleManifest& manifest = m.manifest();
  CommandBus& bus = kernel_->commands();

  if (slot.target == CommandBus::kNoTarget) {
    const Slot* told = &slot;
    const Status added = bus.addTarget(
        "mod", manifest.name,
        [told](JsonWriter& w) {
          told->module->describeAll(w);
          describeThings(*told, w);
        },
        slot.target);
    if (added != Status::Ok) {
      set(m, sdk::ModuleState::Error, "no room for its target");
      LMP_LOGE(src_, "%s: target not registered (%s)", manifest.name, statusName(added));
      return added;
    }
    slot.commands = new (std::nothrow) CommandSet(bus, slot.target);
    if (!slot.commands) return Status::NoMem;
    const Slot* self = &slot;
    bus.setAbout(slot.target, [self](JsonWriter& w) {
      const sdk::ModuleManifest& about = self->module->manifest();
      w.beginObject("module");
      w.str("title", about.title);
      w.str("icon", about.icon);
      w.str("kind", sdk::moduleKindName(about.kind));
      w.str("version", about.version);
      w.str("code", about.code);
      w.endObject();
      // The console builds the form of the settings from this.
      if (self->module->settings()) {
        w.key("config");
        self->module->settings()->schema(w);
      }
    });
  } else {
    // A module that starts again declares its commands again.
    bus.clearCommands(slot.target);
    new (slot.commands) CommandSet(bus, slot.target);
  }
  addThingCommands(slot);

  slot.ctx = sdk::ModuleContext();
  slot.ctx.kernel = kernel_;
  slot.ctx.manifest = &manifest;
  slot.ctx.log = kernel_->log().addSource(manifest.name, manifest.icon);
  slot.ctx.commands = slot.commands;
  slot.ctx.gpio = kernel_->api<IGpio>();
  slot.ctx.clock = kernel_->api<IClockService>();
  slot.ctx.storage = kernel_->api<IStorage>();
  slot.ctx.modules = this;
#if LMP_FEATURE_BUS
  slot.ctx.bus = kernel_->api<IBus>();
#endif
#if LMP_FEATURE_NET
  slot.ctx.net = kernel_->api<INetService>();
#endif
#if LMP_FEATURE_HTTPCLIENT
  slot.ctx.http = kernel_->api<IHttpClientService>();
#if LMP_FEATURE_PROBE
  slot.ctx.probe = kernel_->api<IProbeService>();
#endif
#endif
#if LMP_FEATURE_MQTT
  slot.ctx.mqtt = kernel_->api<IMqttService>();
#endif
#if LMP_FEATURE_GEO
  slot.ctx.geo = kernel_->api<IGeoService>();
#endif

  forget(m);
  attach(m, &slot.ctx);
  slot.live = true;
  slot.started = false;
  slot.dataDue = slot.fileDue = false;
  slot.told = sdk::ModuleState::Stopped;
  set(m, sdk::ModuleState::Init, "");

  const Status s = m.onInit(slot.ctx);
  if (s != Status::Ok || !slot.commands->ok()) {
    const bool commands = s == Status::Ok;
    set(m, sdk::ModuleState::Error, commands ? "no room for its commands" : "init failed");
    LMP_LOGE(src_, "%s: %s (%s)", manifest.name, m.reason(), statusName(s));
    release(slot);
    return commands ? Status::NoMem : s;
  }
  readFile(slot);
  // What the file changed is the state the module starts with, not news to write.
  takeUnsaved(m);
  return Status::Ok;
}

// The service of the product, in a firmware that has one: it says whether the device is blocked
// and the word for it, which the logs and the state of a module carry as it gives it.
bool ModulesService::blocked() const {
  const IProduct* product = kernel_->api<IProduct>();
  return product && product->blocked();
}

const char* ModulesService::blockReason() const {
  const IProduct* product = kernel_->api<IProduct>();
  return product ? product->reason() : "";
}

bool ModulesService::held(const Slot& slot) const {
  return blocked() && slot.module->manifest().kind != sdk::ModuleKind::Bridge &&
         !slot.module->reducedWhenBlocked();
}

// A module that keeps a reduced form: told, and shown in its state.
void ModulesService::tellBlock(Slot& slot) {
  sdk::Module& m = *slot.module;
  if (!m.reducedWhenBlocked() || !slot.started) return;
  const sdk::ModuleState s = m.state();
  if (s != sdk::ModuleState::Ready && s != sdk::ModuleState::Degraded) return;
  const char* why = blockReason();
  if (!why[0]) return;   // no service of the product: nothing blocks
  const bool now = blocked();
  const bool told = strcmp(m.reason(), why) == 0;
  if (now == told) return;
  m.onBlocked(now);
  set(m, now ? sdk::ModuleState::Degraded : sdk::ModuleState::Ready, now ? why : "");
  if (now) {
    LMP_LOGW(src_, "%s: the %s is over: reduced to telling it", m.manifest().name, why);
  } else {
    LMP_LOGW(src_, "%s: the %s is good: back to work", m.manifest().name, why);
  }
}

void ModulesService::followBlock() {
  const char* why = blockReason();
  if (!why[0]) return;
  for (size_t i = 0; i < count_; ++i) {
    Slot& slot = slots_[i];
    sdk::Module& m = *slot.module;
    if (m.reducedWhenBlocked()) {
      tellBlock(slot);
      continue;
    }
    const bool stopped = m.state() == sdk::ModuleState::Stopped && strcmp(m.reason(), why) == 0;
    if (held(slot)) {
      if (stopped) continue;
      // Its outputs go to the safe state, as at any stop; its commands answer no more.
      end(slot);
      if (slot.target != CommandBus::kNoTarget) kernel_->commands().clearCommands(slot.target);
      set(m, sdk::ModuleState::Stopped, why);
      LMP_LOGW(src_, "%s stopped: the %s is over", m.manifest().name, why);
    } else if (stopped) {
      // At once, without a restart: the network is there already, the tick starts it.
      LMP_LOGI(src_, "%s starts again: the %s is good", m.manifest().name, why);
      if (bring(slot) != Status::Ok) degrade("a module did not start");
    }
  }
}

bool ModulesService::networkReady() const {
#if LMP_FEATURE_NET
  const Service* net = kernel_->services().find(ServiceId::Net);
  // A board without network starts its modules all the same.
  return !net || net->running() || net->state() == ServiceState::Error;
#else
  return true;
#endif
}

void ModulesService::begin(Slot& slot, uint32_t nowMs) {
  sdk::Module& m = *slot.module;
  slot.started = true;
  const Status s = m.onStart();
  if (s != Status::Ok) {
    set(m, sdk::ModuleState::Error, "start failed");
    LMP_LOGE(src_, "%s: start failed (%s)", m.manifest().name, statusName(s));
    // A module that does not work holds nothing: pins and buses are free for the others.
    release(slot);
    degrade("a module did not start");
    return;
  }
  // The module may have said by itself that it works in part.
  if (m.state() == sdk::ModuleState::Init) set(m, sdk::ModuleState::Ready, "");
  slot.nextTickMs = nowMs;
  // Started with the device blocked: in its reduced form from the first moment.
  tellBlock(slot);
#if LMP_FEATURE_THINGS
  slot.things.look(m);
#endif
  LMP_LOGI(src_, "%s %s: %s", m.manifest().name, m.manifest().version,
           sdk::moduleStateName(m.state()));
}

void ModulesService::release(Slot& slot) {
#if LMP_FEATURE_THINGS
  slot.things.forget();
#endif
  if (!slot.live) return;
  slot.live = false;
  slot.module->stopAll();
}

// ---------------------------------------------------------------- the things, as data

bool ModulesService::works(const Slot& slot) {
  const sdk::ModuleState s = slot.module->state();
  return slot.started && (s == sdk::ModuleState::Ready || s == sdk::ModuleState::Degraded);
}

void ModulesService::describeThings(const Slot& slot, JsonWriter& w) {
#if LMP_FEATURE_THINGS
  if (!works(slot) || !slot.things.any()) return;
  w.beginObject("things");
  w.u32("gen", slot.things.gen());
  w.u32("rev", slot.things.rev());
  w.beginObject("values");
  sdk::thingsOwnValues(*slot.module, w);
  w.endObject();
  if (slot.things.parts()) w.u32("parts", slot.things.parts());
  w.endObject();
#else
  (void)slot;
  (void)w;
#endif
}

void ModulesService::addThingCommands(Slot& slot) {
#if LMP_FEATURE_THINGS
  const Slot* self = &slot;
  slot.commands->add(
      "things",
      LMP_HELP("What the module shows and lets command: its things, and its parts a page at a time"),
      kThingsArgs, [self](const Args& a, CmdContext& ctx) -> Status {
        if (!works(*self)) return Status::Denied;
        if (ctx.result) sdk::thingsDescribe(*self->module, self->things, a.u16("from"), *ctx.result);
        return Status::Ok;
      });
  slot.commands->add(
      "things.values",
      LMP_HELP("The values of the parts of the module changed since a rev, a page at a time"),
      kThingValuesArgs, [self](const Args& a, CmdContext& ctx) -> Status {
        if (!works(*self)) return Status::Denied;
        if (ctx.result) {
          sdk::thingsPartValues(*self->module, self->things, a.u32("since"), a.u16("from"),
                                *ctx.result);
        }
        return Status::Ok;
      });
  slot.commands->add(
      "thing.set", LMP_HELP("Turns, presses or sets one of the things of the module"),
      kThingSetArgs, [self](const Args& a, CmdContext& ctx) -> Status {
        if (!works(*self)) return Status::Denied;
        const Status s = sdk::thingsSet(*self->module, a);
        if (s == Status::NotFound) ctx.errorField = "thing";
        return s;
      });
#else
  (void)slot;
#endif
}

void ModulesService::end(Slot& slot) {
  sdk::Module& m = *slot.module;
  if (slot.live) {
    // Written now: a restart must find what was changed a moment ago.
    if (slot.fileDue || takeUnsaved(m)) {
      settleWrite();
      writeFile(slot, true);
    }
    release(slot);
  }
  slot.fileDue = slot.dataDue = false;
  set(m, sdk::ModuleState::Stopped, "");
  slot.started = false;
}

void ModulesService::tick(uint32_t nowMs) {
  writeDone();
  const bool network = networkReady();
  for (size_t i = 0; i < count_; ++i) {
    Slot& slot = slots_[i];
    sdk::Module& m = *slot.module;
    const bool alive =
        m.state() == sdk::ModuleState::Ready || m.state() == sdk::ModuleState::Degraded;

    if (m.state() == sdk::ModuleState::Init && !slot.started && network) begin(slot, nowMs);

    const uint16_t every = m.manifest().tickMs;
    if (alive && slot.started && every &&
        static_cast<int32_t>(nowMs - slot.nextTickMs) >= 0) {
      slot.nextTickMs = nowMs + every;
      m.tickAll(nowMs);
    }
    follow(slot, i, nowMs);
  }
}

// What a module changed is told and written a moment later: changes close to each other
// become one.
void ModulesService::follow(Slot& slot, size_t index, uint32_t nowMs) {
  sdk::Module& m = *slot.module;
  const uint8_t id = static_cast<uint8_t>(index);

  if (m.state() != slot.told) {
    slot.told = m.state();
    kernel_->events().publish(
        Event(EventId::ModuleState, id, static_cast<uint8_t>(m.state())));
  }
  if (takeUnsaved(m) && !slot.fileDue) {
    slot.fileDue = true;
    slot.fileAtMs = nowMs + LMP_CONFIG_SAVE_DELAY_MS;
  }
  if (takeChanged(m) && !slot.dataDue) {
    slot.dataDue = true;
    slot.dataAtMs = nowMs + LMP_MODULE_DATA_DELAY_MS;
  }
  if (slot.dataDue && static_cast<int32_t>(nowMs - slot.dataAtMs) >= 0) {
    slot.dataDue = false;
#if LMP_FEATURE_THINGS
    // Before the news goes out: who reads the document finds the things as they are now.
    if (works(slot)) slot.things.look(m);
#endif
    kernel_->events().publish(Event(EventId::ModuleData, id));
  }
  if (slot.fileDue && static_cast<int32_t>(nowMs - slot.fileAtMs) >= 0) {
    // The worker is still writing another file: this one waits for the next round.
    if (writeFile(slot) != Status::Busy) slot.fileDue = false;
  }
}

void ModulesService::onEvent(const Event& event) {
  if (!kernel_) return;
  // Before a restart the files are written, whatever their wait was.
  if (event.id == EventId::ResetRequested && event.a == 0) {
    settleWrite();
    for (size_t i = 0; i < count_; ++i) {
      Slot& slot = slots_[i];
      if (slot.fileDue || takeUnsaved(*slot.module)) writeFile(slot, true);
      slot.fileDue = false;
    }
  }
  // A factory reset takes the files away: what is in memory must not come back on them.
  if (event.id == EventId::ResetRequested && event.a == 1) {
    // A file the worker is writing must not appear again after the files are gone.
    cancelWrite_ = true;
    settleWrite();
    cancelWrite_ = false;
    IStorage* storage = kernel_->api<IStorage>();
    if (storage) storage->removeFiles(kFolder);
    for (size_t i = 0; i < count_; ++i) {
      slots_[i].fileDue = false;
      takeUnsaved(*slots_[i].module);
      set(*slots_[i].module, sdk::ModuleState::Stopped, "");
    }
    return;
  }
  if (event.id == EventId::ProductChanged) followBlock();
  for (size_t i = 0; i < count_; ++i) {
    sdk::Module& m = *slots_[i].module;
    if (m.state() == sdk::ModuleState::Ready || m.state() == sdk::ModuleState::Degraded) {
      m.eventAll(event);
    }
  }
}

// ---------------------------------------------------------------- the file of a module

char* ModulesService::takeFile() {
  if (!file_) file_ = static_cast<char*>(hal::allocLarge(LMP_MODULE_CFG_MAX));
  return file_;
}

void ModulesService::giveFile() {
  if (fileKept_) return;
  hal::freeLarge(file_);
  file_ = nullptr;
}

void ModulesService::readFile(Slot& slot) {
  IStorage* storage = slot.ctx.storage;
  if (!storage) return;
  if (!takeFile()) {
    LMP_LOGE(src_, "%s: no memory to read the settings", slot.module->manifest().name);
    return;
  }
  struct Give {
    ModulesService* self;
    ~Give() { self->giveFile(); }
  } give{this};
  const char* name = slot.module->manifest().name;
  char path[48];
  snprintf(path, sizeof(path), "%s/%s.json", kFolder, name);
  size_t length = 0;
  const Status s = storage->readFile(path, file_, LMP_MODULE_CFG_MAX, length);
  if (s == Status::NotFound) return;   // a module that never saved anything
  if (s != Status::Ok) {
    LMP_LOGW(src_, "%s: settings not read (%s)", name, statusName(s));
    return;
  }
  const JsonValue doc = JsonValue::parse(StringView(file_, length));
  if (!doc.isObject()) {
    LMP_LOGW(src_, "%s: settings not readable, the module starts with its own", name);
    return;
  }
  slot.module->loadAll(doc);
}

Status ModulesService::writeFile(Slot& slot, bool now) {
  IStorage* storage = slot.ctx.storage;
  const char* name = slot.module->manifest().name;
  if (!storage) return Status::Unsupported;
  if (!now && writing_) return Status::Busy;
  if (!takeFile()) return Status::Busy;   // no memory now: tried again later
  struct Give {
    ModulesService* self;
    ~Give() { self->giveFile(); }
  } give{this};

  BufferSink sink(file_, LMP_MODULE_CFG_MAX);
  JsonWriter w(sink);
  w.beginObject();
  w.u32("v", kFileVersion);
  slot.module->saveAll(w);
  w.endObject();
  if (!w.complete()) {
    LMP_LOGE(src_, "%s: settings not saved, longer than %u bytes", name,
             static_cast<unsigned>(LMP_MODULE_CFG_MAX));
    return Status::NoMem;
  }
  char path[48];
  snprintf(path, sizeof(path), "%s/%s.json", kFolder, name);
  if (!now && pending_) {
    // The worker writes a copy: the text is ready, only the flash is slow.
    memcpy(pending_, sink.view().data, sink.size());
    pendingLength_ = sink.size();
    snprintf(pendingPath_, sizeof(pendingPath_), "%s", path);
    pendingStorage_ = storage;
    writing_ = true;
    const Status posted = kernel_->worker().post([this]() {
      if (!cancelWrite_) {
        lastWrite_ = pendingStorage_->writeFile(pendingPath_, StringView(pending_, pendingLength_));
      }
      writing_ = false;
    });
    if (posted == Status::Ok) return Status::Ok;
    writing_ = false;   // the queue of the worker is full: written here
  }
  const Status s = storage->writeFile(path, sink.view());
  if (s != Status::Ok) LMP_LOGE(src_, "%s: settings not saved (%s)", name, statusName(s));
  return s;
}

// The outcome of the last write of the worker, told on the application task.
void ModulesService::writeDone() {
  if (writing_ || !pendingPath_[0]) return;
  const Status s = lastWrite_;
  if (s != Status::Ok) {
    LMP_LOGE(src_, "%s not saved (%s)", pendingPath_, statusName(s));
    lastWrite_ = Status::Ok;
  }
  pendingPath_[0] = '\0';
}

void ModulesService::settleWrite() {
  if (!writing_ || !kernel_) return;
  Worker& worker = kernel_->worker();
  if (!worker.threaded()) {
    // The job waits in the queue of the round, maybe behind others: they run now.
    for (int i = 0; i <= LMP_WORKER_QUEUE && writing_; ++i) worker.poll();
  } else {
    hal::IRtos* rtos = kernel_->board().rtos;
    // A file of a few hundred bytes: well under a second, even on a slow flash.
    for (int i = 0; i < 400 && writing_; ++i) {
      if (rtos) rtos->delayMs(5);
    }
  }
  writeDone();
}

// ---------------------------------------------------------------- for the other parts

Status ModulesService::command(const char* module, const char* name, const JsonValue& args) {
  if (!kernel_ || !module || !name) return Status::Invalid;
  char target[40];
  snprintf(target, sizeof(target), "mod.%s", module);
  CmdContext ctx;
  ctx.source = CmdSource::Internal;
  return kernel_->commands().execute(StringView(target), StringView(name), args, ctx);
}

void ModulesService::writeEntry(JsonWriter& w, const Slot& slot, bool withState) const {
  const sdk::ModuleManifest& m = slot.module->manifest();
  w.beginObject();
  w.str("name", m.name);
  w.str("title", m.title);
  w.str("icon", m.icon);
  w.str("kind", sdk::moduleKindName(m.kind));
  w.str("version", m.version);
  if (m.ui && *m.ui) w.str("ui", m.ui);
  if (withState) {
    w.str("code", m.code);
    w.str("state", sdk::moduleStateName(slot.module->state()));
    w.str("reason", slot.module->reason());
    w.u32("tickMs", m.tickMs);
    w.boolean("settings", slot.module->settings() != nullptr);
  }
  w.endObject();
}

void ModulesService::listPublic(JsonWriter& w) const {
  w.beginArray();
  for (size_t i = 0; i < count_; ++i) writeEntry(w, slots_[i], false);
  w.endArray();
}

Status ModulesService::describeConfig(StringView name, JsonWriter& w) const {
  const int i = indexOf(name);
  if (i < 0) return Status::NotFound;
  const sdk::SettingsBase* settings = slots_[i].module->settings();
  if (settings) settings->describeConfig(w);
  return Status::Ok;
}

// The part "modules" of the report is this list: the object is already open.
void ModulesService::describe(JsonWriter& w) const {
  w.u32("count", static_cast<uint32_t>(count_));
  w.u32("max", LMP_MAX_MODULES);
  w.beginArray("list");
  for (size_t i = 0; i < count_; ++i) writeEntry(w, slots_[i], true);
  w.endArray();
}

void ModulesService::commands(CommandSet& set) {
  set.add("restart", LMP_HELP("Stops a module and starts it again"), kModuleArgs,
          [this](const Args& a, CmdContext& ctx) -> Status {
            const int i = indexOf(StringView(a.str("module")));
            if (i < 0) {
              ctx.errorField = "module";
              return Status::NotFound;
            }
            Slot& slot = slots_[i];
            // Stopped by the block of the product: only its end starts it again.
            if (held(slot)) {
              ctx.errorField = blockReason();
              return Status::Denied;
            }
            end(slot);
            const Status s = bring(slot);
            if (s != Status::Ok) return s;
            if (networkReady()) begin(slot, kernel_->now());
            return slot.module->state() == sdk::ModuleState::Error ? Status::Error : Status::Ok;
          },
          kCmdConfirm);
}

}  // namespace lmp

#endif  // LMP_FEATURE_MODULES

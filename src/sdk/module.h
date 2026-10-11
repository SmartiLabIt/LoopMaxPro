// LoopMaxPro SDK - the module: the part that gives a device its purpose.
// A module declares what it is, keeps its state in native structs and offers commands. The
// system gives it documents, settings on file, HTTP, MQTT, the serial command line and the
// cloud: a module never writes a route, a topic or a line of JSON by hand.
//
//   class Meteo final : public lmp::sdk::Module {
//     LMP_MODULE("meteo", "Meteo", "\xF0\x9F\x8C\xA4", "1.0.0", lmp::sdk::ModuleKind::Online, 1000)
//     lmp::Status onInit(lmp::sdk::ModuleContext& c) override { ... }
//   };
//   LMP_REGISTER_MODULE(meteo, Meteo)
//
// Everything of a module runs on the application task: no locks are needed.
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "base/status.h"
#include "kernel/command/command.h"
#include "kernel/event/event.h"
#include "kernel/json/json_reader.h"
#include "kernel/json/json_writer.h"
#include "kernel/log/logger.h"
#include "lmp_config.h"
#include "sdk/home.h"

namespace lmp {

class Kernel;
class IGpio;
class IBus;
class IClockService;
class INetService;
class IStorage;
class IHttpClientService;
class IProbeService;
class IMqttService;
class IGeoService;

namespace sdk {

class Module;
class SettingsBase;
struct HttpCall;

enum class ModuleKind : uint8_t {
  Hardware,   // pins, PWM, analog inputs
  Bridge,     // talks to hardware outside the board, or to other devices
  Online,     // uses services on the network
  Ui,         // lives in the browser: no code on the device
  Headless    // logic without an interface
};
const char* moduleKindName(ModuleKind kind);

enum class ModuleState : uint8_t { Stopped, Init, Ready, Degraded, Error };
const char* moduleStateName(ModuleState state);

struct ModuleManifest {
  const char* name;      // short and lower case: it is part of the target "mod.<name>"
  const char* title;
  const char* icon;
  const char* version;
  ModuleKind kind;
  uint16_t tickMs;       // 0 = never ticked
  const char* code;      // kind of device, as the server knows it. Empty without one
  const char* ui;        // file of the interface, under the folder of the module. Empty without
};

// The other modules of the device.
class IModules {
 public:
  virtual ~IModules() = default;
  virtual size_t count() const = 0;
  virtual Module* find(const char* name) = 0;
  // Runs a command of another module, as if it came from inside the device.
  virtual Status command(const char* module, const char* name, const JsonValue& args) = 0;
};

// What a module finds when it starts. A pointer is null when the build, or the board, does
// not have that part.
struct ModuleContext {
  Kernel* kernel = nullptr;
  const ModuleManifest* manifest = nullptr;
  LogSource log = kLogSys;
  CommandSet* commands = nullptr;   // commands of the target mod.<name>
  IGpio* gpio = nullptr;
  IBus* bus = nullptr;
  IClockService* clock = nullptr;
  INetService* net = nullptr;
  IStorage* storage = nullptr;
  IHttpClientService* http = nullptr;
  // Pings, ports, who is in the network, the packet that wakes a PC (services/probe).
  IProbeService* probe = nullptr;
  IMqttService* mqtt = nullptr;
  // Where the device is. A module with a GPS writes it with GeoSource::Gps.
  IGeoService* geo = nullptr;
  IModules* modules = nullptr;
};

// A ready made piece of a module: a bank of pins, the schedules, the settings. The module
// says which ones it uses, the rest happens by itself: commands, document, file, ticks.
class Component {
 public:
  virtual ~Component() = default;

  virtual void commands(CommandSet& set) {}
  // Members of the state document of the module.
  virtual void describe(JsonWriter& w) const {}
  // What is kept between two starts: the same members, written and read back.
  virtual void save(JsonWriter& w) const {}
  virtual void load(const JsonValue& doc) {}
  virtual void tick(uint32_t nowMs) {}
  virtual void onEvent(const Event& event) {}
  // The module stops: what was taken from the system is given back.
  virtual void stop() {}
  // What this part shows and lets command to a home system (sdk/home.h). NotFound for a
  // thing that is not of this part.
  virtual void things(ThingSink& sink) const { (void)sink; }
  virtual Status thingSet(const char* id, const ThingAsk& ask) {
    (void)id;
    (void)ask;
    return Status::NotFound;
  }

  // True once after what the outside sees changed, and after what is kept changed.
  bool takeChanged();
  bool takeUnsaved();

 protected:
  void changed() { changed_ = true; }
  void unsaved() { unsaved_ = changed_ = true; }
  ModuleContext* ctx() const { return ctx_; }
  // For the parts that need the system before the module takes them in.
  void bind(ModuleContext& c) { ctx_ = &c; }

 private:
  friend class Module;
  ModuleContext* ctx_ = nullptr;
  bool changed_ = false;
  bool unsaved_ = false;
};

class Module {
 public:
  static constexpr size_t kParts = 6;

  virtual ~Module() = default;

  virtual const ModuleManifest& manifest() const = 0;

  // Pins, buses, commands, parts. The base services are ready, the network may not be.
  virtual Status onInit(ModuleContext& ctx) = 0;
  // The network is ready, as an access point or on a network.
  virtual Status onStart() { return Status::Ok; }
  virtual void onTick(uint32_t nowMs) {}
  virtual void onEvent(const Event& event) {}
  virtual void onStop() {}

  // The service of the product blocks the device (kernel/service/product.h). A module is
  // stopped, as at any stop, unless it keeps a reduced form: then it stays on, is told here,
  // and stops its service by itself: no data, no commands, only what says why (a screen, the
  // devices it speaks to). Told again with false when the block ends.
  virtual bool reducedWhenBlocked() const { return false; }
  virtual void onBlocked(bool blocked) { (void)blocked; }

  // State document. The parts in use write theirs first.
  virtual void describe(JsonWriter& w) const {}
  // What the module itself keeps between two starts, beside its parts.
  virtual void save(JsonWriter& w) const {}
  virtual void load(const JsonValue& doc) {}
  // What the module itself shows and lets command to a home system, beside its parts
  // (sdk/home.h). NotFound for a thing the module does not have.
  virtual void things(ThingSink& sink) const { (void)sink; }
  virtual Status thingSet(const char* id, const ThingAsk& ask) {
    (void)id;
    (void)ask;
    return Status::NotFound;
  }

  // Requests of other programs (sdk/http.h): the word of the paths that are the module's, as
  // "ep" for /ep/...; null, as for nearly every module, for none. onHttp answers on the
  // application task, while the module works.
  virtual const char* httpPrefix() const { return nullptr; }
  virtual void onHttp(HttpCall& call) { (void)call; }

  ModuleState state() const { return state_; }
  const char* reason() const { return reason_; }

  // ---- used by the service that hosts the modules
  void describeAll(JsonWriter& w) const;
  void saveAll(JsonWriter& w) const;
  void loadAll(const JsonValue& doc);
  void tickAll(uint32_t nowMs);
  void eventAll(const Event& event);
  void stopAll();
  // The things of the parts, then the ones of the module; who has the thing takes the ask.
  void thingsAll(ThingSink& sink) const;
  Status thingSetAll(const char* id, const ThingAsk& ask);
  // Settings of the module, when one of its parts holds them. Null without.
  SettingsBase* settings() const { return settings_; }

 protected:
  ModuleContext& ctx() { return *ctx_; }
  // Takes a part in: its commands are registered now, the rest follows the module.
  Status use(Component& part);
  // The part that holds the settings: also what GET /api/cfg/mod.<name> shows.
  Status useSettings(SettingsBase& part);
  // What the outside sees changed: console, server and broker are told.
  void markChanged() { changed_ = true; }
  // What is kept between two starts changed: the file is written a moment later.
  void markUnsaved() { unsaved_ = changed_ = true; }
  // The text must stay valid: use a literal.
  void setState(ModuleState state, const char* reason = nullptr);

 private:
  friend class ModuleHost;

  ModuleContext* ctx_ = nullptr;
  Component* parts_[kParts] = {};
  SettingsBase* settings_ = nullptr;
  uint8_t partCount_ = 0;
  ModuleState state_ = ModuleState::Stopped;
  const char* reason_ = "";
  bool changed_ = false;
  bool unsaved_ = false;
};

// Access for the service that hosts the modules: nothing else touches these.
class ModuleHost {
 protected:
  static void attach(Module& m, ModuleContext* ctx) { m.ctx_ = ctx; }
  static void forget(Module& m) {
    m.partCount_ = 0;
    m.settings_ = nullptr;
    m.changed_ = m.unsaved_ = false;
  }
  static void set(Module& m, ModuleState state, const char* reason) {
    m.state_ = state;
    m.reason_ = reason ? reason : "";
  }
  static bool takeChanged(Module& m);
  static bool takeUnsaved(Module& m);
};

}  // namespace sdk
}  // namespace lmp

// Inside the class: the manifest of the module.
#define LMP_MODULE(name_, title_, icon_, version_, kind_, tickMs_)                       \
 public:                                                                                  \
  const ::lmp::sdk::ModuleManifest& manifest() const override {                           \
    static const ::lmp::sdk::ModuleManifest kManifest{name_, title_, icon_, version_,     \
                                                      kind_, tickMs_, LMP_DEVICE_CODE, ""}; \
    return kManifest;                                                                     \
  }                                                                                       \
                                                                                          \
 private:

// After the class, at global scope. id is the name of the module without quotes, the same
// of the manifest: the build lists the modules of a firmware by these names.
#define LMP_REGISTER_MODULE(id, Class)      \
  ::lmp::sdk::Module& lmp_module_##id() {   \
    static Class instance;                  \
    return instance;                        \
  }

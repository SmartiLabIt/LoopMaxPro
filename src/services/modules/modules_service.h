// LoopMaxPro - the service that hosts the modules.
// The modules of a firmware are chosen by the build. Each one gets its target mod.<name>,
// its file of settings, its ticks and the events; what it changes is told to the console,
// to the broker and to the cloud without the module knowing any of them.
#pragma once
#include "lmp_config.h"

#if LMP_FEATURE_MODULES

#include "kernel/kernel.h"
#include "sdk/module.h"
#if LMP_FEATURE_THINGS
#include "sdk/things.h"
#endif

namespace lmp {

class IModulesService : public sdk::IModules {
 public:
  static constexpr ServiceId kId = ServiceId::Modules;

  // What everybody can know before the login: [{"name":..,"title":..,"icon":..,"kind":..}]
  virtual void listPublic(JsonWriter& w) const = 0;
  // Settings of a module as the console reads them. NotFound for a name that is not there;
  // a module without settings gives no members.
  virtual Status describeConfig(StringView name, JsonWriter& w) const = 0;
  // Name of the module at that place: the number the events ModuleState and ModuleData carry.
  // Null past the last one.
  virtual const char* nameAt(size_t index) const = 0;
  // The working module that answers the paths /<word>/... (sdk/http.h). Null for none.
  virtual sdk::Module* httpOwner(StringView word) = 0;
};

class ModulesService : public Service, public IModulesService, private sdk::ModuleHost {
 public:
  ModulesService() = default;
  // The modules outlive the service: what they took from the system is given back.
  ~ModulesService() override;

  // The firmware has a service of the product: the modules start after it. Before the start.
  void afterProduct() { afterProduct_ = true; }

  const ServiceInfo& info() const override;
  Status start(Kernel& kernel) override;
  void stop() override;
  void tick(uint32_t nowMs) override;
  void onEvent(const Event& event) override;
  void describe(JsonWriter& w) const override;
  void commands(CommandSet& set) override;
  const char* reportPart() const override { return "modules"; }
  void* api() override { return static_cast<IModulesService*>(this); }

  size_t count() const override { return count_; }
  sdk::Module* find(const char* name) override;
  Status command(const char* module, const char* name, const JsonValue& args) override;
  void listPublic(JsonWriter& w) const override;
  Status describeConfig(StringView name, JsonWriter& w) const override;
  const char* nameAt(size_t index) const override {
    return index < count_ ? slots_[index].module->manifest().name : nullptr;
  }
  sdk::Module* httpOwner(StringView word) override;

  // For the tests: a module given by hand takes the place of the ones of the build.
  // Call before the system begins. The module must live as long as the service.
  Status add(sdk::Module& module);

 private:
  struct Slot {
    sdk::Module* module = nullptr;
    sdk::ModuleContext ctx;
    CommandSet* commands = nullptr;
    uint8_t target = CommandBus::kNoTarget;
    sdk::ModuleState told = sdk::ModuleState::Stopped;
    uint32_t nextTickMs = 0;
    uint32_t dataAtMs = 0;
    uint32_t fileAtMs = 0;
    bool live = false;        // onInit was called, and the module was not stopped since
    bool started = false;     // onStart was called
    bool dataDue = false;     // a change is waiting to be told
    bool fileDue = false;     // a change is waiting to be written
#if LMP_FEATURE_THINGS
    sdk::ThingsBook things;   // what its things were at the last look
#endif
  };

  void collect();
  Status bring(Slot& slot);
  void begin(Slot& slot, uint32_t nowMs);
  void end(Slot& slot);
  // Stops a module without writing anything: what it holds goes back to the system.
  void release(Slot& slot);
  bool networkReady() const;
  // True for a module that must not work now: the service of the product blocks the device.
  // A bridge is never held: what ends the block arrives through it.
  bool held(const Slot& slot) const;
  // True when the device is blocked, and the word that says why.
  bool blocked() const;
  const char* blockReason() const;
  // Stops, or starts again, the modules after the block changed.
  void followBlock();
  void tellBlock(Slot& slot);
  void follow(Slot& slot, size_t index, uint32_t nowMs);
  void readFile(Slot& slot);
  // now: written before returning, on the application task. Otherwise the text is prepared
  // here and written by the worker: the round of the system does not wait for the flash.
  // Busy while the worker is still writing the file of before.
  Status writeFile(Slot& slot, bool now = false);
  // Waits for the file the worker is writing. For restart, factory reset and stop.
  void settleWrite();
  void writeDone();
  void writeEntry(JsonWriter& w, const Slot& slot, bool withState) const;
  // The things of a module as data (sdk/things.h): the commands every module has for them,
  // and the member of its document.
  void addThingCommands(Slot& slot);
  static void describeThings(const Slot& slot, JsonWriter& w);
  static bool works(const Slot& slot);
  int indexOf(StringView name) const;

  Kernel* kernel_ = nullptr;
  LogSource src_ = kLogSys;
  bool afterProduct_ = false;
  Slot slots_[LMP_MAX_MODULES];
  size_t count_ = 0;
  bool collected_ = false;
  bool byHand_ = false;
  // Room for the file of one module. Where the worker has a task it is taken once; on the
  // boards without tasks, where every KB counts, only while a file is read or written.
  char* file_ = nullptr;
  bool fileKept_ = false;
  char* takeFile();
  void giveFile();

  // The file handed to the worker: its own copy, since file_ is used again at once.
  char* pending_ = nullptr;
  size_t pendingLength_ = 0;
  char pendingPath_[48] = "";
  IStorage* pendingStorage_ = nullptr;
  volatile bool writing_ = false;       // the worker has a file to write
  volatile bool cancelWrite_ = false;   // factory reset: the file must not be written
  volatile Status lastWrite_ = Status::Ok;
  uint32_t writesDone_ = 0;
  uint32_t writesSeen_ = 0;
};

}  // namespace lmp

#endif  // LMP_FEATURE_MODULES

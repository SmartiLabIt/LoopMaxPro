#include "services/config/config_service.h"

#include <string.h>

#if LMP_FEATURE_MODULES
#include "services/modules/modules_service.h"
#endif

namespace lmp {

Status describeTargetConfig(Kernel& kernel, StringView target, JsonWriter& w) {
  if (kernel.commands().findTarget(target) == CommandBus::kNoTarget) return Status::NotFound;
  // The settings of the system are in sections: each one belongs to a service.
  struct Owner {
    const char* target;
    Config::Section section;
  };
  static const Owner owners[] = {
      {"sys.config", Config::kSys}, {"sys.net", Config::kNet},     {"sys.web", Config::kWeb},
      {"sys.clock", Config::kClock}, {"sys.cloud", Config::kCloud}, {"sys.mqtt", Config::kMqtt},
      {"sys.geo", Config::kGeo},
  };
  w.beginObject();
  for (const Owner& o : owners) {
    if (target == StringView(o.target)) kernel.config().describe(w, o.section);
  }
#if LMP_FEATURE_MODULES
  // The settings of a module are in the module.
  const IModulesService* modules = kernel.api<IModulesService>();
  if (modules && target.startsWith("mod.")) modules->describeConfig(target.substr(4), w);
#endif
  w.endObject();
  return Status::Ok;
}

namespace {

constexpr ArgSpec kSetArgs[] = {
    Arg::str("lang", 2, 2).optional(),
    Arg::str("hostname", 1, 32).optional(),
    Arg::str("deviceName", 0, 32).optional(),
};

// Host names: letters, digits and hyphen, not at the ends.
bool validHostname(const char* name) {
  const size_t len = strlen(name);
  if (len == 0 || name[0] == '-' || name[len - 1] == '-') return false;
  for (size_t i = 0; i < len; ++i) {
    const char c = name[i];
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                    c == '-';
    if (!ok) return false;
  }
  return true;
}

bool validLanguage(const char* code) {
  return strlen(code) == 2 && code[0] >= 'a' && code[0] <= 'z' && code[1] >= 'a' && code[1] <= 'z';
}

}  // namespace

const ServiceInfo& ConfigService::info() const {
  static const ServiceInfo kInfo{ServiceId::Config, "config", "\xF0\x9F\x9B\xA0", Mode::Any,
                                 RestartPolicy::Critical, 500,
                                 DepList(ServiceId::Storage, ServiceId::Log)};
  return kInfo;
}

Status ConfigService::start(Kernel& kernel) {
  kernel_ = &kernel;
  src_ = kernel.log().addSource(info().name, info().icon);
  Config& config = kernel.config();
  const Status loaded = config.load();
  if (loaded != Status::Ok) {
    // The settings in memory are valid: the device works, changes may not survive a restart.
    LMP_LOGE(src_, "settings not saved: %s", statusName(loaded));
    degrade("settings cannot be saved");
  }
  if (config.firstBoot()) LMP_LOGI(src_, "first start: device key created");
  LMP_LOGI(src_, "host %s, access point %s, language %s", config.sys.hostname.c_str(),
           config.net.apSsid.c_str(), config.sys.lang.c_str());
  kernel.setMode(config.mode());
  return Status::Ok;
}

void ConfigService::stop() {
  if (kernel_ && kernel_->config().dirty()) kernel_->config().save();
}

void ConfigService::tick(uint32_t nowMs) {
  if (kernel_) kernel_->config().tick(nowMs);
}

void ConfigService::describe(JsonWriter& w) const {
  if (!kernel_) return;
  const Config& config = kernel_->config();
  w.boolean("dirty", config.dirty());
  w.boolean("firstBoot", config.firstBoot());
  for (uint8_t i = 0; i < Config::kSectionCount; ++i) {
    const Config::Section section = static_cast<Config::Section>(i);
    w.beginObject(Config::sectionName(section));
    config.describe(w, section);
    w.endObject();
  }
}

Status ConfigService::applySys(const Args& a, CmdContext& ctx) {
  if (!kernel_) return Status::Error;
  Config& config = kernel_->config();

  // Check everything first: either all the values are applied or none.
  if (a.has("lang") && !validLanguage(a.str("lang"))) {
    ctx.errorField = "lang";
    return Status::Invalid;
  }
  if (a.has("hostname") && !validHostname(a.str("hostname"))) {
    ctx.errorField = "hostname";
    return Status::Invalid;
  }
  if (a.count() == 0) return Status::Invalid;

  if (a.has("lang")) config.sys.lang.assign(a.str("lang"));
  if (a.has("hostname")) config.sys.hostname.assign(a.str("hostname"));
  if (a.has("deviceName")) config.sys.deviceName.assign(a.str("deviceName"));
  config.touch(Config::kSys);
  kernel_->events().publish(Event(EventId::ConfigChanged, Config::kSys));
  return Status::Ok;
}

void ConfigService::commands(CommandSet& set) {
  set.add("set", LMP_HELP("Changes language, host name or device name"), kSetArgs,
          [this](const Args& a, CmdContext& ctx) -> Status { return applySys(a, ctx); });
  set.add("save", LMP_HELP("Writes the settings to storage now"),
          [this](const Args&, CmdContext&) -> Status {
            return kernel_ ? kernel_->config().save() : Status::Error;
          });
}

}  // namespace lmp

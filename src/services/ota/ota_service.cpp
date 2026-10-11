#include "services/ota/ota_service.h"

#if LMP_FEATURE_OTA

#include <string.h>

#include "lmp_version.h"

#if LMP_FEATURE_HTTPCLIENT
#include "services/httpclient/httpclient_service.h"
#endif

namespace lmp {
namespace {

// A download can last minutes on a slow connection.
constexpr uint32_t kDownloadTimeoutMs = 300000;
// Time left to the answer of the request before the device restarts.
constexpr uint32_t kRestartDelayMs = 1500;

constexpr ArgSpec kUrlArgs[] = {
    Arg::str("url", 8, 160),
    Arg::str("sha256", 64, 64).optional(),
    Arg::str("target", 2, 3).optional(),
    Arg::u32("size", 1, 16777216).optional(),
};

const char* targetName(hal::OtaTarget target) {
  return target == hal::OtaTarget::Filesystem ? "fs" : "app";
}

}  // namespace

const char* otaStateName(OtaState state) {
  switch (state) {
    case OtaState::Receiving: return "receiving";
    case OtaState::Installed: return "installed";
    case OtaState::Failed:    return "failed";
    case OtaState::Idle:      break;
  }
  return "idle";
}

const ServiceInfo& OtaService::info() const {
  static const ServiceInfo kInfo{ServiceId::Ota, "ota", "\xE2\x8F\xAC", Mode::Any,
                                 RestartPolicy::Retry, 250,
                                 DepList(ServiceId::Storage, ServiceId::Log)};
  return kInfo;
}

Status OtaService::start(Kernel& kernel) {
  kernel_ = &kernel;
  src_ = kernel.log().addSource(info().name, info().icon);
  ota_ = kernel.board().ota;
  if (!ota_) {
    fail("no firmware update on this board");
    return Status::Unsupported;
  }
  if (!mutex_ && kernel.board().rtos) mutex_ = kernel.board().rtos->createMutex();

  trial_ = ota_->pendingVerify();
  trialSinceMs_ = kernel.now();
  if (trial_) LMP_LOGW(src_, "new firmware %s on trial", LMP_VERSION);
  return Status::Ok;
}

void OtaService::stop() { abort("service stopped"); }

// ---------------------------------------------------------------- an image, piece by piece

Status OtaService::begin(hal::OtaTarget target, size_t size, StringView sha256,
                         OtaOrigin origin) {
  if (!ota_ || !running()) return Status::Denied;
  hal::LockGuard guard(mutex_);
  if (state_ == OtaState::Receiving || state_ == OtaState::Installed) return Status::Busy;

  check_ = !sha256.empty();
  if (check_ && !sha256Parse(sha256, wanted_)) return Status::Invalid;

  const Status s = ota_->begin(target, size);
  if (s != Status::Ok) {
    state_ = OtaState::Failed;
    error_ = s == Status::NoMem ? "image too large" : "cannot start";
    ++failures_;
    LMP_LOGE(src_, "%s image refused: %s", targetName(target), statusName(s));
    return s;
  }
  state_ = OtaState::Receiving;
  origin_ = origin;
  target_ = target;
  size_ = size;
  received_ = 0;
  error_ = "";
  digest_[0] = '\0';
  hash_.reset();
  LMP_LOGI(src_, "%s image from %s, %lu bytes", targetName(target),
           origin == OtaOrigin::Url ? "the network" : "a file", static_cast<unsigned long>(size));
  return Status::Ok;
}

Status OtaService::write(const uint8_t* data, size_t len) {
  if (!ota_) return Status::Denied;
  hal::LockGuard guard(mutex_);
  if (state_ != OtaState::Receiving) return Status::Denied;
  if (len == 0) return Status::Ok;
  if (size_ && received_ + len > size_) {
    giveUp("longer than declared");
    return Status::Invalid;
  }
  const Status s = ota_->write(data, len);
  if (s != Status::Ok) {
    giveUp(s == Status::NoMem ? "image too large" : "write error");
    return s;
  }
  hash_.update(data, len);
  received_ += len;
  return Status::Ok;
}

Status OtaService::finish() {
  if (!ota_) return Status::Denied;
  hal::LockGuard guard(mutex_);
  if (state_ != OtaState::Receiving) return Status::Denied;

  if (received_ == 0 || (size_ && received_ != size_)) {
    giveUp(received_ == 0 ? "empty" : "shorter than declared");
    return Status::Invalid;
  }
  uint8_t digest[Sha256::kSize];
  hash_.finish(digest);
  sha256Hex(digest, digest_);
  if (check_ && memcmp(digest, wanted_, sizeof(digest)) != 0) {
    giveUp("fingerprint does not match");
    return Status::Invalid;
  }

  const Status s = ota_->end();
  if (s != Status::Ok) {
    state_ = OtaState::Failed;
    error_ = "image not valid";
    ++failures_;
    LMP_LOGE(src_, "%s image not accepted: %s", targetName(target_), statusName(s));
    return s;
  }
  state_ = OtaState::Installed;
  ++installs_;
  LMP_LOGI(src_, "%s image installed, %lu bytes", targetName(target_),
           static_cast<unsigned long>(received_));
  return Status::Ok;
}

// Called with the mutex taken.
void OtaService::giveUp(const char* reason) {
  ota_->abort();
  state_ = OtaState::Failed;
  error_ = reason;
  ++failures_;
  LMP_LOGE(src_, "%s image given up after %lu bytes: %s", targetName(target_),
           static_cast<unsigned long>(received_), reason);
}

void OtaService::abort(const char* reason) {
  if (!ota_) return;
  hal::LockGuard guard(mutex_);
  if (state_ != OtaState::Receiving) return;
  giveUp(reason ? reason : "stopped");
}

// ---------------------------------------------------------------- from an address

#if LMP_FEATURE_HTTPCLIENT

Status OtaService::fromUrl(const char* url, const char* sha256, hal::OtaTarget target,
                           size_t size, uint32_t ref) {
  if (downloading_) return Status::Busy;
  IHttpClientService* http = kernel_->api<IHttpClientService>();
  if (!http) return Status::Unsupported;
  // Asked before anything is touched: without a way out there is nothing to start.
  if (!http->ready()) return Status::Denied;

  LMP_TRY(begin(target, size, StringView(sha256), OtaOrigin::Url));
  url_.assign(url);
  writeFailed_ = false;

  HttpFetch what;
  what.url = url_.c_str();
  what.timeoutMs = kDownloadTimeoutMs;
  what.sink = [this](Span<const uint8_t> piece) {
    if (write(piece.data(), piece.size()) == Status::Ok) return true;
    writeFailed_ = true;
    return false;
  };
  const Status asked =
      http->fetch(what, [this](const HttpAnswer& answer) { onDownloaded(answer); });
  if (asked != Status::Ok) {
    abort("download not started");
    return asked;
  }
  downloading_ = true;
  downloadRef_ = ref;
  return Status::Ok;
}

void OtaService::onDownloaded(const HttpAnswer& answer) {
  downloading_ = false;
  Status outcome;
  if (writeFailed_) {
    outcome = Status::Io;   // already given up, with its reason
  } else if (!answer.good()) {
    // The page of an error must never be taken for a firmware.
    abort(answer.result != Status::Ok ? "download interrupted" : "the server refused");
    outcome = answer.result != Status::Ok ? answer.result : Status::NotFound;
  } else {
    outcome = finish();
  }
  if (downloadRef_) kernel_->events().publish(commandDone(downloadRef_, outcome));
  downloadRef_ = 0;
}

#else

Status OtaService::fromUrl(const char*, const char*, hal::OtaTarget, size_t, uint32_t) {
  return Status::Unsupported;
}
void OtaService::onDownloaded(const HttpAnswer&) {}

#endif

// ---------------------------------------------------------------- application task

void OtaService::tick(uint32_t nowMs) {
  if (!ota_) return;

  OtaState state;
  size_t received, size;
  uint32_t installs, failures;
  {
    hal::LockGuard guard(mutex_);
    state = state_;
    received = received_;
    size = size_;
    installs = installs_;
    failures = failures_;
  }

  if (state == OtaState::Receiving) {
    // A step every five parts in a hundred, or every 64 KB when the size is not known.
    if (size) {
      const uint8_t percent = static_cast<uint8_t>((static_cast<uint64_t>(received) * 100) / size);
      if (percent >= percentSaid_ + 5 || percent < percentSaid_) {
        percentSaid_ = static_cast<uint8_t>(percent - percent % 5);
        kernel_->events().publish(Event(EventId::OtaProgress, 0, percent, received));
      }
    } else if (received >= receivedSaid_ + 65536 || received < receivedSaid_) {
      receivedSaid_ = received;
      kernel_->events().publish(Event(EventId::OtaProgress, 0, 0, received));
    }
  }

  // Every image that ended is told, also the one given up between two rounds.
  if (state != OtaState::Receiving) {
    percentSaid_ = 0;
    receivedSaid_ = 0;
  }
  for (; failuresSaid_ != failures; ++failuresSaid_) {
    kernel_->events().publish(
        Event(EventId::OtaDone, 0, static_cast<uint8_t>(Status::Error), received));
  }
  if (installsSaid_ != installs) {
    installsSaid_ = installs;
    kernel_->events().publish(
        Event(EventId::OtaDone, 0, static_cast<uint8_t>(Status::Ok), received));
    kernel_->requestRestart("firmware updated", false, kRestartDelayMs);
  }

  // A firmware that ran this long without restarting is good.
  if (trial_ && LMP_OTA_CONFIRM_SEC > 0 &&
      static_cast<int32_t>(nowMs - trialSinceMs_) >=
          static_cast<int32_t>(LMP_OTA_CONFIRM_SEC) * 1000) {
    if (ota_->markValid() == Status::Ok) {
      trial_ = false;
      LMP_LOGI(src_, "new firmware confirmed");
    } else {
      trialSinceMs_ = nowMs;   // asked again later
    }
  }
}

// ---------------------------------------------------------------- documents and commands

void OtaService::describe(JsonWriter& w) const {
  if (!kernel_) return;
  hal::LockGuard guard(mutex_);
  w.boolean("available", ota_ != nullptr);
  w.str("installed", LMP_VERSION);
  w.str("build", LMP_BUILD_DATE);
  w.str("state", otaStateName(state_));
  if (state_ != OtaState::Idle) {
    w.str("target", targetName(target_));
    w.str("from", origin_ == OtaOrigin::Url ? "url" : "file");
    w.u32("received", static_cast<uint32_t>(received_));
    w.u32("size", static_cast<uint32_t>(size_));
    if (size_) w.u32("percent", static_cast<uint32_t>((static_cast<uint64_t>(received_) * 100) / size_));
    w.str("error", error_);
    w.str("sha256", digest_);
  }
  w.boolean("trial", trial_);
  w.boolean("canRollback", ota_ && ota_->canRollback());
  w.boolean("fromUrl", LMP_FEATURE_HTTPCLIENT != 0);
  w.u32("installs", installs_);
  w.u32("failures", failures_);
}

void OtaService::commands(CommandSet& set) {
  set.add("fromUrl", LMP_HELP("Downloads an image from an address and installs it"), kUrlArgs,
          [this](const Args& a, CmdContext& ctx) -> Status {
            if (!ota_) return Status::Unsupported;
            hal::OtaTarget target = hal::OtaTarget::App;
            if (a.has("target")) {
              const char* name = a.str("target");
              if (strcmp(name, "fs") == 0) {
                target = hal::OtaTarget::Filesystem;
              } else if (strcmp(name, "app") != 0) {
                ctx.errorField = "target";
                return Status::Invalid;
              }
            }
            const char* url = a.str("url");
            if (strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0) {
              ctx.errorField = "url";
              return Status::Invalid;
            }
            const Status s = fromUrl(url, a.str("sha256"), target, a.u32("size"), ctx.ref);
            if (s == Status::Invalid) ctx.errorField = "sha256";
            return s == Status::Ok ? Status::Pending : s;
          },
          kCmdConfirm | kCmdAsync);

  set.add("confirm", LMP_HELP("Confirms the firmware that is running"),
          [this](const Args&, CmdContext&) -> Status {
            if (!ota_) return Status::Unsupported;
            const Status s = ota_->markValid();
            if (s == Status::Ok && trial_) {
              trial_ = false;
              LMP_LOGI(src_, "new firmware confirmed");
            }
            return s;
          });

  set.add("rollback", LMP_HELP("Goes back to the firmware of before and restarts"),
          [this](const Args&, CmdContext&) -> Status {
            if (!ota_) return Status::Unsupported;
            if (!ota_->canRollback()) return Status::Denied;
            const Status s = ota_->rollback();
            if (s != Status::Ok) return s;
            LMP_LOGW(src_, "back to the firmware of before");
            kernel_->requestRestart("firmware rollback", false, kRestartDelayMs);
            return Status::Ok;
          },
          kCmdConfirm);

  set.add("abort", LMP_HELP("Gives up the image that is arriving"),
          [this](const Args&, CmdContext&) -> Status {
            if (state_ != OtaState::Receiving) return Status::Denied;
            abort("stopped by command");
            return Status::Ok;
          });
}

}  // namespace lmp

#endif  // LMP_FEATURE_OTA

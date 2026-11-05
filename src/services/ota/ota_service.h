// LoopMaxPro - firmware update.
// An image arrives in pieces, from a file sent by the console or from an address on the
// network. It is written in the space of the firmware that is not running, checked against
// its fingerprint, and used from the next start. A new firmware is on trial until it is
// confirmed: where the board can, a firmware that does not hold goes back to the one before.
#pragma once
#include "lmp_config.h"

#if LMP_FEATURE_OTA

#include "base/sha256.h"
#include "hal/api/istorage.h"
#include "kernel/kernel.h"

namespace lmp {

struct HttpAnswer;

enum class OtaState : uint8_t {
  Idle,
  Receiving,   // an image is arriving
  Installed,   // written and checked: the device is about to restart
  Failed       // the last image was given up
};
const char* otaStateName(OtaState state);

enum class OtaOrigin : uint8_t { None, File, Url };

class IOtaService {
 public:
  static constexpr ServiceId kId = ServiceId::Ota;
  virtual ~IOtaService() = default;

  // The calls below can come from any context, one image at a time.
  // size is 0 when not known. sha256 is 64 hexadecimal digits, or empty: no check.
  // Busy while another image is arriving, Invalid for a fingerprint not well written.
  virtual Status begin(hal::OtaTarget target, size_t size, StringView sha256, OtaOrigin origin) = 0;
  virtual Status write(const uint8_t* data, size_t len) = 0;
  // Checks size and fingerprint and closes the image. Ok: the device restarts by itself.
  virtual Status finish() = 0;
  // The text must stay valid: use a literal.
  virtual void abort(const char* reason) = 0;

  virtual OtaState otaState() const = 0;
  virtual size_t received() const = 0;
};

class OtaService : public Service, public IOtaService {
 public:
  const ServiceInfo& info() const override;
  Status start(Kernel& kernel) override;
  void stop() override;
  void tick(uint32_t nowMs) override;
  void describe(JsonWriter& w) const override;
  void commands(CommandSet& set) override;
  void* api() override { return static_cast<IOtaService*>(this); }

  Status begin(hal::OtaTarget target, size_t size, StringView sha256, OtaOrigin origin) override;
  Status write(const uint8_t* data, size_t len) override;
  Status finish() override;
  void abort(const char* reason) override;
  OtaState otaState() const override { return state_; }
  size_t received() const override { return received_; }

  // Fingerprint of the last image that was closed, 64 digits. Empty before the first one.
  const char* lastSha256() const { return digest_; }

 private:
  Status fromUrl(const char* url, const char* sha256, hal::OtaTarget target, size_t size,
                 uint32_t ref);
  void onDownloaded(const HttpAnswer& answer);
  void giveUp(const char* reason);

  Kernel* kernel_ = nullptr;
  hal::IOta* ota_ = nullptr;
  hal::IMutex* mutex_ = nullptr;
  LogSource src_ = kLogSys;

  // The image that is arriving. Changed under the mutex: the pieces come from the context
  // of who sends them, the documents are written by the application task.
  OtaState state_ = OtaState::Idle;
  OtaOrigin origin_ = OtaOrigin::None;
  hal::OtaTarget target_ = hal::OtaTarget::App;
  size_t size_ = 0;
  size_t received_ = 0;
  const char* error_ = "";
  Sha256 hash_;
  uint8_t wanted_[Sha256::kSize];
  bool check_ = false;
  char digest_[Sha256::kSize * 2 + 1] = "";

  // Application task.
  uint8_t percentSaid_ = 0;
  size_t receivedSaid_ = 0;
  uint32_t installsSaid_ = 0;
  uint32_t failuresSaid_ = 0;
  uint32_t downloadRef_ = 0;
  bool downloading_ = false;
  bool writeFailed_ = false;    // a piece of the download was refused
  bool trial_ = false;          // the firmware running is waiting for its confirmation
  uint32_t trialSinceMs_ = 0;
  uint32_t installs_ = 0;
  uint32_t failures_ = 0;
  FixedString<160> url_;
};

}  // namespace lmp

#endif  // LMP_FEATURE_OTA

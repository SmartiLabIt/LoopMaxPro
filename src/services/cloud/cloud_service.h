// LoopMaxPro - the cloud, public part.
// What the device tells the server and when, the form of the envelope, and the requests of
// the server taken to the command bus: the same documents and the same commands of the
// console. How the envelope travels, who the server is and how the device proves who it is
// belong to the bridge, a private part that attaches itself here (ICloudBridge).
//
// The bridge carries any envelope without knowing what is inside: a new module, or a new
// command, works from the server without changing bridge and server. docs/cloud.md.
#pragma once
#include "lmp_config.h"

#if LMP_FEATURE_CLOUD

#include "kernel/kernel.h"

namespace lmp {

// Called with the text of an envelope that arrived from the server. Any task: the text is
// copied at once and handled on the application task.
using CloudReceiveFn = Delegate<void(StringView envelope)>;

// The private part: transport, authentication, protocol of the server.
class ICloudBridge {
 public:
  virtual ~ICloudBridge() = default;
  // Short and without secrets: shown in the document of the service.
  virtual const char* name() const = 0;
  // Application task, at every round: the bridge moves its own work on.
  virtual void poll(uint32_t nowMs) = 0;
  virtual bool connected() const = 0;
  // One envelope, JSON. Busy when it cannot take it now: the service offers it again later.
  virtual Status send(StringView envelope) = 0;
  // Where the envelopes of the server go. Set once, when the bridge is attached.
  virtual void onReceive(CloudReceiveFn fn) = 0;
  // Members of the document of the service: server, account, last error. Never secrets.
  // An update the server offers goes in "update": {version, kind, size}:
  // the console of the device shows it with a button.
  virtual void describe(JsonWriter& w) const {}
  // Asks the server now whether there is an update. The answer arrives later: event
  // cloud.update. Unsupported for a bridge that cannot.
  virtual Status checkUpdate() { return Status::Unsupported; }
  // Installs the update the server offered: download, check, install, restart. Pending while
  // the bridge asks the server again for an address that is still valid.
  virtual Status installUpdate() { return Status::Unsupported; }
};

class ICloudService {
 public:
  static constexpr ServiceId kId = ServiceId::Cloud;
  virtual ~ICloudService() = default;

  // One bridge at a time: Busy when there is one already. The bridge must live as long as
  // it is attached.
  virtual Status attach(ICloudBridge& bridge) = 0;
  virtual void detach(ICloudBridge& bridge) = 0;
  virtual bool connected() const = 0;
  // True when the device has a server to tell, whether it is there now or not.
  virtual bool attached() const = 0;
};

class CloudService : public Service, public ICloudService, private ILogSink {
 public:
  ~CloudService() override;

  const ServiceInfo& info() const override;
  Status start(Kernel& kernel) override;
  void stop() override;
  void tick(uint32_t nowMs) override;
  void onEvent(const Event& event) override;
  void describe(JsonWriter& w) const override;
  void commands(CommandSet& set) override;
  const char* reportPart() const override { return "cloud"; }
  void* api() override { return static_cast<ICloudService*>(this); }

  Status attach(ICloudBridge& bridge) override;
  void detach(ICloudBridge& bridge) override;
  bool connected() const override { return up_; }
  bool attached() const override { return bridge_ != nullptr; }

 private:
  // Parts of the report, as bits: what changed and waits to be told.
  enum Part : uint16_t {
    kDevice = 1, kSystem = 2, kClock = 4, kNet = 8, kGeo = 16, kStorage = 32,
    kServices = 64, kModules = 128
  };
  struct Incoming {
    char text[LMP_CLOUD_IN_MAX];
    uint16_t length;
    bool cut;
  };
  // A command that answered pending: its outcome goes to the server with the same ref.
  struct Waiting {
    uint32_t busRef = 0;
    bool done = false;         // ended: the outcome waits to leave
    Status outcome = Status::Ok;
    char ref[24] = "";
  };

  void onReceive(StringView envelope);   // context of the bridge
  void handle(const Incoming& in);       // application task
  void onLog(const LogEntry& entry, const char* sourceName) override;

  bool roomReady();
  void onUp(uint32_t nowMs);
  void flush(uint32_t nowMs);
  // Writes the envelope and hands it to the bridge. False when the bridge said Busy: what
  // was due stays due.
  // With whole set, an envelope longer than the room is not sent: tooLarge_ says so, and the
  // caller sends it again in pieces.
  bool send(const char* type, StringView target, StringView name, StringView ref,
            DescribeFn body, bool whole = false);
  bool sendReport(uint16_t parts, StringView ref, bool whole = false);
  // A report of those parts: in one envelope, or one part at a time when it does not fit.
  bool sendParts(uint16_t parts);
  bool sendDoc(StringView target, StringView ref);
  bool sendLog(uint32_t since, uint32_t max, StringView ref);
  bool sendEvents();
  void runCommand(StringView target, StringView name, StringView args, StringView ref);
  void answerError(const char* type, StringView ref, const char* error);

  Kernel* kernel_ = nullptr;
  ICloudBridge* bridge_ = nullptr;
  hal::IMutex* mutex_ = nullptr;
  LogSource src_ = kLogSys;

  bool up_ = false;
  uint32_t upSinceMs_ = 0;
  uint32_t connections_ = 0;
  uint32_t sent_ = 0;
  uint32_t received_ = 0;
  uint32_t refused_ = 0;    // envelopes not understood, or for another device
  uint32_t dropped_ = 0;    // envelopes of the server that found the inbox full
  uint32_t failed_ = 0;     // envelopes the bridge could not send

  // What is due. Changes close together become one envelope.
  bool fullReport_ = false;
  bool tooLarge_ = false;
  uint16_t parts_ = 0;
  uint16_t single_ = 0;      // parts that go one by one: together they did not fit
  uint32_t partsAtMs_ = 0;
  uint32_t moduleDocs_ = 0;   // one bit for each module
  uint32_t docsAtMs_ = 0;
  uint32_t nextBeatMs_ = 0;

  // Log entries to push, by number: read back from the logger when they leave.
  uint32_t logSeq_[LMP_CLOUD_LOG_QUEUE] = {};
  size_t logCount_ = 0;

  // Events for the server, as the console gets them: with them the console on the server
  // reads again what changed, as on the device. Application task only.
  Event events_[LMP_CLOUD_EVENT_QUEUE];
  size_t eventCount_ = 0;
  uint32_t eventsLost_ = 0;   // the queue was full

  Waiting waiting_[LMP_CLOUD_WAITING];

  // Room for the envelopes: taken when a bridge attaches, never before.
  static constexpr size_t kOutMax = LMP_CLOUD_OUT_MAX;
  char* out_ = nullptr;
  size_t outLength_ = 0;     // length of the last envelope written in out_
  Incoming* inbox_ = nullptr;
  size_t head_ = 0;
  size_t count_ = 0;
};

}  // namespace lmp

#endif  // LMP_FEATURE_CLOUD

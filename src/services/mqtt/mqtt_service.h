// LoopMaxPro - messages through a broker, MQTT.
// One more way in for the commands and one more way out for documents and events: the same
// ones of the console. Who connects, with which name and password, is in the settings.
//
//   <base>/status                 out, kept by the broker: online, offline
//   <base>/cmd/<target>/<command> in: the arguments as JSON
//   <base>/ack/<target>/<command> out: the answer of the command
//   <base>/get/<target>           in: asks a document; target "report" asks the report
//   <base>/doc/<target>           out: the document
//   <base>/evt                    out: the events of the system
//   <base>/home/...               what the modules show to Home Assistant: home_bridge.h
#pragma once
#include "lmp_config.h"

#if LMP_FEATURE_MQTT

#include "hal/api/ihttp.h"
#include "kernel/kernel.h"
#if LMP_FEATURE_MODULES
#include "services/mqtt/home_bridge.h"
#endif

namespace lmp {

// Called on the application task. topic is what follows the base, for example "x/meteo".
using MqttListener = Delegate<void(StringView topic, StringView payload)>;

class IMqttService {
 public:
  static constexpr ServiceId kId = ServiceId::Mqtt;
  virtual ~IMqttService() = default;

  virtual bool connected() const = 0;
  // topic follows the base: "x/meteo" goes out as "<base>/x/meteo". Denied when not connected.
  virtual Status publish(const char* topic, StringView payload, bool retain = false) = 0;
  // The whole topic, outside the base of the device: for what another system wants at an
  // address of its own, as the descriptions Home Assistant looks for.
  virtual Status publishAt(const char* topic, StringView payload, bool retain = false) = 0;
  // Messages of the topics that begin with prefix, base excluded. The text must stay valid.
  // cmd, ack, get, doc, evt and status belong to the system.
  virtual Status listen(const char* prefix, MqttListener listener) = 0;
};

class MqttService : public Service, public IMqttService {
 public:
  const ServiceInfo& info() const override;
  Status start(Kernel& kernel) override;
  void stop() override;
  void tick(uint32_t nowMs) override;
  void onEvent(const Event& event) override;
  void describe(JsonWriter& w) const override;
  void commands(CommandSet& set) override;
  void* api() override { return static_cast<IMqttService*>(this); }

  bool connected() const override { return up_; }
  Status publish(const char* topic, StringView payload, bool retain) override;
  Status publishAt(const char* topic, StringView payload, bool retain) override;
  Status listen(const char* prefix, MqttListener listener) override;

  const char* base() const { return base_.c_str(); }

 private:
  struct Message {
    char topic[LMP_MQTT_TOPIC_MAX];
    char payload[LMP_MQTT_PAYLOAD_MAX];
    uint16_t length;
    bool cut;      // the content was longer than what the system reads
  };
  struct Listener {
    const char* prefix = nullptr;
    MqttListener call;
  };

  // Applies the settings: connects, or leaves the broker.
  void apply();
  void leave();
  void onUp();
  // Context of the driver.
  void onMessage(StringView topic, StringView payload);
  // Application task.
  void handle(const Message& m);
  void runCommand(StringView rest, StringView payload);
  void sendDocument(StringView target, StringView payload);
  Status send(const char* topic, StringView payload, bool retain);

  Kernel* kernel_ = nullptr;
  hal::IMqtt* mqtt_ = nullptr;
  hal::IMutex* mutex_ = nullptr;
  LogSource src_ = kLogSys;

  bool wanted_ = false;      // the settings ask for a connection and the network is there
  bool up_ = false;
  bool online_ = false;
  uint32_t upSinceMs_ = 0;
  uint32_t connections_ = 0;
  uint32_t sent_ = 0;
  uint32_t received_ = 0;
  uint32_t dropped_ = 0;     // messages that found the queue full

  FixedString<48> base_;
  FixedString<32> clientId_;
  FixedString<64> will_;
  FixedString<48> lastError_;

  // Room for messages and answers: taken the first time the settings ask for a broker,
  // and kept. A device that never uses one never pays for it.
  bool roomReady();
  static constexpr size_t kOutMax = LMP_MQTT_OUT_MAX;
  Message* inbox_ = nullptr;
  char* out_ = nullptr;
  size_t head_ = 0;
  size_t count_ = 0;
  Listener listeners_[LMP_MQTT_LISTENERS];
  size_t listenerCount_ = 0;

  char topic_[LMP_MQTT_TOPIC_MAX + 48];
#if LMP_FEATURE_MODULES
  HomeBridge home_;
  bool homeListens_ = false;
#endif
};

}  // namespace lmp

#endif  // LMP_FEATURE_MQTT

// LoopMaxPro HAL - ESP32 family: time from the network, requests to servers, MQTT.
// Written with the components of the chip maker, which are part of both the Arduino and the
// ESP-IDF builds: the two implementations of the HAL share this file.
#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "base/fixed_string.h"
#include "hal/api/iclock.h"
#include "hal/api/ihttp.h"
#include "hal/api/memory.h"
#include "lmp_config.h"

#include <string.h>

#include <freertos/task.h>

namespace lmp::hal::esp32 {

class Ntp : public INtp {
 public:
  Status start(const char* server) override;
  void stop() override;
  bool running() const override { return running_; }
  uint32_t answers() const override;

 private:
  bool running_ = false;
  FixedString<48> server_;   // the driver keeps the pointer
};

#if LMP_FEATURE_HTTPCLIENT

class HttpClient : public IHttpClient {
 public:
  bool secure() const override { return true; }
  bool roomFor(const char* url) const override {
    return !url || strncmp(url, "https://", 8) != 0 || roomForSecure();
  }
  Status request(const HttpClientRequest& req, HttpClientResponse& res,
                 HttpChunkHandler onChunk) override;
};

#endif  // LMP_FEATURE_HTTPCLIENT

#if LMP_FEATURE_MQTT

class Mqtt : public IMqtt {
 public:
  Status configure(const MqttConfig& cfg) override;
  Status connect() override;
  void disconnect() override;
  bool connected() const override { return connected_; }
  const char* lastError() const override { return error_; }
  Status publish(const char* topic, StringView payload, uint8_t qos, bool retain) override;
  Status subscribe(const char* topic, uint8_t qos) override;
  void onMessage(MqttMessageHandler handler) override { handler_ = handler; }
  void poll() override {}

 private:
  static void onEvent(void* self, const char* base, int32_t id, void* data);
  void handle(int32_t id, void* data);

  void* client_ = nullptr;   // esp_mqtt_client_handle_t
  MqttMessageHandler handler_;
  volatile bool connected_ = false;
  volatile bool stopping_ = false;   // disconnect() is waiting for the task of the driver
  const char* error_ = "";
  bool configured_ = false;

  // The driver keeps the pointers for as long as it is connected.
  FixedString<64> host_;
  FixedString<32> clientId_;
  FixedString<32> username_;
  FixedString<63> password_;
  FixedString<64> willTopic_;
  FixedString<16> willMessage_;
  uint16_t port_ = 8883;
  uint16_t keepAliveSec_ = 60;
  bool useTls_ = true;
  const char* caPem_ = nullptr;
};

#endif  // LMP_FEATURE_MQTT

}  // namespace lmp::hal::esp32

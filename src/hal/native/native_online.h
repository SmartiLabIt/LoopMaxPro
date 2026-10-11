// LoopMaxPro HAL for the PC - what a device finds on the network: the time, servers that
// answer requests, a broker of messages. Nothing leaves this computer: the tests decide what
// is out there and read what the system sent.
#pragma once
#include <functional>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "hal/api/iclock.h"
#include "hal/api/ihttp.h"

namespace lmp::hal::native {

// ---------------------------------------------------------------- time from the network

class Ntp : public INtp {
 public:
  Status start(const char* server) override;
  void stop() override { running_ = false; }
  bool running() const override { return running_; }
  uint32_t answers() const override { return answers_; }

  // ---- hooks for the tests
  void reset();
  // The clock the answers set.
  void attach(IClock* clock) { clock_ = clock; }
  // A server answers. Nothing happens when nobody asked.
  void answer(int64_t unixSeconds);
  // Answers by itself at every start(), with the time of this computer. For the simulator.
  void setAuto(bool enabled) { auto_ = enabled; }
  void failNext(Status result) { startResult_ = result; }

  const std::string& server() const { return server_; }
  unsigned starts() const { return starts_; }

 private:
  IClock* clock_ = nullptr;
  std::string server_;
  uint32_t answers_ = 0;
  unsigned starts_ = 0;
  bool running_ = false;
  bool auto_ = false;
  Status startResult_ = Status::Ok;
};

// ---------------------------------------------------------------- requests to servers

using HttpPairs = std::vector<std::pair<std::string, std::string>>;

// A request as the system made it.
struct HttpSeen {
  std::string method;
  std::string url;
  std::string body;
  std::string contentType;
  HttpPairs headers;
};

class HttpClient : public IHttpClient {
 public:
  bool secure() const override { return secure_; }
  bool roomFor(const char* url) const override {
    return room_ || !url || std::string(url).compare(0, 8, "https://") != 0;
  }
  Status request(const HttpClientRequest& req, HttpClientResponse& res,
                 HttpChunkHandler onChunk) override;

  // ---- hooks for the tests
  void reset();
  // What a server answers. An address that ends with '*' stands for all the ones that begin
  // that way. An address nobody described answers 404.
  void when(const char* url, int status, const std::string& body);
  // No answer at all: the server is not there, the time is over, the certificate is refused.
  void whenFails(const char* url, Status result);
  // An answer made again at each request: a counter that moves, in the simulator.
  void whenMade(const char* url, int status, std::function<std::string()> make);
  // An answer that depends on what was asked: a server that reads the body of a request.
  void whenAsked(const char* url, int status, std::function<std::string(const HttpSeen&)> answer);
  // Size of the pieces a body is handed over in.
  void setChunk(size_t bytes) { chunk_ = bytes ? bytes : 1; }
  void setSecure(bool enabled) { secure_ = enabled; }
  // No memory to set up a secure connection, as on a small board under load.
  void setRoom(bool enough) { room_ = enough; }

  std::vector<HttpSeen> seen() const;
  size_t count() const;

 private:
  struct Rule {
    std::string url;
    int status;
    std::string body;
    Status result;
    std::function<std::string()> make;
    std::function<std::string(const HttpSeen&)> answer;
  };

  mutable std::mutex mutex_;
  std::vector<Rule> rules_;
  std::vector<HttpSeen> seen_;
  size_t chunk_ = 512;
  bool secure_ = true;
  bool room_ = true;
};

// ---------------------------------------------------------------- broker of messages

struct MqttSent {
  std::string topic;
  std::string payload;
  uint8_t qos;
  bool retain;
};

class Mqtt : public IMqtt {
 public:
  Status configure(const MqttConfig& cfg) override;
  Status connect() override;
  void disconnect() override;
  bool connected() const override { return connected_; }
  const char* lastError() const override { return error_.c_str(); }
  Status publish(const char* topic, StringView payload, uint8_t qos, bool retain) override;
  Status subscribe(const char* topic, uint8_t qos) override;
  void onMessage(MqttMessageHandler handler) override { handler_ = handler; }
  void poll() override;

  // ---- hooks for the tests
  void reset();
  // A broker that is not there, or that does not accept the device: every attempt fails.
  void setRefusing(const char* why);
  // Calls to poll() between a request to connect and its outcome.
  void setDelay(unsigned polls) { delay_ = polls ? polls : 1; }
  // The connection goes away. The driver connects again by itself.
  void drop();
  // A message arrives. False when the device is not connected or did not ask for that topic.
  bool deliver(const char* topic, const std::string& payload);

  const std::vector<MqttSent>& sent() const { return sent_; }
  void clearSent() { sent_.clear(); }
  const std::vector<std::string>& subscriptions() const { return subscriptions_; }
  unsigned connects() const { return connects_; }

  // What the system asked for.
  std::string host, clientId, username, password, willTopic, willMessage;
  uint16_t port = 0;
  uint16_t keepAliveSec = 0;
  bool useTls = false;
  bool configured = false;

 private:
  static bool matches(const std::string& filter, const std::string& topic);

  MqttMessageHandler handler_;
  std::vector<MqttSent> sent_;
  std::vector<std::string> subscriptions_;
  std::string error_;
  std::string refusing_;
  unsigned delay_ = 2;
  unsigned left_ = 0;
  unsigned connects_ = 0;
  bool wanted_ = false;
  bool connected_ = false;
};

}  // namespace lmp::hal::native

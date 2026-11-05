#include "hal/native/native_online.h"

#include <time.h>

namespace lmp::hal::native {
namespace {

const char* methodName(HttpMethod method) {
  switch (method) {
    case HttpMethod::Get:     return "GET";
    case HttpMethod::Post:    return "POST";
    case HttpMethod::Put:     return "PUT";
    case HttpMethod::Delete:  return "DELETE";
    case HttpMethod::Options: return "OPTIONS";
    case HttpMethod::Any:     break;
  }
  return "GET";
}

}  // namespace

// ---------------------------------------------------------------- time from the network

void Ntp::reset() {
  clock_ = nullptr;
  server_.clear();
  answers_ = 0;
  starts_ = 0;
  running_ = false;
  auto_ = false;
  startResult_ = Status::Ok;
}

Status Ntp::start(const char* server) {
  if (startResult_ != Status::Ok) return startResult_;
  if (!server || !*server) return Status::Invalid;
  server_ = server;
  running_ = true;
  ++starts_;
  if (auto_) answer(static_cast<int64_t>(time(nullptr)));
  return Status::Ok;
}

void Ntp::answer(int64_t unixSeconds) {
  if (!running_) return;
  if (clock_) clock_->setUnixTime(unixSeconds);
  ++answers_;
}

// ---------------------------------------------------------------- requests to servers

void HttpClient::reset() {
  std::lock_guard<std::mutex> lock(mutex_);
  rules_.clear();
  seen_.clear();
  chunk_ = 512;
  secure_ = true;
  room_ = true;
}

void HttpClient::when(const char* url, int status, const std::string& body) {
  std::lock_guard<std::mutex> lock(mutex_);
  rules_.insert(rules_.begin(), Rule{url ? url : "", status, body, Status::Ok, nullptr, nullptr});
}

void HttpClient::whenMade(const char* url, int status, std::function<std::string()> make) {
  std::lock_guard<std::mutex> lock(mutex_);
  rules_.insert(rules_.begin(), Rule{url ? url : "", status, std::string(), Status::Ok, std::move(make), nullptr});
}

void HttpClient::whenAsked(const char* url, int status, std::function<std::string(const HttpSeen&)> answer) {
  std::lock_guard<std::mutex> lock(mutex_);
  rules_.insert(rules_.begin(), Rule{url ? url : "", status, std::string(), Status::Ok, nullptr, std::move(answer)});
}

void HttpClient::whenFails(const char* url, Status result) {
  std::lock_guard<std::mutex> lock(mutex_);
  rules_.insert(rules_.begin(), Rule{url ? url : "", 0, std::string(), result, nullptr, nullptr});
}

std::vector<HttpSeen> HttpClient::seen() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return seen_;
}

size_t HttpClient::count() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return seen_.size();
}

Status HttpClient::request(const HttpClientRequest& req, HttpClientResponse& res,
                           HttpChunkHandler onChunk) {
  res = HttpClientResponse();
  if (!req.url || !*req.url) return Status::Invalid;
  const std::string url(req.url);
  if (url.compare(0, 7, "http://") != 0 && url.compare(0, 8, "https://") != 0) {
    return Status::Invalid;
  }
  if (url.compare(0, 8, "https://") == 0 && !secure_) return Status::Unsupported;

  Rule rule{url, 404, std::string(), Status::Ok, nullptr, nullptr};
  HttpSeen seen;
  size_t chunk;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    seen.method = methodName(req.method);
    seen.url = url;
    seen.body.assign(req.body.data ? req.body.data : "", req.body.size);
    seen.contentType = req.contentType ? req.contentType : "";
    for (const HttpHeader& h : req.headers) {
      seen.headers.emplace_back(h.name ? h.name : "", h.value ? h.value : "");
    }
    seen_.push_back(seen);

    for (const Rule& r : rules_) {
      const bool prefix = !r.url.empty() && r.url.back() == '*';
      const bool match = prefix ? url.compare(0, r.url.size() - 1, r.url, 0, r.url.size() - 1) == 0
                                : url == r.url;
      if (match) {
        rule = r;
        break;
      }
    }
    chunk = chunk_;
  }

  if (rule.result != Status::Ok) return rule.result;
  if (rule.make) rule.body = rule.make();
  if (rule.answer) rule.body = rule.answer(seen);
  res.status = rule.status;
  res.contentLength = rule.body.size();
  if (req.onHeader) {
    req.onHeader("Server", "lmp-test");
    req.onHeader("Content-Length", std::to_string(rule.body.size()).c_str());
  }
  for (size_t at = 0; at < rule.body.size(); at += chunk) {
    const size_t n = rule.body.size() - at < chunk ? rule.body.size() - at : chunk;
    if (onChunk && !onChunk(Span<const uint8_t>(
                       reinterpret_cast<const uint8_t*>(rule.body.data()) + at, n))) {
      return Status::Error;
    }
  }
  return Status::Ok;
}

// ---------------------------------------------------------------- broker of messages

void Mqtt::reset() {
  handler_ = MqttMessageHandler();
  sent_.clear();
  subscriptions_.clear();
  error_.clear();
  refusing_.clear();
  delay_ = 2;
  left_ = 0;
  connects_ = 0;
  wanted_ = false;
  connected_ = false;
  host.clear();
  clientId.clear();
  username.clear();
  password.clear();
  willTopic.clear();
  willMessage.clear();
  port = 0;
  keepAliveSec = 0;
  useTls = false;
  configured = false;
}

Status Mqtt::configure(const MqttConfig& cfg) {
  if (!cfg.host || !*cfg.host) return Status::Invalid;
  host = cfg.host;
  port = cfg.port;
  clientId = cfg.clientId ? cfg.clientId : "";
  username = cfg.username ? cfg.username : "";
  password = cfg.password ? cfg.password : "";
  willTopic = cfg.willTopic ? cfg.willTopic : "";
  willMessage = cfg.willMessage ? cfg.willMessage : "";
  keepAliveSec = cfg.keepAliveSec;
  useTls = cfg.useTls;
  configured = true;
  return Status::Ok;
}

Status Mqtt::connect() {
  if (!configured) return Status::Denied;
  wanted_ = true;
  if (!connected_ && left_ == 0) left_ = delay_;
  return Status::Ok;
}

void Mqtt::disconnect() {
  wanted_ = false;
  connected_ = false;
  left_ = 0;
  subscriptions_.clear();
}

void Mqtt::setRefusing(const char* why) { refusing_ = why ? why : ""; }

void Mqtt::drop() {
  if (!connected_) return;
  connected_ = false;
  subscriptions_.clear();
  error_ = "connection lost";
  if (wanted_) left_ = delay_;
}

void Mqtt::poll() {
  if (!wanted_ || connected_ || left_ == 0 || --left_ != 0) return;
  ++connects_;
  if (!refusing_.empty()) {
    error_ = refusing_;
    left_ = delay_;   // tries again, as a driver does
    return;
  }
  error_.clear();
  connected_ = true;
}

Status Mqtt::publish(const char* topic, StringView payload, uint8_t qos, bool retain) {
  if (!topic || !*topic) return Status::Invalid;
  if (!connected_) return Status::Denied;
  sent_.push_back(MqttSent{topic, std::string(payload.data ? payload.data : "", payload.size), qos,
                           retain});
  return Status::Ok;
}

Status Mqtt::subscribe(const char* topic, uint8_t) {
  if (!topic || !*topic) return Status::Invalid;
  if (!connected_) return Status::Denied;
  subscriptions_.push_back(topic);
  return Status::Ok;
}

// "a/+/c" takes one level, "a/#" takes all the rest.
bool Mqtt::matches(const std::string& filter, const std::string& topic) {
  size_t f = 0, t = 0;
  while (f < filter.size()) {
    if (filter[f] == '#') return true;
    if (filter[f] == '+') {
      while (t < topic.size() && topic[t] != '/') ++t;
      ++f;
      continue;
    }
    if (t >= topic.size() || filter[f] != topic[t]) return false;
    ++f;
    ++t;
  }
  return t == topic.size();
}

bool Mqtt::deliver(const char* topic, const std::string& payload) {
  if (!connected_ || !topic || !handler_) return false;
  const std::string name(topic);
  for (const std::string& filter : subscriptions_) {
    if (!matches(filter, name)) continue;
    handler_(StringView(name.data(), name.size()), StringView(payload.data(), payload.size()));
    return true;
  }
  return false;
}

}  // namespace lmp::hal::native

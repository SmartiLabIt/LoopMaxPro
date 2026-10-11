// LoopMaxPro HAL - ESP8266 with the Arduino framework: Wi-Fi and HTTP server.
// Everything here runs in the one context of the chip: the library of the core calls back
// only while the system lets it, never in the middle of something else.
#include "lmp_config.h"

#if LMP_FEATURE_NET

#include <Arduino.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#if LMP_FEATURE_MDNS
#include <ESP8266mDNS.h>
#endif
#include <LittleFS.h>

#include "hal/arduino/esp8266/esp8266_hal.h"
#include "hal/common/wifi_reason.h"

namespace lmp::hal::esp8266 {
namespace {

constexpr uint8_t kApClients = 4;
constexpr size_t kFrame = LMP_WEB_EVENT_LEN + 64;

// Objects of the core. They allocate: created once, at the first use.
Net* g_net = nullptr;
HttpServer* g_http = nullptr;
DNSServer g_dns;
ESP8266WebServer g_server(LMP_HTTP_PORT);
WiFiClient g_followers[LMP_WEB_EVENT_CLIENTS];
WiFiEventHandler g_onGotIp, g_onDisconnected, g_onConnected, g_onApJoined, g_onApLeft;

Ip4 fromCore(const IPAddress& a) { return Ip4::of(a[0], a[1], a[2], a[3]); }
IPAddress toCore(const Ip4& a) { return IPAddress(a.b[0], a.b[1], a.b[2], a.b[3]); }

uint8_t methodOf(HTTPMethod method) {
  switch (method) {
    case HTTP_GET:     return static_cast<uint8_t>(HttpMethod::Get);
    case HTTP_POST:    return static_cast<uint8_t>(HttpMethod::Post);
    case HTTP_PUT:     return static_cast<uint8_t>(HttpMethod::Put);
    case HTTP_DELETE:  return static_cast<uint8_t>(HttpMethod::Delete);
    case HTTP_OPTIONS: return static_cast<uint8_t>(HttpMethod::Options);
    default:           return 0;
  }
}

bool copyOut(const String& value, char* out, size_t cap) {
  if (!out || value.length() + 1 > cap) return false;
  memcpy(out, value.c_str(), value.length() + 1);
  return true;
}

// ---------------------------------------------------------------- request and response

class Request : public IHttpRequest {
 public:
  Request() {
    snprintf(path_, sizeof(path_), "%s", g_server.uri().c_str());
    snprintf(type_, sizeof(type_), "%s", g_server.header("Content-Type").c_str());
    const bool form = strncasecmp(type_, "application/x-www-form-urlencoded", 33) == 0;
    if (g_server.hasArg("plain")) {
      // Anything but a form: the body as it arrived.
      const String& plain = g_server.arg("plain");
      tooLong_ = plain.length() > LMP_HTTP_BODY_MAX;
      body_ = StringView(plain.c_str(), tooLong_ ? LMP_HTTP_BODY_MAX : plain.length());
    } else if (form) {
      // A form: the core keeps the values, already decoded, not the text. It is written
      // again, with what cannot travel as it is written as a code. The values of the
      // address are in the same list: who reads the form skips the names it does not know.
      size_t n = 0;
      for (int i = 0; i < g_server.args() && !tooLong_; ++i) {
        n += append(n, n ? "&" : "", false);
        n += append(n, g_server.argName(i).c_str(), true);
        n += append(n, "=", false);
        n += append(n, g_server.arg(i).c_str(), true);
      }
      form_[n < sizeof(form_) ? n : sizeof(form_) - 1] = '\0';
      body_ = StringView(form_, n);
    }
  }

  HttpMethod method() const override {
    return static_cast<HttpMethod>(methodOf(g_server.method()));
  }
  StringView path() const override { return StringView(path_); }
  StringView body() const override { return body_; }
  bool tooLong() const override { return tooLong_; }
  StringView contentType() const override { return StringView(type_); }
  Ip4 clientIp() const override { return fromCore(g_server.client().remoteIP()); }

  bool query(const char* name, char* out, size_t cap) const override {
    return g_server.hasArg(name) && copyOut(g_server.arg(name), out, cap);
  }
  bool header(const char* name, char* out, size_t cap) const override {
    return g_server.hasHeader(name) && copyOut(g_server.header(name), out, cap);
  }

 private:
  size_t append(size_t at, const char* text, bool encode) {
    static const char digits[] = "0123456789ABCDEF";
    size_t n = 0;
    for (const char* p = text; *p; ++p) {
      const unsigned char c = static_cast<unsigned char>(*p);
      const bool plain = !encode || (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
                         (c >= 'A' && c <= 'Z') || c == '-' || c == '_' || c == '.';
      if (at + n + (plain ? 1 : 3) >= sizeof(form_)) {
        tooLong_ = true;
        break;
      }
      if (plain) {
        form_[at + n++] = static_cast<char>(c);
      } else {
        form_[at + n++] = '%';
        form_[at + n++] = digits[c >> 4];
        form_[at + n++] = digits[c & 0x0F];
      }
    }
    return n;
  }

  char path_[128];
  char type_[48];
  // One request at a time: the text of a form can stay out of the stack.
  static char form_[LMP_HTTP_BODY_MAX + 1];
  StringView body_;
  bool tooLong_ = false;
};

char Request::form_[LMP_HTTP_BODY_MAX + 1];

class Response : public IHttpResponse {
 public:
  void status(uint16_t code) override { code_ = code; }
  void header(const char* name, const char* value) override {
    if (name && value) g_server.sendHeader(name, value);
  }
  void send(const char* contentType, StringView body) override {
    g_server.send(code_, contentType ? contentType : "text/plain", body.data ? body.data : "",
                  body.size);
    sent_ = true;
  }
  void sendFile(const char* fsPath, const char* contentType, bool gzip,
                uint32_t cacheSeconds) override {
    File file = LittleFS.open(fsPath, "r");
    if (!file) {
      g_server.send(404, "text/plain", "not found");
      sent_ = true;
      return;
    }
    char cache[56];
    if (cacheSeconds) {
      snprintf(cache, sizeof(cache), "public, max-age=%lu, immutable",
               static_cast<unsigned long>(cacheSeconds));
    } else {
      snprintf(cache, sizeof(cache), "no-cache");
    }
    g_server.sendHeader("Cache-Control", cache);
    // The core adds the header of the compression by itself, from the name of the file.
    (void)gzip;
    g_server.streamFile(file, contentType ? contentType : "application/octet-stream");
    file.close();
    sent_ = true;
  }
  void redirect(const char* url) override {
    g_server.sendHeader("Location", url ? url : "/");
    g_server.send(302, "text/plain", "");
    sent_ = true;
  }

  bool sent() const { return sent_; }

 private:
  uint16_t code_ = 200;
  bool sent_ = false;
};

}  // namespace

// ---------------------------------------------------------------- Wi-Fi

Status Net::init(const char* hostname) {
  g_net = this;
  if (!ready_) {
    // Nothing of the network is written to the flash by the chip, and nothing is tried
    // again by the chip: both are decisions of the system.
    WiFi.persistent(false);
    WiFi.setAutoConnect(false);
    WiFi.setAutoReconnect(false);
    WiFi.mode(WIFI_OFF);
    g_onGotIp = WiFi.onStationModeGotIP([](const WiFiEventStationModeGotIP&) {
      if (g_net) g_net->onGotAddress();
    });
    g_onDisconnected =
        WiFi.onStationModeDisconnected([](const WiFiEventStationModeDisconnected& e) {
          if (g_net) g_net->onDisconnected(static_cast<uint8_t>(e.reason));
        });
    g_onConnected = WiFi.onStationModeConnected([](const WiFiEventStationModeConnected&) {
      if (g_net) g_net->onAssociated();
    });
    g_onApJoined =
        WiFi.onSoftAPModeStationConnected([](const WiFiEventSoftAPModeStationConnected&) {
          if (g_net) g_net->onApClient(true);
        });
    g_onApLeft =
        WiFi.onSoftAPModeStationDisconnected([](const WiFiEventSoftAPModeStationDisconnected&) {
          if (g_net) g_net->onApClient(false);
        });
    ready_ = true;
  }
  if (hostname && *hostname) WiFi.hostname(hostname);
  return Status::Ok;
}

void Net::emit(NetEvent event) const {
  if (handler_) handler_(event);
}

void Net::applyMode() {
  const bool sta = wantSta_ || scanning_;
  WiFiMode_t mode = WIFI_OFF;
  if (sta && wantAp_) {
    mode = WIFI_AP_STA;
  } else if (sta) {
    mode = WIFI_STA;
  } else if (wantAp_) {
    mode = WIFI_AP;
  }
  if (WiFi.getMode() != mode) WiFi.mode(mode);
}

void Net::poll() {
  if (modeStale_) {
    modeStale_ = false;
    applyMode();
  }
#if LMP_FEATURE_MDNS
  if (mdns_) MDNS.update();
#endif
  if (captive_) g_dns.processNextRequest();
}

void Net::onAssociated() { emit(NetEvent::StaConnected); }

void Net::onGotAddress() {
  sta_.state = StaState::Connected;
  sta_.ip = fromCore(WiFi.localIP());
  sta_.gateway = fromCore(WiFi.gatewayIP());
  sta_.mask = fromCore(WiFi.subnetMask());
  sta_.dns = fromCore(WiFi.dnsIP());
  sta_.failReason = 0;
  emit(NetEvent::StaGotIp);
}

void Net::onDisconnected(uint8_t reason) {
  if (reason == kWifiReasonLeft || sta_.state == StaState::Idle) return;
  const bool was = sta_.state == StaState::Connected;
  sta_.state = was ? StaState::Lost : StaState::Failed;
  sta_.ip = Ip4();
  sta_.failReason = reason;
  sta_.fail = wifiFailOf(reason);
  // The chip would go on trying by itself.
  wifi_station_disconnect();
  emit(was ? NetEvent::StaLost : NetEvent::StaFailed);
}

void Net::onApClient(bool joined) {
  emit(joined ? NetEvent::ApClientJoined : NetEvent::ApClientLeft);
}

Status Net::staConnect(const char* ssid, const char* password) {
  if (!ready_) return Status::Denied;
  if (!ssid || !*ssid || strlen(ssid) > 32) return Status::Invalid;
  const char* key = password ? password : "";
  if (strlen(key) > 63) return Status::Invalid;

  wantSta_ = true;
  applyMode();
  sta_ = StaInfo();
  sta_.state = StaState::Connecting;
  snprintf(sta_.ssid, sizeof(sta_.ssid), "%s", ssid);
  const wl_status_t s = WiFi.begin(ssid, *key ? key : nullptr);
  return s == WL_CONNECT_FAILED ? Status::Io : Status::Ok;
}

Status Net::staDisconnect() {
  if (!ready_) return Status::Ok;
  sta_ = StaInfo();
  if (wantSta_) WiFi.disconnect(false);
  wantSta_ = false;
  applyMode();
  return Status::Ok;
}

void Net::staInfo(StaInfo& out) const {
  out = sta_;
  if (out.state != StaState::Connected) return;
  out.rssi = static_cast<int8_t>(WiFi.RSSI());
  out.channel = static_cast<uint8_t>(WiFi.channel());
}

void Net::staMac(Mac& out) const { WiFi.macAddress(out.b); }

Status Net::apStart(const char* ssid, const char* password, Ip4 ip, uint8_t channel) {
  if (!ready_) return Status::Denied;
  if (!ssid || !*ssid || strlen(ssid) > 32) return Status::Invalid;
  const char* key = password ? password : "";
  if (*key && (strlen(key) < 8 || strlen(key) > 63)) return Status::Invalid;

  wantAp_ = true;
  applyMode();
  const IPAddress own = toCore(ip);
  if (!WiFi.softAPConfig(own, own, IPAddress(255, 255, 255, 0)) ||
      !WiFi.softAP(ssid, *key ? key : nullptr, channel, 0, kApClients)) {
    wantAp_ = false;
    applyMode();
    return Status::Io;
  }
  ap_ = ApInfo();
  ap_.up = true;
  snprintf(ap_.ssid, sizeof(ap_.ssid), "%s", ssid);
  ap_.ip = ip;
  ap_.channel = channel;
  emit(NetEvent::ApStarted);
  return Status::Ok;
}

Status Net::apStop() {
  captiveStop();
  if (ap_.up) {
    WiFi.softAPdisconnect(false);
    emit(NetEvent::ApStopped);
  }
  ap_ = ApInfo();
  wantAp_ = false;
  applyMode();
  return Status::Ok;
}

void Net::apInfo(ApInfo& out) const {
  out = ap_;
  if (!out.up) return;
  out.clients = WiFi.softAPgetStationNum();
  // With one radio the access point follows the channel of the client.
  const int channel = WiFi.channel();
  if (channel > 0) out.channel = static_cast<uint8_t>(channel);
}

// ---------------------------------------------------------------- scan

Status Net::scanStart() {
  if (!ready_) return Status::Denied;
  if (scan_ == ScanState::Running) return Status::Busy;
  scanning_ = true;
  applyMode();
  found_ = 0;
  scan_ = ScanState::Running;
  WiFi.scanNetworksAsync([](int networks) {
    if (g_net) g_net->onScanDone(networks);
  });
  return Status::Ok;
}

void Net::onScanDone(int networks) {
  found_ = 0;
  for (int i = 0; i < networks && found_ < kRecords; ++i) {
    WifiNetwork& n = records_[found_++];
    snprintf(n.ssid, sizeof(n.ssid), "%s", WiFi.SSID(i).c_str());
    n.rssi = static_cast<int8_t>(WiFi.RSSI(i));
    n.channel = static_cast<uint8_t>(WiFi.channel(i));
    n.secured = WiFi.encryptionType(i) != ENC_TYPE_NONE;
  }
  WiFi.scanDelete();
  scan_ = networks >= 0 ? ScanState::Done : ScanState::Failed;
  scanning_ = false;
  modeStale_ = true;
  emit(NetEvent::ScanDone);
}

bool Net::scanResult(size_t index, WifiNetwork& out) const {
  if (index >= found_) return false;
  out = records_[index];
  return true;
}

void Net::scanClear() {
  if (scan_ == ScanState::Running) {
    WiFi.scanDelete();
    scanning_ = false;
    modeStale_ = true;
  }
  found_ = 0;
  scan_ = ScanState::Idle;
}

// ---------------------------------------------------------------- names

Status Net::mdnsStart(const char* hostname, uint16_t httpPort) {
#if LMP_FEATURE_MDNS
  if (!ready_ || !hostname || !*hostname) return Status::Invalid;
  if (mdns_) mdnsStop();
  if (!MDNS.begin(hostname)) return Status::Io;
  MDNS.addService("http", "tcp", httpPort);
  mdns_ = true;
  return Status::Ok;
#else
  (void)hostname;
  (void)httpPort;
  return Status::Unsupported;
#endif
}

void Net::mdnsStop() {
#if LMP_FEATURE_MDNS
  if (mdns_) MDNS.end();
#endif
  mdns_ = false;
}

Status Net::captiveStart(Ip4 address) {
  if (!ready_) return Status::Denied;
  if (captive_) g_dns.stop();
  g_dns.setErrorReplyCode(DNSReplyCode::NoError);
  g_dns.setTTL(60);
  // Every name asked gets the address of the device.
  captive_ = g_dns.start(53, "*", toCore(address));
  return captive_ ? Status::Ok : Status::Io;
}

void Net::captiveStop() {
  if (captive_) g_dns.stop();
  captive_ = false;
}

// ---------------------------------------------------------------- HTTP server

Status HttpServer::on(HttpMethod methods, const char* path, HttpHandler handler) {
  if (!path || !*path || !handler) return Status::Invalid;
  if (routeCount_ >= kRoutes) return Status::NoMem;
  Route& r = routes_[routeCount_++];
  r.methods = static_cast<uint8_t>(methods);
  r.path = path;
  r.handler = handler;
  return Status::Ok;
}

ISseChannel* HttpServer::sse(const char* path) {
  if (!path || !*path) return nullptr;
  if (!channel_.path) channel_.path = path;
  return &channel_;
}

Status HttpServer::start(uint16_t port) {
  g_http = this;
  if (!hooked_) {
    // Every request arrives here: the routes are matched by this class.
    g_server.onNotFound([]() {
      if (g_http) g_http->serve();
    });
    static const char* kept[] = {"Authorization", "Host", "Content-Type"};
    g_server.collectHeaders(kept, sizeof(kept) / sizeof(kept[0]));
    hooked_ = true;
  }
  g_server.begin(port);
  running_ = true;
  return Status::Ok;
}

void HttpServer::stop() {
  if (!running_) return;
  channel_.closeAll();
  g_server.stop();
  running_ = false;
}

void HttpServer::poll() {
  if (running_) g_server.handleClient();
}

void HttpServer::serve() {
  // Answers are short and come in pieces: each one leaves at once, without waiting for the
  // client to acknowledge the one before.
  g_server.client().setNoDelay(true);
  Request request;
  const StringView path = request.path();
  if (channel_.path && g_server.method() == HTTP_GET && path == StringView(channel_.path)) {
    follow(request);
    return;
  }

  Response response;
  const uint8_t method = methodOf(g_server.method());
  bool handled = false;
  for (size_t i = 0; i < routeCount_ && !handled; ++i) {
    const Route& r = routes_[i];
    if (!(r.methods & method)) continue;
    const StringView pattern(r.path);
    const bool match = pattern.endsWith("/*")
                           ? path.startsWith(pattern.substr(0, pattern.size - 1))
                           : path == pattern;
    if (!match) continue;
    r.handler(request, response);
    handled = true;
  }
  if (!handled && notFound_) {
    notFound_(request, response);
    handled = true;
  }
  if (!response.sent()) g_server.send(handled ? 500 : 404, "text/plain", "no answer");
}

// ---------------------------------------------------------------- events

void HttpServer::follow(IHttpRequest& request) {
  if (channel_.gate_ && !channel_.gate_(request)) {
    g_server.send(401, "application/json", "{\"error\":\"auth\",\"msg\":\"login required\"}");
    return;
  }
  WiFiClient* place = nullptr;
  for (WiFiClient& follower : g_followers) {
    if (!follower.connected()) {
      place = &follower;
      break;
    }
  }
  if (!place) {
    g_server.send(503, "application/json", "{\"error\":\"busy\",\"msg\":\"too many clients\"}");
    return;
  }
  // The answer starts and never ends: the client keeps the connection, the server of the
  // core lets it go.
  *place = g_server.client();
  place->setNoDelay(true);
  place->print(F("HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\n"
                 "Cache-Control: no-cache\r\nConnection: keep-alive\r\n\r\n: ok\n\n"));
}

size_t HttpServer::Channel::clients() const {
  size_t n = 0;
  for (WiFiClient& follower : g_followers) {
    if (follower.connected()) ++n;
  }
  return n;
}

void HttpServer::Channel::closeAll() {
  for (WiFiClient& follower : g_followers) follower.stop();
}

void HttpServer::Channel::send(const char* event, StringView data, uint32_t id) {
  static char frame[kFrame];
  int n;
  if (id) {
    n = snprintf(frame, sizeof(frame), "id: %lu\nevent: %s\ndata: %.*s\n\n",
                 static_cast<unsigned long>(id), event ? event : "message",
                 static_cast<int>(data.size), data.data ? data.data : "");
  } else {
    n = snprintf(frame, sizeof(frame), "event: %s\ndata: %.*s\n\n", event ? event : "message",
                 static_cast<int>(data.size), data.data ? data.data : "");
  }
  if (n <= 0 || static_cast<size_t>(n) >= sizeof(frame)) return;
  for (WiFiClient& follower : g_followers) {
    if (!follower.connected()) continue;
    // A client that is not reading loses the event: the system does not wait for it.
    if (follower.availableForWrite() < n) continue;
    follower.write(reinterpret_cast<const uint8_t*>(frame), static_cast<size_t>(n));
  }
}

}  // namespace lmp::hal::esp8266

#endif  // LMP_FEATURE_NET

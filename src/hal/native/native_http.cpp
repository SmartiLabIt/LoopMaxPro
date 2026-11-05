#include "hal/native/native_http.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fstream>
#include <sstream>
#include <thread>

#include "hal/native/native_hal.h"
#include "lmp_config.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace lmp::hal::native {
namespace {

constexpr size_t kLongestHead = 16384;
constexpr intptr_t kNoSocket = -1;

#ifdef _WIN32
using RawSocket = SOCKET;
void closeRaw(intptr_t s) { closesocket(static_cast<SOCKET>(s)); }
bool startSockets() {
  static bool started = false;
  if (!started) {
    WSADATA data;
    started = WSAStartup(MAKEWORD(2, 2), &data) == 0;
  }
  return started;
}
#else
using RawSocket = int;
void closeRaw(intptr_t s) { close(static_cast<int>(s)); }
bool startSockets() { return true; }
#endif

bool sendAll(intptr_t socket, const std::string& data) {
  size_t sent = 0;
  while (sent < data.size()) {
    const int n = static_cast<int>(::send(static_cast<RawSocket>(socket), data.data() + sent,
                                          static_cast<int>(data.size() - sent), 0));
    if (n <= 0) return false;
    sent += static_cast<size_t>(n);
  }
  return true;
}

std::string lower(std::string text) {
  for (char& c : text) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
  }
  return text;
}

int hexValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

std::string urlDecode(const std::string& in, bool plusIsSpace) {
  std::string out;
  for (size_t i = 0; i < in.size(); ++i) {
    if (in[i] == '%' && i + 2 < in.size() && hexValue(in[i + 1]) >= 0 &&
        hexValue(in[i + 2]) >= 0) {
      out += static_cast<char>(hexValue(in[i + 1]) * 16 + hexValue(in[i + 2]));
      i += 2;
    } else if (in[i] == '+' && plusIsSpace) {
      out += ' ';
    } else {
      out += in[i];
    }
  }
  return out;
}

uint8_t methodOf(const std::string& name) {
  if (name == "GET") return static_cast<uint8_t>(HttpMethod::Get);
  if (name == "POST") return static_cast<uint8_t>(HttpMethod::Post);
  if (name == "PUT") return static_cast<uint8_t>(HttpMethod::Put);
  if (name == "DELETE") return static_cast<uint8_t>(HttpMethod::Delete);
  if (name == "OPTIONS") return static_cast<uint8_t>(HttpMethod::Options);
  return 0;
}

const char* reasonOf(uint16_t code) {
  switch (code) {
    case 200: return "OK";
    case 202: return "Accepted";
    case 204: return "No Content";
    case 302: return "Found";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 409: return "Conflict";
    case 413: return "Payload Too Large";
    case 429: return "Too Many Requests";
    case 500: return "Internal Server Error";
    case 501: return "Not Implemented";
    case 503: return "Service Unavailable";
    case 504: return "Gateway Timeout";
    case 507: return "Insufficient Storage";
    default:  return "Status";
  }
}

std::string headerOf(const HttpHeaders& headers, const char* name) {
  const std::string wanted = lower(name);
  for (const auto& h : headers) {
    if (lower(h.first) == wanted) return h.second;
  }
  return std::string();
}

bool copyOut(const std::string& value, char* out, size_t cap) {
  if (!out || value.size() + 1 > cap) return false;
  memcpy(out, value.c_str(), value.size() + 1);
  return true;
}

}  // namespace

std::string HttpResult::header(const char* name) const { return headerOf(headers, name); }

// ---------------------------------------------------------------- request and response

class HttpServer::Request : public IHttpRequest {
 public:
  // upload: the body is handed over in pieces, nothing of it is kept here.
  explicit Request(const HttpCall& call, bool upload = false)
      : headers_(call.headers), from_(call.from) {
    method_ = methodOf(call.method);
    const size_t mark = call.target.find('?');
    path_ = urlDecode(call.target.substr(0, mark), false);
    if (mark != std::string::npos) query_ = call.target.substr(mark + 1);
    contentType_ = call.contentType.empty() ? headerOf(headers_, "Content-Type") : call.contentType;
    if (upload) return;
    tooLong_ = call.body.size() > LMP_HTTP_BODY_MAX;
    body_ = tooLong_ ? call.body.substr(0, LMP_HTTP_BODY_MAX) : call.body;
  }

  HttpMethod method() const override { return static_cast<HttpMethod>(method_); }
  bool known() const { return method_ != 0; }
  StringView path() const override { return StringView(path_.data(), path_.size()); }
  StringView body() const override { return StringView(body_.data(), body_.size()); }
  bool tooLong() const override { return tooLong_; }
  StringView contentType() const override {
    return StringView(contentType_.data(), contentType_.size());
  }
  Ip4 clientIp() const override { return from_; }

  bool query(const char* name, char* out, size_t cap) const override {
    size_t start = 0;
    while (start < query_.size()) {
      size_t end = query_.find('&', start);
      if (end == std::string::npos) end = query_.size();
      const std::string pair = query_.substr(start, end - start);
      const size_t equal = pair.find('=');
      const std::string key = urlDecode(pair.substr(0, equal), true);
      if (key == name) {
        const std::string value =
            equal == std::string::npos ? std::string() : urlDecode(pair.substr(equal + 1), true);
        return copyOut(value, out, cap);
      }
      start = end + 1;
    }
    return false;
  }

  bool header(const char* name, char* out, size_t cap) const override {
    const std::string wanted = lower(name);
    for (const auto& h : headers_) {
      if (lower(h.first) == wanted) return copyOut(h.second, out, cap);
    }
    return false;
  }

 private:
  uint8_t method_ = 0;
  std::string path_;
  std::string query_;
  std::string body_;
  std::string contentType_;
  HttpHeaders headers_;
  Ip4 from_;
  bool tooLong_ = false;
};

class HttpServer::Response : public IHttpResponse {
 public:
  explicit Response(Fs& fs) : fs_(fs) {}

  void status(uint16_t code) override { result.status = code; }
  void header(const char* name, const char* value) override {
    result.headers.emplace_back(name ? name : "", value ? value : "");
  }
  void send(const char* contentType, StringView body) override {
    if (!result.status) result.status = 200;
    result.contentType = contentType ? contentType : "";
    result.body.assign(body.data ? body.data : "", body.size);
  }
  void sendFile(const char* fsPath, const char* contentType, bool gzip,
                uint32_t cacheSeconds) override {
    std::ifstream in(fs_.root() + fsPath, std::ios::binary);
    if (!in) {
      result.status = 404;
      result.contentType = "text/plain";
      result.body = "not found";
      return;
    }
    std::ostringstream all;
    all << in.rdbuf();
    result.body = all.str();
    if (!result.status) result.status = 200;
    result.contentType = contentType ? contentType : "";
    result.file = fsPath;
    result.gzip = gzip;
    result.cacheSeconds = cacheSeconds;
    if (gzip) result.headers.emplace_back("Content-Encoding", "gzip");
    result.headers.emplace_back(
        "Cache-Control", cacheSeconds ? "public, max-age=" + std::to_string(cacheSeconds) +
                                            ", immutable"
                                      : std::string("no-cache"));
  }
  void redirect(const char* url) override {
    result.status = 302;
    result.location = url ? url : "";
    result.headers.emplace_back("Location", result.location);
  }

  HttpResult result;

 private:
  Fs& fs_;
};

// ---------------------------------------------------------------- events

class HttpServer::Channel : public ISseChannel {
 public:
  explicit Channel(const char* path) : path_(path) {}

  void setGate(HttpGate gate) override { gate_ = gate; }

  void send(const char* event, StringView data, uint32_t id) override {
    std::string frame;
    if (id) frame += "id: " + std::to_string(id) + "\n";
    frame += std::string("event: ") + (event ? event : "message") + "\n";
    frame += "data: " + std::string(data.data ? data.data : "", data.size) + "\n\n";

    std::lock_guard<std::mutex> lock(mutex_);
    for (size_t i = 0; i < clients_.size();) {
      Client& c = clients_[i];
      if (c.socket == kNoSocket) {
        c.pending += frame;
      } else if (!sendAll(c.socket, frame)) {
        closeRaw(c.socket);
        clients_.erase(clients_.begin() + static_cast<long>(i));
        continue;
      }
      ++i;
    }
  }

  size_t clients() const override {
    std::lock_guard<std::mutex> lock(mutex_);
    return clients_.size();
  }

  void closeAll() override {
    std::lock_guard<std::mutex> lock(mutex_);
    for (Client& c : clients_) {
      if (c.socket != kNoSocket) closeRaw(c.socket);
    }
    clients_.clear();
  }

  bool opens(IHttpRequest& req) const { return !gate_ || gate_(req); }

  // Returns the client, or -1 when there is no room.
  int add(intptr_t socket) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (clients_.size() >= static_cast<size_t>(LMP_WEB_EVENT_CLIENTS)) return -1;
    clients_.push_back(Client{nextId_, socket, std::string()});
    return nextId_++;
  }

  // False when the client was already gone.
  bool remove(int id) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (size_t i = 0; i < clients_.size(); ++i) {
      if (clients_[i].id == id) {
        clients_.erase(clients_.begin() + static_cast<long>(i));
        return true;
      }
    }
    return false;
  }

  std::string take(int id) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (Client& c : clients_) {
      if (c.id == id) {
        std::string out;
        out.swap(c.pending);
        return out;
      }
    }
    return std::string();
  }

  const std::string& path() const { return path_; }

 private:
  struct Client {
    int id;
    intptr_t socket;
    std::string pending;
  };

  mutable std::mutex mutex_;
  std::vector<Client> clients_;
  HttpGate gate_;
  std::string path_;
  int nextId_ = 1;
};

// ---------------------------------------------------------------- server

HttpServer::HttpServer(Fs& fs) : fs_(fs) {}

HttpServer::~HttpServer() {
  stop();
  delete channel_;
}

void HttpServer::reset() {
  stop();
  routes_.clear();
  uploads_.clear();
  uploadChunk_ = 1024;
  notFound_ = HttpHandler();
  delete channel_;
  channel_ = nullptr;
  listen_ = false;
}

Status HttpServer::on(HttpMethod methods, const char* path, HttpHandler handler) {
  if (!path || !*path || !handler) return Status::Invalid;
  routes_.push_back(Route{static_cast<uint8_t>(methods), path, handler});
  return Status::Ok;
}

Status HttpServer::onUpload(const char* path, HttpUploadHandler parts, HttpHandler done) {
  if (!path || !*path || !parts || !done) return Status::Invalid;
  uploads_.push_back(Upload{path, parts, done});
  return Status::Ok;
}

const HttpServer::Upload* HttpServer::findUpload(const std::string& method,
                                                 const std::string& target) const {
  if (method != "POST") return nullptr;
  const std::string path = urlDecode(target.substr(0, target.find('?')), false);
  for (const Upload& u : uploads_) {
    if (u.path == path) return &u;
  }
  return nullptr;
}

ISseChannel* HttpServer::sse(const char* path) {
  if (!path || !*path) return nullptr;
  if (!channel_) channel_ = new Channel(path);
  return channel_;
}

Status HttpServer::start(uint16_t port) {
  if (running_) return Status::Ok;
  port_ = port;
  running_ = true;
  if (!listen_) return Status::Ok;

  // The port of the build may be taken by another program of this computer.
  if (const char* wanted = getenv("LMP_SIM_PORT")) {
    const long chosen = strtol(wanted, nullptr, 10);
    if (chosen > 0 && chosen < 65536) port_ = port = static_cast<uint16_t>(chosen);
  }

  if (!startSockets()) return Status::Io;
  const RawSocket s = socket(AF_INET, SOCK_STREAM, 0);
#ifdef _WIN32
  if (s == INVALID_SOCKET) return Status::Io;
#else
  if (s < 0) return Status::Io;
#endif
  const int yes = 1;
  setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes));
  sockaddr_in where;
  memset(&where, 0, sizeof(where));
  where.sin_family = AF_INET;
  where.sin_port = htons(port);
  // This computer only: the simulator is not offered to the network.
  where.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (bind(s, reinterpret_cast<sockaddr*>(&where), sizeof(where)) != 0 || listen(s, 8) != 0) {
    closeRaw(static_cast<intptr_t>(s));
    running_ = false;
    return Status::Busy;
  }
  listener_ = static_cast<intptr_t>(s);
  listening_ = true;
  std::thread(&HttpServer::listenLoop, this).detach();
  return Status::Ok;
}

void HttpServer::stop() {
  running_ = false;
  if (listening_.exchange(false) && listener_ != kNoSocket) {
    closeRaw(listener_);
    listener_ = kNoSocket;
  }
  if (channel_) channel_->closeAll();
}

void HttpServer::dispatch(Request& req, Response& res) {
  const std::string path(req.path().data, req.path().size);
  for (const Route& r : routes_) {
    if (!(r.methods & static_cast<uint8_t>(req.method()))) continue;
    const bool prefix = r.path.size() >= 2 && r.path.compare(r.path.size() - 2, 2, "/*") == 0;
    const bool match = prefix ? path.compare(0, r.path.size() - 1, r.path, 0, r.path.size() - 1) == 0
                              : path == r.path;
    if (match) {
      r.handler(req, res);
      return;
    }
  }
  if (notFound_) {
    notFound_(req, res);
  } else {
    res.status(404);
    res.send("text/plain", StringView("not found"));
  }
}

// ---------------------------------------------------------------- hooks for the tests

HttpResult HttpServer::request(const HttpCall& call) {
  Response res(fs_);
  if (!running_) return res.result;

  if (const Upload* upload = findUpload(call.method, call.target)) {
    Request req(call, true);
    HttpUploadPart part;
    part.total = call.body.size();
    bool going = upload->parts(req, part);
    const size_t limit = call.dropAfter < call.body.size() ? call.dropAfter : call.body.size();
    while (going && part.received < limit) {
      const size_t n = limit - part.received < uploadChunk_ ? limit - part.received : uploadChunk_;
      part.phase = HttpUploadPhase::Data;
      part.data = Span<const uint8_t>(
          reinterpret_cast<const uint8_t*>(call.body.data()) + part.received, n);
      part.received += n;
      going = upload->parts(req, part);
    }
    part.data = Span<const uint8_t>();
    part.phase = going && limit == call.body.size() ? HttpUploadPhase::End
                                                    : HttpUploadPhase::Aborted;
    // A refusal at the beginning opens nothing: there is nothing to close.
    if (part.received || going) upload->parts(req, part);
    upload->done(req, res);
    return res.result;
  }

  Request req(call);
  if (!req.known()) {
    res.status(405);
    res.send("text/plain", StringView("method not allowed"));
    return res.result;
  }
  dispatch(req, res);
  return res.result;
}

HttpResult HttpServer::get(const char* target, const char* token) {
  HttpCall call;
  call.target = target;
  if (token) call.headers.emplace_back("Authorization", std::string("Bearer ") + token);
  return request(call);
}

HttpResult HttpServer::post(const char* target, const char* body, const char* token,
                            const char* contentType) {
  HttpCall call;
  call.method = "POST";
  call.target = target;
  call.body = body ? body : "";
  call.contentType = contentType ? contentType : "";
  if (token) call.headers.emplace_back("Authorization", std::string("Bearer ") + token);
  return request(call);
}

int HttpServer::follow(const char* token) {
  if (!running_ || !channel_) return -1;
  HttpCall call;
  call.target = channel_->path();
  if (token) call.target += std::string("?token=") + token;
  Request req(call);
  return channel_->opens(req) ? channel_->add(kNoSocket) : -1;
}

void HttpServer::unfollow(int client) {
  if (channel_) channel_->remove(client);
}

std::string HttpServer::taken(int client) {
  return channel_ ? channel_->take(client) : std::string();
}

// ---------------------------------------------------------------- real port

void HttpServer::listenLoop() {
  while (listening_) {
    sockaddr_in peer;
    memset(&peer, 0, sizeof(peer));
#ifdef _WIN32
    int length = sizeof(peer);
#else
    socklen_t length = sizeof(peer);
#endif
    const RawSocket client =
        accept(static_cast<RawSocket>(listener_), reinterpret_cast<sockaddr*>(&peer), &length);
#ifdef _WIN32
    if (client == INVALID_SOCKET) {
#else
    if (client < 0) {
#endif
      if (!listening_) break;
      continue;
    }
    const uint32_t address = ntohl(peer.sin_addr.s_addr);
    // The browser on this computer plays a client of the access point: it gets in without a
    // login while the access point is up, as on a device.
    const Ip4 from = (address >> 24) == 127
                         ? Ip4::of(192, 168, 4, 2)
                         : Ip4::of(static_cast<uint8_t>(address >> 24),
                                   static_cast<uint8_t>(address >> 16),
                                   static_cast<uint8_t>(address >> 8),
                                   static_cast<uint8_t>(address));
    std::thread(&HttpServer::serveSocket, this, static_cast<intptr_t>(client), from).detach();
  }
}

void HttpServer::serveSocket(intptr_t socket, Ip4 from) {
  std::string data;
  char chunk[2048];
  size_t headEnd = std::string::npos;
  while (headEnd == std::string::npos && data.size() < kLongestHead) {
    const int n = static_cast<int>(recv(static_cast<RawSocket>(socket), chunk, sizeof(chunk), 0));
    if (n <= 0) {
      closeRaw(socket);
      return;
    }
    data.append(chunk, static_cast<size_t>(n));
    headEnd = data.find("\r\n\r\n");
  }
  if (headEnd == std::string::npos) {
    closeRaw(socket);
    return;
  }

  HttpCall call;
  call.from = from;
  std::istringstream head(data.substr(0, headEnd));
  std::string line;
  std::getline(head, line);
  {
    std::istringstream first(line);
    first >> call.method >> call.target;
  }
  size_t length = 0;
  while (std::getline(head, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const size_t colon = line.find(':');
    if (colon == std::string::npos) continue;
    std::string value = line.substr(colon + 1);
    while (!value.empty() && value.front() == ' ') value.erase(0, 1);
    const std::string name = line.substr(0, colon);
    if (lower(name) == "content-length") length = static_cast<size_t>(strtoul(value.c_str(), nullptr, 10));
    call.headers.emplace_back(name, value);
  }
  call.body = data.substr(headEnd + 4);
  // A long body is read whole here, on the PC, and handed over in pieces by request().
  const bool upload = findUpload(call.method, call.target) != nullptr;
  // Otherwise one byte more than the limit is enough to know the body is too long.
  const size_t wanted =
      !upload && length > LMP_HTTP_BODY_MAX + 1 ? LMP_HTTP_BODY_MAX + 1 : length;
  while (call.body.size() < wanted) {
    const int n = static_cast<int>(recv(static_cast<RawSocket>(socket), chunk, sizeof(chunk), 0));
    if (n <= 0) break;
    call.body.append(chunk, static_cast<size_t>(n));
  }

  // A page served by another program of this computer can call the simulator.
  const std::string cors =
      "Access-Control-Allow-Origin: *\r\n"
      "Access-Control-Allow-Headers: Authorization, Content-Type\r\n"
      "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n";

  if (call.method == "OPTIONS") {
    sendAll(socket, "HTTP/1.1 204 No Content\r\n" + cors + "Content-Length: 0\r\nConnection: close\r\n\r\n");
    closeRaw(socket);
    return;
  }

  Request req(call);
  const std::string path(req.path().data, req.path().size);
  if (running_ && channel_ && call.method == "GET" && path == channel_->path()) {
    const int client = channel_->opens(req) ? channel_->add(socket) : -1;
    if (client < 0) {
      sendAll(socket, "HTTP/1.1 401 Unauthorized\r\n" + cors +
                          "Content-Length: 0\r\nConnection: close\r\n\r\n");
      closeRaw(socket);
      return;
    }
    sendAll(socket, "HTTP/1.1 200 OK\r\n" + cors +
                        "Content-Type: text/event-stream\r\nCache-Control: no-cache\r\n"
                        "Connection: keep-alive\r\n\r\n");
    // Nothing more arrives from a client that follows the events: this waits for its end.
    while (recv(static_cast<RawSocket>(socket), chunk, sizeof(chunk), 0) > 0) {
    }
    // Still listed: nobody has closed the connection yet.
    if (channel_->remove(client)) closeRaw(socket);
    return;
  }

  const HttpResult result = request(call);
  const uint16_t code = result.status ? result.status : 503;
  std::string out = "HTTP/1.1 " + std::to_string(code) + " " + reasonOf(code) + "\r\n" + cors;
  if (!result.contentType.empty()) out += "Content-Type: " + result.contentType + "\r\n";
  for (const auto& h : result.headers) out += h.first + ": " + h.second + "\r\n";
  out += "Content-Length: " + std::to_string(result.body.size()) + "\r\nConnection: close\r\n\r\n";
  sendAll(socket, out + result.body);
  closeRaw(socket);
}

}  // namespace lmp::hal::native

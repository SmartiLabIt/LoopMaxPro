#include "hal/esp32/esp32_http.h"

#include <fcntl.h>
#include <lwip/sockets.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "hal/api/memory.h"
#include "hal/esp32/esp32_paths.h"

namespace lmp::hal::esp32 {
namespace {

constexpr size_t kPath = 128;
constexpr size_t kQuery = 256;
// A file leaves in pieces this large: every piece of a chunked answer costs two small packets
// more, and over Wi-Fi each packet costs its time.
constexpr size_t kFileChunk = 4096;
constexpr size_t kDrainMax = 16384;
// A long body that was refused is read to its end, so the client gets the answer: up to
// this size, which is more than any image the boards take.
constexpr size_t kUploadDrainMax = 8u * 1024 * 1024;
// Silences of the client, three seconds each, before a transfer is given up.
constexpr int kUploadSilences = 5;
constexpr int kNoSocket = -1;

// The server has one task: one request at a time, so one set of buffers is enough.
HttpServer* g_server = nullptr;
char g_body[LMP_HTTP_BODY_MAX + 1];
char g_chunk[kFileChunk];

class Lock {
 public:
  explicit Lock(SemaphoreHandle_t handle) : handle_(handle) {
    if (handle_) xSemaphoreTake(handle_, portMAX_DELAY);
  }
  ~Lock() {
    if (handle_) xSemaphoreGive(handle_);
  }

 private:
  SemaphoreHandle_t handle_;
};

int hexValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// Decodes percent codes in place, and plus signs where they mean a space.
void urlDecode(char* text, bool plusIsSpace) {
  char* out = text;
  for (const char* in = text; *in; ++in) {
    if (*in == '%' && hexValue(in[1]) >= 0 && hexValue(in[2]) >= 0) {
      *out++ = static_cast<char>(hexValue(in[1]) * 16 + hexValue(in[2]));
      in += 2;
    } else if (*in == '+' && plusIsSpace) {
      *out++ = ' ';
    } else {
      *out++ = *in;
    }
  }
  *out = '\0';
}

const char* statusLine(uint16_t code) {
  switch (code) {
    case 200: return "200 OK";
    case 202: return "202 Accepted";
    case 204: return "204 No Content";
    case 302: return "302 Found";
    case 400: return "400 Bad Request";
    case 401: return "401 Unauthorized";
    case 403: return "403 Forbidden";
    case 404: return "404 Not Found";
    case 405: return "405 Method Not Allowed";
    case 409: return "409 Conflict";
    case 413: return "413 Payload Too Large";
    case 429: return "429 Too Many Requests";
    case 501: return "501 Not Implemented";
    case 503: return "503 Service Unavailable";
    case 504: return "504 Gateway Timeout";
    case 507: return "507 Insufficient Storage";
    default:  return "500 Internal Server Error";
  }
}

uint8_t methodOf(int method) {
  switch (method) {
    case HTTP_GET:     return static_cast<uint8_t>(HttpMethod::Get);
    case HTTP_POST:    return static_cast<uint8_t>(HttpMethod::Post);
    case HTTP_PUT:     return static_cast<uint8_t>(HttpMethod::Put);
    case HTTP_DELETE:  return static_cast<uint8_t>(HttpMethod::Delete);
    case HTTP_OPTIONS: return static_cast<uint8_t>(HttpMethod::Options);
    default:           return 0;
  }
}

// ---------------------------------------------------------------- request

class Request : public IHttpRequest {
 public:
  // upload: the body is handed over in pieces by who asked for it, nothing is read here.
  explicit Request(httpd_req_t* req, bool upload = false) : req_(req) {
    // Path without the query, decoded.
    const char* uri = req->uri;
    size_t n = 0;
    while (uri[n] && uri[n] != '?' && n + 1 < sizeof(path_)) {
      path_[n] = uri[n];
      ++n;
    }
    path_[n] = '\0';
    urlDecode(path_, false);

    type_[0] = '\0';
    httpd_req_get_hdr_value_str(req, "Content-Type", type_, sizeof(type_));
    if (upload) return;

    // The body, up to the limit.
    size_t left = req->content_len;
    tooLong_ = left > LMP_HTTP_BODY_MAX;
    if (tooLong_) left = LMP_HTTP_BODY_MAX;
    while (bodyLength_ < left) {
      const int got = httpd_req_recv(req, g_body + bodyLength_, left - bodyLength_);
      if (got == HTTPD_SOCK_ERR_TIMEOUT) continue;
      if (got <= 0) break;
      bodyLength_ += static_cast<size_t>(got);
    }
    g_body[bodyLength_] = '\0';

    // What is beyond the limit is read and thrown away: closing the connection with bytes
    // still to read would take the answer away from the client too. Only up to a point:
    // somebody sending megabytes is simply cut off.
    if (tooLong_) {
      size_t extra = req->content_len - LMP_HTTP_BODY_MAX;
      drained_ = extra <= kDrainMax;
      while (drained_ && extra > 0) {
        const size_t want = extra < sizeof(g_chunk) ? extra : sizeof(g_chunk);
        const int got = httpd_req_recv(req, g_chunk, want);
        if (got == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (got <= 0) {
          drained_ = false;
          break;
        }
        extra -= static_cast<size_t>(got);
      }
    }
  }

  // False when part of the request is still on the line: the connection cannot go on.
  bool whole() const { return !tooLong_ || drained_; }

  HttpMethod method() const override { return static_cast<HttpMethod>(methodOf(req_->method)); }
  StringView path() const override { return StringView(path_); }
  StringView body() const override { return StringView(g_body, bodyLength_); }
  bool tooLong() const override { return tooLong_; }
  StringView contentType() const override { return StringView(type_); }

  bool query(const char* name, char* out, size_t cap) const override {
    char all[kQuery];
    if (!out || cap == 0) return false;
    if (httpd_req_get_url_query_str(req_, all, sizeof(all)) != ESP_OK) return false;
    if (httpd_query_key_value(all, name, out, cap) != ESP_OK) return false;
    urlDecode(out, true);
    return true;
  }

  bool header(const char* name, char* out, size_t cap) const override {
    return out && cap && httpd_req_get_hdr_value_str(req_, name, out, cap) == ESP_OK;
  }

  Ip4 clientIp() const override {
    sockaddr_storage peer;
    socklen_t length = sizeof(peer);
    if (getpeername(httpd_req_to_sockfd(req_), reinterpret_cast<sockaddr*>(&peer), &length) != 0) {
      return Ip4();
    }
    if (peer.ss_family == AF_INET) {
      const uint32_t a = ntohl(reinterpret_cast<sockaddr_in*>(&peer)->sin_addr.s_addr);
      return Ip4::of(static_cast<uint8_t>(a >> 24), static_cast<uint8_t>(a >> 16),
                     static_cast<uint8_t>(a >> 8), static_cast<uint8_t>(a));
    }
#if LWIP_IPV6
    if (peer.ss_family == AF_INET6) {
      // An IPv4 client seen through an IPv6 socket: the address is in the last four bytes.
      const uint8_t* b = reinterpret_cast<sockaddr_in6*>(&peer)->sin6_addr.s6_addr;
      return Ip4::of(b[12], b[13], b[14], b[15]);
    }
#endif
    return Ip4();
  }

 private:
  httpd_req_t* req_;
  char path_[kPath];
  char type_[64];
  size_t bodyLength_ = 0;
  bool tooLong_ = false;
  bool drained_ = false;
};

// ---------------------------------------------------------------- response

class Response : public IHttpResponse {
 public:
  explicit Response(httpd_req_t* req) : req_(req) {}

  void status(uint16_t code) override { httpd_resp_set_status(req_, statusLine(code)); }
  void header(const char* name, const char* value) override {
    if (name && value) httpd_resp_set_hdr(req_, name, value);
  }

  void send(const char* contentType, StringView body) override {
    if (contentType) httpd_resp_set_type(req_, contentType);
    sent_ = httpd_resp_send(req_, body.data ? body.data : "", static_cast<ssize_t>(body.size)) ==
            ESP_OK;
  }

  void sendFile(const char* fsPath, const char* contentType, bool gzip,
                uint32_t cacheSeconds) override {
    char full[kPath + 16];
    snprintf(full, sizeof(full), "%s%s", kFsMountPoint, fsPath ? fsPath : "");
    const int file = open(full, O_RDONLY);
    if (file < 0) {
      status(404);
      send("text/plain", StringView("not found"));
      return;
    }
    char cache[56];
    if (cacheSeconds) {
      snprintf(cache, sizeof(cache), "public, max-age=%lu, immutable",
               static_cast<unsigned long>(cacheSeconds));
    } else {
      snprintf(cache, sizeof(cache), "no-cache");
    }
    if (contentType) httpd_resp_set_type(req_, contentType);
    httpd_resp_set_hdr(req_, "Cache-Control", cache);
    if (gzip) httpd_resp_set_hdr(req_, "Content-Encoding", "gzip");

    // While the file goes, the stack may join the small writes (the size of a piece and its
    // end) to the data in full packets; before the last piece every byte goes out at once.
    // Measured on the ESP32 N4: 57 KB in 2.7 s with 1 KB pieces, each piece three packets.
    const int socket = httpd_req_to_sockfd(req_);
    int nodelay = 0;
    setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));
    esp_err_t e = ESP_OK;
    for (;;) {
      const ssize_t n = read(file, g_chunk, sizeof(g_chunk));
      if (n <= 0) break;
      e = httpd_resp_send_chunk(req_, g_chunk, n);
      if (e != ESP_OK) break;
    }
    close(file);
    nodelay = 1;
    setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));
    // The empty chunk ends the answer. After a failure the connection cannot be used again.
    if (e == ESP_OK) e = httpd_resp_send_chunk(req_, nullptr, 0);
    sent_ = e == ESP_OK;
    broken_ = e != ESP_OK;
  }

  void redirect(const char* url) override {
    status(302);
    httpd_resp_set_hdr(req_, "Location", url ? url : "/");
    send("text/plain", StringView(""));
  }

  bool sent() const { return sent_; }
  bool broken() const { return broken_; }

 private:
  httpd_req_t* req_;
  bool sent_ = false;
  bool broken_ = false;
};

}  // namespace

// ---------------------------------------------------------------- server

HttpServer::HttpServer() : channel_(*this) {
  for (int& socket : followers_) socket = kNoSocket;
}

Status HttpServer::on(HttpMethod methods, const char* path, HttpHandler handler) {
  if (!path || !*path || !handler) return Status::Invalid;
  if (routeCount_ >= kRoutes) return Status::NoMem;
  Route& r = routes_[routeCount_++];
  r.methods = static_cast<uint8_t>(methods);
  r.path = path;
  r.handler = handler;
  return Status::Ok;
}

Status HttpServer::onUpload(const char* path, HttpUploadHandler parts, HttpHandler done) {
  if (!path || !*path || !parts || !done) return Status::Invalid;
  if (uploadCount_ >= kUploads) return Status::NoMem;
  Upload& u = uploads_[uploadCount_++];
  u.path = path;
  u.parts = parts;
  u.done = done;
  return Status::Ok;
}

ISseChannel* HttpServer::sse(const char* path) {
  if (!path || !*path) return nullptr;
  if (!channel_.path) channel_.path = path;
  return &channel_;
}

Status HttpServer::start(uint16_t port) {
  if (server_) return Status::Ok;
  if (!lock_) lock_ = xSemaphoreCreateMutex();
  if (!frames_) {
    frames_ = static_cast<char(*)[kFrame]>(hal::allocLarge(sizeof(char[kFrame]) * LMP_WEB_EVENT_QUEUE));
    if (!frames_) return Status::NoMem;
  }
  g_server = this;

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = port;
  config.stack_size = 6144;
  config.max_uri_handlers = 8;
  config.max_open_sockets = LMP_WEB_MAX_SOCKETS;
  config.uri_match_fn = httpd_uri_match_wildcard;
  // A new client takes the place of the one silent for longest.
  config.lru_purge_enable = true;
  // A client that stops reading holds the server for this long at most, in seconds.
  config.send_wait_timeout = 3;
  config.recv_wait_timeout = 3;
  config.open_fn = &HttpServer::onOpen;
  config.close_fn = &HttpServer::onClose;
  if (httpd_start(&server_, &config) != ESP_OK) {
    server_ = nullptr;
    return Status::Io;
  }

  // Every request arrives here: the routes are matched by this class.
  const httpd_method_t methods[] = {HTTP_GET, HTTP_POST, HTTP_PUT, HTTP_DELETE, HTTP_OPTIONS};
  for (httpd_method_t method : methods) {
    httpd_uri_t all;
    memset(&all, 0, sizeof(all));
    all.uri = "/*";
    all.method = method;
    all.handler = &HttpServer::onRequest;
    all.user_ctx = this;
    if (httpd_register_uri_handler(server_, &all) != ESP_OK) {
      stop();
      return Status::NoMem;
    }
  }
  return Status::Ok;
}

void HttpServer::stop() {
  if (!server_) return;
  channel_.closeAll();
  httpd_stop(server_);
  server_ = nullptr;
  Lock lock(lock_);
  for (int& socket : followers_) socket = kNoSocket;
  frameCount_ = 0;
  flushQueued_ = false;
}

esp_err_t HttpServer::onRequest(httpd_req_t* req) {
  return static_cast<HttpServer*>(req->user_ctx)->serve(req);
}

esp_err_t HttpServer::receive(httpd_req_t* req, const Upload& upload) {
  Request request(req, true);
  Response response(req);
  HttpUploadPart part;
  part.total = req->content_len;

  size_t left = req->content_len;
  bool lost = false;   // the client went away
  int silences = 0;
  bool going = upload.parts(request, part);
  while (going && left > 0) {
    const size_t want = left < sizeof(g_chunk) ? left : sizeof(g_chunk);
    const int got = httpd_req_recv(req, g_chunk, want);
    if (got == HTTPD_SOCK_ERR_TIMEOUT && ++silences < kUploadSilences) continue;
    if (got <= 0) {
      lost = true;
      break;
    }
    silences = 0;
    left -= static_cast<size_t>(got);
    part.phase = HttpUploadPhase::Data;
    part.data = Span<const uint8_t>(reinterpret_cast<const uint8_t*>(g_chunk),
                                    static_cast<size_t>(got));
    part.received += static_cast<size_t>(got);
    going = upload.parts(request, part);
  }
  part.data = Span<const uint8_t>();
  part.phase = going && !lost ? HttpUploadPhase::End : HttpUploadPhase::Aborted;
  // A refusal at the beginning opened nothing: there is nothing to close.
  if (part.received || going) upload.parts(request, part);

  // Refused half way: what the client is still sending is read and thrown away.
  while (!lost && left > 0 && req->content_len <= kUploadDrainMax) {
    const size_t want = left < sizeof(g_chunk) ? left : sizeof(g_chunk);
    const int got = httpd_req_recv(req, g_chunk, want);
    if (got == HTTPD_SOCK_ERR_TIMEOUT && ++silences < kUploadSilences) continue;
    if (got <= 0) {
      lost = true;
      break;
    }
    silences = 0;
    left -= static_cast<size_t>(got);
  }

  upload.done(request, response);
  if (!response.sent()) {
    response.status(500);
    response.send("text/plain", StringView("no answer"));
  }
  return (lost || left > 0) ? ESP_FAIL : ESP_OK;
}

esp_err_t HttpServer::serve(httpd_req_t* req) {
  if (req->method == HTTP_POST) {
    // The path only: what follows the question mark belongs to who receives.
    const char* uri = req->uri;
    size_t length = 0;
    while (uri[length] && uri[length] != '?') ++length;
    for (size_t i = 0; i < uploadCount_; ++i) {
      if (StringView(uri, length) == StringView(uploads_[i].path)) return receive(req, uploads_[i]);
    }
  }

  Request request(req);
  const StringView path = request.path();

  if (channel_.path && req->method == HTTP_GET && path == StringView(channel_.path)) {
    return follow(req, request);
  }

  Response response(req);
  const uint8_t method = methodOf(req->method);
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
  if (!response.sent() && !response.broken()) {
    response.status(handled ? 500 : 404);
    response.send("text/plain", StringView(handled ? "no answer" : "not found"));
  }
  // With part of the request still on the line, or the answer left in the middle, the
  // connection ends here.
  if (!request.whole() || response.broken()) return ESP_FAIL;
  return ESP_OK;
}

esp_err_t HttpServer::onOpen(httpd_handle_t, int socket) {
  // Answers are short and come in two pieces, headers and body: without this the second
  // piece waits for the client to acknowledge the first, some tens of milliseconds.
  const int on = 1;
  setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));
  return ESP_OK;
}

// ---------------------------------------------------------------- events

esp_err_t HttpServer::follow(httpd_req_t* req, IHttpRequest& request) {
  Response response(req);
  if (channel_.gate_ && !channel_.gate_(request)) {
    response.status(401);
    response.send("application/json", StringView("{\"error\":\"auth\",\"msg\":\"login required\"}"));
    return ESP_OK;
  }

  const int socket = httpd_req_to_sockfd(req);
  bool placed = false;
  {
    Lock lock(lock_);
    for (int& follower : followers_) {
      if (follower == kNoSocket) {
        follower = socket;
        placed = true;
        break;
      }
    }
  }
  if (!placed) {
    response.status(503);
    response.send("application/json", StringView("{\"error\":\"busy\",\"msg\":\"too many clients\"}"));
    return ESP_OK;
  }

  // The answer starts and never ends: every event is one more piece of it.
  httpd_resp_set_type(req, "text/event-stream");
  httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
  httpd_resp_set_hdr(req, "X-Accel-Buffering", "no");
  static const char hello[] = ": ok\n\n";
  if (httpd_resp_send_chunk(req, hello, sizeof(hello) - 1) != ESP_OK) {
    forget(socket);
    return ESP_FAIL;
  }
  return ESP_OK;
}

void HttpServer::forget(int socket) {
  Lock lock(lock_);
  for (int& follower : followers_) {
    if (follower == socket) follower = kNoSocket;
  }
}

void HttpServer::onClose(httpd_handle_t, int socket) {
  if (g_server) g_server->forget(socket);
  close(socket);
}

size_t HttpServer::Channel::clients() const {
  Lock lock(server_.lock_);
  size_t n = 0;
  for (int follower : server_.followers_) {
    if (follower != kNoSocket) ++n;
  }
  return n;
}

void HttpServer::Channel::closeAll() {
  if (!server_.server_) return;
  Lock lock(server_.lock_);
  for (int& follower : server_.followers_) {
    if (follower == kNoSocket) continue;
    httpd_sess_trigger_close(server_.server_, follower);
    follower = kNoSocket;
  }
}

void HttpServer::Channel::send(const char* event, StringView data, uint32_t id) {
  HttpServer& s = server_;
  if (!s.server_) return;
  bool ask = false;
  {
    Lock lock(s.lock_);
    // With the queue full the oldest event gives its place.
    if (s.frameCount_ == LMP_WEB_EVENT_QUEUE) {
      s.frameHead_ = (s.frameHead_ + 1) % LMP_WEB_EVENT_QUEUE;
      --s.frameCount_;
    }
    const size_t slot = (s.frameHead_ + s.frameCount_) % LMP_WEB_EVENT_QUEUE;
    char* frame = s.frames_[slot];

    // The event, then the piece of the answer that carries it: length, content, end of line.
    char text[kFrame - 16];
    int n;
    if (id) {
      n = snprintf(text, sizeof(text), "id: %lu\nevent: %s\ndata: %.*s\n\n",
                   static_cast<unsigned long>(id), event ? event : "message",
                   static_cast<int>(data.size), data.data ? data.data : "");
    } else {
      n = snprintf(text, sizeof(text), "event: %s\ndata: %.*s\n\n", event ? event : "message",
                   static_cast<int>(data.size), data.data ? data.data : "");
    }
    if (n <= 0 || static_cast<size_t>(n) >= sizeof(text)) return;  // it does not fit: dropped
    const int total = snprintf(frame, kFrame, "%x\r\n%s\r\n", static_cast<unsigned>(n), text);
    s.frameLength_[slot] = static_cast<uint16_t>(total);
    ++s.frameCount_;
    if (!s.flushQueued_) {
      s.flushQueued_ = true;
      ask = true;
    }
  }
  // The sockets belong to the task of the server: the events leave from there.
  if (ask && httpd_queue_work(s.server_, &HttpServer::flushWork, &s) != ESP_OK) {
    Lock lock(s.lock_);
    s.flushQueued_ = false;
  }
}

void HttpServer::flushWork(void* self) { static_cast<HttpServer*>(self)->flush(); }

void HttpServer::flush() {
  static char frame[kFrame];
  for (;;) {
    size_t length;
    int sockets[LMP_WEB_EVENT_CLIENTS];
    {
      Lock lock(lock_);
      if (frameCount_ == 0) {
        flushQueued_ = false;
        return;
      }
      length = frameLength_[frameHead_];
      memcpy(frame, frames_[frameHead_], length);
      frameHead_ = (frameHead_ + 1) % LMP_WEB_EVENT_QUEUE;
      --frameCount_;
      memcpy(sockets, followers_, sizeof(sockets));
    }
    for (int socket : sockets) {
      if (socket == kNoSocket) continue;
      if (httpd_socket_send(server_, socket, frame, length, 0) < 0) {
        forget(socket);
        httpd_sess_trigger_close(server_, socket);
      }
    }
  }
}

}  // namespace lmp::hal::esp32

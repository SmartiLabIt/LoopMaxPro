#include "services/web/web_service.h"

#include "hal/api/memory.h"

#if LMP_FEATURE_NET

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <new>

#include "lmp_version.h"
#include "services/config/config_service.h"
#include "services/log/log_service.h"
#include "services/net/net_service.h"
#if LMP_FEATURE_MODULES
#include "sdk/http.h"
#include "services/modules/modules_service.h"
#endif

namespace lmp {
namespace {

#if LMP_FEATURE_MODULES
// The addresses of the local networks, and of the PC itself for the simulator: the paths of a
// module have no login, they answer only there.
bool localAddress(const hal::Ip4& ip) {
  const uint8_t* b = ip.b;
  return b[0] == 10 || b[0] == 127 || (b[0] == 172 && b[1] >= 16 && b[1] <= 31) ||
         (b[0] == 192 && b[1] == 168) || (b[0] == 169 && b[1] == 254);
}
#endif

constexpr const char* kJson = "application/json";
constexpr const char* kHtml = "text/html; charset=utf-8";
constexpr const char* kWebRoot = "/web";
constexpr const char* kUser = "admin";
constexpr const char* kApiPrefix = "/api/";
constexpr const char* kEventsPath = "/api/events";
// Files with the fingerprint of their content in the name never change: one year.
constexpr uint32_t kLongCacheSec = 31536000;
constexpr uint32_t kPingMs = 15000;
constexpr size_t kLogsPerTick = 2;

// Shown when the files of the console are not in the filesystem.
const char kPlaceholder[] =
    "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>LoopMaxPro</title></head><body style=\"font-family:sans-serif;margin:2em\">"
    "<h1>LoopMaxPro</h1><p>The device is running. The files of the console are not "
    "installed.</p><p><a href=\"/api/state\">/api/state</a></p></body></html>";

constexpr ArgSpec kPasswordArgs[] = {
    Arg::secret("old", 0, 63),
    Arg::secret("new", 0, 63),
};

constexpr ArgSpec kFileArgs[] = {
    Arg::str("name", 1, 64),
};

struct Type {
  const char* extension;
  const char* type;
};
constexpr Type kTypes[] = {
    {".html", "text/html; charset=utf-8"},
    {".js", "text/javascript; charset=utf-8"},
    {".css", "text/css; charset=utf-8"},
    {".json", "application/json"},
    {".svg", "image/svg+xml"},
    {".png", "image/png"},
    {".jpg", "image/jpeg"},
    {".ico", "image/x-icon"},
    {".woff2", "font/woff2"},
    {".txt", "text/plain; charset=utf-8"},
};

int hexValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// Decodes percent codes and plus signs. False when malformed or too long.
bool urlDecode(StringView in, char* out, size_t cap) {
  size_t n = 0;
  for (size_t i = 0; i < in.size; ++i) {
    char c = in[i];
    if (c == '+') {
      c = ' ';
    } else if (c == '%') {
      if (i + 2 >= in.size) return false;
      const int high = hexValue(in[i + 1]);
      const int low = hexValue(in[i + 2]);
      if (high < 0 || low < 0) return false;
      c = static_cast<char>(high * 16 + low);
      i += 2;
    }
    if (c == '\0' || n + 1 >= cap) return false;
    out[n++] = c;
  }
  out[n] = '\0';
  return true;
}

// Visits the pairs of "a=1&b=2". The visitor returns false to stop.
template <class Visitor>
void eachPair(StringView form, Visitor visit) {
  size_t start = 0;
  while (start < form.size) {
    size_t end = form.find('&', start);
    if (end == StringView::npos) end = form.size;
    const StringView pair = form.substr(start, end - start);
    const size_t equal = pair.find('=');
    if (!pair.empty()) {
      const StringView key = equal == StringView::npos ? pair : pair.substr(0, equal);
      const StringView value = equal == StringView::npos ? StringView() : pair.substr(equal + 1);
      if (!visit(key, value)) return;
    }
    start = end + 1;
  }
}

bool sameNoCase(StringView a, StringView b) {
  if (a.size != b.size) return false;
  for (size_t i = 0; i < a.size; ++i) {
    char x = a[i], y = b[i];
    if (x >= 'A' && x <= 'Z') x = static_cast<char>(x + 32);
    if (y >= 'A' && y <= 'Z') y = static_cast<char>(y + 32);
    if (x != y) return false;
  }
  return true;
}

uint16_t codeOf(Status status) {
  switch (status) {
    case Status::Ok:          return 200;
    case Status::Pending:     return 202;
    case Status::Invalid:     return 400;
    case Status::Denied:      return 403;
    case Status::NotFound:    return 404;
    case Status::Busy:        return 409;
    case Status::Unsupported: return 501;
    case Status::Timeout:     return 504;
    case Status::NoMem:       return 507;
    case Status::Io:
    case Status::Error:       break;
  }
  return 500;
}

bool isForm(StringView contentType) {
  const StringView form("application/x-www-form-urlencoded");
  return contentType.size >= form.size && sameNoCase(contentType.substr(0, form.size), form);
}

int32_t elapsed(uint32_t nowMs, uint32_t sinceMs) { return static_cast<int32_t>(nowMs - sinceMs); }

}  // namespace

// ---------------------------------------------------------------- helpers, tested on the PC

bool formValue(StringView form, const char* name, char* out, size_t cap) {
  bool found = false;
  const StringView wanted(name);
  eachPair(form, [&](StringView key, StringView value) {
    char decoded[32];
    if (!urlDecode(key, decoded, sizeof(decoded)) || StringView(decoded) != wanted) return true;
    found = urlDecode(value, out, cap);
    return false;
  });
  return found;
}

bool formToArgs(StringView form, char* out, size_t cap) {
  size_t used = 0;
  bool good = true;
  if (cap) out[0] = '\0';
  eachPair(form, [&](StringView key, StringView value) {
    char name[32];
    char text[LMP_CMD_TEXT];
    good = urlDecode(key, name, sizeof(name)) && urlDecode(value, text, sizeof(text));
    for (const char* p = name; good && *p; ++p) {
      const bool letter = (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z');
      good = letter || (*p >= '0' && *p <= '9') || *p == '_';
    }
    // The command line has no way to write a quote inside a value.
    for (const char* p = text; good && *p; ++p) {
      good = *p != '"' && static_cast<unsigned char>(*p) >= 0x20;
    }
    if (!good || !name[0]) {
      good = false;
      return false;
    }
    const int n = snprintf(out + used, cap - used, "%s%s=\"%s\"", used ? " " : "", name, text);
    if (n < 0 || static_cast<size_t>(n) >= cap - used) {
      good = false;
      return false;
    }
    used += static_cast<size_t>(n);
    return true;
  });
  return good;
}

const char* contentTypeOf(StringView path) {
  if (path.endsWith(".gz")) path = path.substr(0, path.size - 3);
  for (const Type& t : kTypes) {
    if (path.endsWith(t.extension)) return t.type;
  }
  return "application/octet-stream";
}

bool hasFingerprint(StringView path) {
  size_t slash = 0;
  for (size_t i = 0; i < path.size; ++i) {
    if (path[i] == '/') slash = i + 1;
  }
  const StringView name = path.substr(slash);
  // A part between two dots, at least eight characters, all hexadecimal digits.
  size_t start = name.find('.');
  while (start != StringView::npos) {
    const size_t end = name.find('.', start + 1);
    if (end == StringView::npos) break;
    const StringView part = name.substr(start + 1, end - start - 1);
    bool hex = part.size >= 8;
    for (size_t i = 0; hex && i < part.size; ++i) hex = hexValue(part[i]) >= 0;
    if (hex) return true;
    start = end;
  }
  return false;
}

// Paths of files: nothing that climbs out of the folder of the console.
bool safeWebPath(StringView path) {
  if (path.empty() || path[0] != '/' || path.size > 96) return false;
  for (size_t i = 0; i < path.size; ++i) {
    const char c = path[i];
    if (static_cast<unsigned char>(c) < 0x20 || c == '\\' || c == ':') return false;
    if (c == '.' && i + 1 < path.size && path[i + 1] == '.') return false;
    if (c == '/' && i + 1 < path.size && path[i + 1] == '/') return false;
  }
  return true;
}

// ---------------------------------------------------------------- life cycle

const ServiceInfo& WebService::info() const {
  static const ServiceInfo kInfo{
      ServiceId::Web, "web", "\xF0\x9F\x8C\x90", Mode::Any, RestartPolicy::Retry, 20,
      DepList(ServiceId::Net, ServiceId::Config, ServiceId::Storage, ServiceId::Log)};
  return kInfo;
}

uint64_t WebService::uptimeMs() const { return kernel_->board().clock->uptimeMs(); }

Status WebService::start(Kernel& kernel) {
  kernel_ = &kernel;
  src_ = kernel.log().addSource(info().name, info().icon);
  const hal::Board& board = kernel.board();
  server_ = board.http;
  if (!server_) {
    fail("no HTTP server on this board");
    return Status::Unsupported;
  }
  if (!replyMutex_ && board.rtos) replyMutex_ = board.rtos->createMutex();
  if (!body_) body_ = static_cast<char*>(hal::allocLarge(kAnswerMax));
  if (!body_) {
    fail("no memory for the answers");
    return Status::NoMem;
  }
  auth_.setRandom(board.rng);

  // Registered once: a server that starts again keeps its routes.
  if (!events_) {
    const Status ota = server_->onUpload(
        "/api/upload/ota",
        [this](hal::IHttpRequest& req, const hal::HttpUploadPart& part) {
          return uploadPart(req, part, true);
        },
        [this](hal::IHttpRequest& req, hal::IHttpResponse& res) { uploadDone(req, res); });
    const Status file = server_->onUpload(
        "/api/upload/file",
        [this](hal::IHttpRequest& req, const hal::HttpUploadPart& part) {
          return uploadPart(req, part, false);
        },
        [this](hal::IHttpRequest& req, hal::IHttpResponse& res) { uploadDone(req, res); });
    // A server that cannot take long bodies is a server all the same.
    canUpload_ = ota == Status::Ok && file == Status::Ok;
    if (!canUpload_ && ota != Status::Unsupported) return ota;
    LMP_TRY(server_->on(hal::HttpMethod::Any, "/api/*",
                        [this](hal::IHttpRequest& req, hal::IHttpResponse& res) {
                          serve(req, res, true);
                        }));
    server_->onNotFound(
        [this](hal::IHttpRequest& req, hal::IHttpResponse& res) { serve(req, res, false); });
    events_ = server_->sse(kEventsPath);
    if (events_) events_->setGate([this](hal::IHttpRequest& req) { return gate(req); });
  }

  const Status up = server_->start(LMP_HTTP_PORT);
  if (up != Status::Ok) {
    fail("the server does not start");
    return up;
  }
  lastLogSeq_ = kernel.log().lastSeq();
  lastPingMs_ = kernel.now();
  LMP_LOGI(src_, "%s on port %u, console %s", server_->name(), static_cast<unsigned>(LMP_HTTP_PORT),
           !consoleInstalled()  ? "not installed"
           : consoleOverride()  ? "from the filesystem (uploaded by hand)"
           : webBundleCount()   ? "in the program"
                                : "installed");
  return Status::Ok;
}

void WebService::stop() {
  if (events_) events_->closeAll();
  if (server_) server_->stop();
}

void WebService::tick(uint32_t nowMs) {
  if (!server_) return;
  server_->poll();
  if (!events_) return;
  if (events_->clients() == 0) {
    // Who connects later asks for the past with /api/log.
    lastLogSeq_ = kernel_->log().lastSeq();
    lastPingMs_ = nowMs;
    return;
  }
  pushLogs();
  if (elapsed(nowMs, lastPingMs_) >= static_cast<int32_t>(kPingMs)) {
    lastPingMs_ = nowMs;
    events_->send("ping", StringView("{}"), 0);
  }
}

void WebService::pushLogs() {
  Logger& log = kernel_->log();
  LogEntry block[kLogsPerTick];
  const size_t n = log.read(lastLogSeq_, block, kLogsPerTick);
  for (size_t i = 0; i < n; ++i) {
    const LogEntry& e = block[i];
    lastLogSeq_ = e.seq;
    char text[LMP_WEB_EVENT_LEN];
    // Characters that JSON writes with several take more room than the entry has: the
    // message is then sent shorter, never dropped.
    size_t length = strlen(e.msg);
    for (;;) {
      BufferSink sink(text, sizeof(text));
      JsonWriter w(sink);
      w.beginObject();
      w.u32("seq", e.seq);
      w.u32("ms", e.ms);
      w.str("lvl", logLevelName(e.level));
      w.str("src", log.sourceName(e.source));
      w.str("msg", StringView(e.msg, length));
      if (e.truncated || length < strlen(e.msg)) w.boolean("cut", true);
      w.endObject();
      if (w.complete()) {
        events_->send("log", sink.view(), e.seq);
        break;
      }
      if (length == 0) break;
      length /= 2;
    }
  }
}

void WebService::onEvent(const Event& event) {
  if (!events_ || events_->clients() == 0) return;
  char text[160];
  BufferSink sink(text, sizeof(text));
  JsonWriter w(sink);
  kernel_->describeEvent(event, w);
  if (w.complete()) events_->send("event", sink.view(), 0);
}

// ---------------------------------------------------------------- context of the server

void WebService::serve(hal::IHttpRequest& req, hal::IHttpResponse& res, bool api) {
  struct Call {
    WebService* self;
    hal::IHttpRequest* req;
    Reply reply;
    bool api;
  } call{this, &req, Reply(), api};

  hal::LockGuard guard(replyMutex_);
  const Status carried = kernel_->invoke([&call]() {
    if (call.api) {
      call.self->route(*call.req, call.reply);
    } else {
      call.self->routeFile(*call.req, call.reply);
    }
  });

  char version[8];
  snprintf(version, sizeof(version), "%u", kWebApiVersion);
  res.header("X-LMP-Api", version);

  if (carried != Status::Ok) {
    // The application task did not take the request in time.
    res.status(503);
    res.header("Retry-After", "1");
    res.send(kJson, StringView("{\"error\":\"busy\",\"msg\":\"the device is busy\"}"));
    return;
  }

  const Reply& reply = call.reply;
  if (reply.redirect) {
    res.redirect(path_);
    return;
  }
  if (reply.file) {
    res.sendFile(path_, reply.type, reply.gzip, reply.cacheSec);
    return;
  }
  if (reply.asset) {
    res.sendData(reply.type,
                 StringView(reinterpret_cast<const char*>(reply.asset->data), reply.asset->size),
                 true, reply.cacheSec);
    return;
  }
  char wait[12];
  res.status(reply.code);
  if (api || reply.cors) res.header("Cache-Control", "no-store");
  if (reply.cors) {
    // A page of another origin, the one of who is writing a program, may read the answer.
    res.header("Access-Control-Allow-Origin", "*");
    res.header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
    res.header("Access-Control-Allow-Headers", "Content-Type, Authorization, X-Api-Key");
  }
  if (reply.retryAfterSec) {
    snprintf(wait, sizeof(wait), "%lu", static_cast<unsigned long>(reply.retryAfterSec));
    res.header("Retry-After", wait);
  }
  res.send(reply.type, StringView(body_, bodyLength_));
}

bool WebService::gate(hal::IHttpRequest& req) {
  struct Call {
    WebService* self;
    hal::IHttpRequest* req;
    bool open;
  } call{this, &req, false};
  const Status carried =
      kernel_->invoke([&call]() { call.open = call.self->allowed(*call.req); });
  if (carried != Status::Ok || !call.open) {
    ++refused_;
    return false;
  }
  return true;
}

// ---------------------------------------------------------------- access

bool WebService::accessFree(const hal::IHttpRequest& req) const {
  // Who is on the access point got in with its password. It is so in mode AP, when the access
  // point is up as a fallback, and while it stays up after a change of network: who asked
  // for the change reads the new address from there. Who comes from the network logs in.
  const INetService* net = kernel_->api<INetService>();
  return net && net->fromAccessPoint(req.clientIp());
}

StringView WebService::password() const {
  const Config& config = kernel_->config();
  return config.web.adminPsw.empty() ? config.net.apPsw.view() : config.web.adminPsw.view();
}

bool WebService::tokenOf(hal::IHttpRequest& req, char* out, size_t cap) const {
  char header[80];
  if (req.header("Authorization", header, sizeof(header))) {
    const StringView value(header);
    const StringView scheme("Bearer ");
    if (value.size > scheme.size && sameNoCase(value.substr(0, scheme.size), scheme)) {
      value.substr(scheme.size).copyTo(out, cap);
      return true;
    }
  }
  // The events are followed with a plain request, which cannot carry the header.
  return req.query("token", out, cap);
}

bool WebService::allowed(hal::IHttpRequest& req) {
  if (accessFree(req)) return true;
  char token[WebAuth::kTokenLength + 8];
  return tokenOf(req, token, sizeof(token)) && auth_.check(StringView(token), uptimeMs());
}

bool WebService::consoleInstalled() const {
  if (webBundleFind(StringView("/index.html"))) return true;
  hal::IFs* fs = kernel_->board().fs;
  if (!fs || !fs->mounted()) return false;
  return fs->exists("/web/index.html.gz") || fs->exists("/web/index.html");
}

bool WebService::consoleOverride() const {
  hal::IFs* fs = kernel_->board().fs;
  return fs && fs->mounted() && fs->exists(kWebOverrideMark);
}

unsigned WebService::removeConsoleFiles() {
  hal::IFs* fs = kernel_->board().fs;
  if (!fs || !fs->mounted() || !fs->exists(kWebRoot)) return 0;
  // One name at a time: a folder is not changed while it is being read.
  unsigned removed = 0;
  for (unsigned round = 0; round < 256; ++round) {
    char name[72] = "";
    fs->list(kWebRoot, [&name](const hal::DirEntry& e) {
      if (e.isDir) return true;
      snprintf(name, sizeof(name), "%s", e.name);
      return false;
    });
    if (!name[0]) break;
    snprintf(path_, sizeof(path_), "%s/%s", kWebRoot, name);
    if (fs->remove(path_) != Status::Ok) break;
    ++removed;
  }
  return removed;
}

// ---------------------------------------------------------------- routes

void WebService::answerError(Reply& reply, JsonWriter& w, uint16_t code, const char* error,
                      const char* msg, const char* field) {
  reply.code = code;
  reply.type = kJson;
  if (code >= 500) ++failed_;
  w.beginObject();
  w.str("error", error);
  w.str("msg", msg);
  if (field) w.str("field", field);
  w.endObject();
}

void WebService::route(hal::IHttpRequest& req, Reply& reply) {
  ++requests_;
  struct Serving {
    const hal::IHttpRequest*& slot;
    Serving(const hal::IHttpRequest*& s, const hal::IHttpRequest& r) : slot(s) { slot = &r; }
    ~Serving() { slot = nullptr; }
  } serving(serving_, req);
  BufferSink sink(body_, kAnswerMax);
  JsonWriter w(sink);
  reply.type = kJson;

  // "/api/<head>/<rest>"
  StringView path = req.path();
  path = path.substr(strlen(kApiPrefix));
  while (path.endsWith("/")) path = path.substr(0, path.size - 1);
  const size_t slash = path.find('/');
  const StringView head = slash == StringView::npos ? path : path.substr(0, slash);
  const StringView rest = slash == StringView::npos ? StringView() : path.substr(slash + 1);
  const bool get = req.method() == hal::HttpMethod::Get;
  const bool post = req.method() == hal::HttpMethod::Post;
  CommandBus& bus = kernel_->commands();
  bool fits = true;

  if (req.tooLong()) {
    answerError(reply, w, 413, "too_long", "the request is too long");
  } else if (head == "state") {
    if (get) {
      writeState(req, w);
    } else {
      answerError(reply, w, 405, "method", "use GET");
    }
  } else if (head == "auth") {
    routeAuth(rest, req, reply, w);
  } else if (!allowed(req)) {
    ++refused_;
    answerError(reply, w, 401, "auth", "login required");
  } else if (head == "cmd") {
    if (post) {
      runCommand(rest, req, reply, w);
    } else {
      answerError(reply, w, 405, "method", "use POST");
    }
  } else if (!get) {
    answerError(reply, w, 405, "method", "use GET");
  } else if (head == "report") {
    char parts[96] = "";
    req.query("parts", parts, sizeof(parts));
    kernel_->report(w, StringView(parts), true);
  } else if (head == "doc") {
    if (bus.findTarget(rest) == CommandBus::kNoTarget) {
      answerError(reply, w, 404, "not_found", "no such target", "target");
    } else {
      w.beginObject();
      bus.describe(rest, w);
      w.endObject();
    }
  } else if (head == "cfg") {
    writeConfig(rest, reply, w);
  } else if (head == "manifest") {
    if (rest.empty()) {
      w.beginObject();
      w.key("targets");
      bus.targets(w);
      w.endObject();
    } else {
      char help[4] = "";
      req.query("help", help, sizeof(help));
      if (bus.manifest(rest, w, strcmp(help, "0") != 0) != Status::Ok) {
        answerError(reply, w, 404, "not_found", "no such target", "target");
      }
    }
  } else if (head == "log") {
    fits = writeLog(req, reply, sink);
  } else {
    answerError(reply, w, 404, "not_found", "no such route");
  }

  if (!fits || !w.complete()) {
    // The answer did not fit: what was written is dropped.
    sink.clear();
    JsonWriter again(sink);
    answerError(reply, again, 507, "too_large", "the answer does not fit in the memory of the device");
  }
  bodyLength_ = sink.size();
}

void WebService::writeState(const hal::IHttpRequest& req, JsonWriter& w) const {
  const Config& config = kernel_->config();
  const INetService* net = kernel_->api<INetService>();
  w.beginObject();
  w.u32("v", 2);
  w.u32("api", kWebApiVersion);
  w.str("name", config.sys.deviceName.c_str());
  w.str("host", config.sys.hostname.c_str());
  w.str("board", kernel_->board().name);
  // light: the edition of the small boards, with less memory spent (docs/build.md).
  w.str("edition", LMP_LIGHT ? "light" : "full");
  w.str("code", LMP_DEVICE_CODE);
  w.str("fw", LMP_VERSION);
  w.str("build", LMP_BUILD_DATE);
  w.str("lang", config.sys.lang.c_str());
  w.str("mode", modeName(kernel_->mode()));
  w.boolean("fallback", net && net->netState() == NetState::Fallback);
#if LMP_WEB_LANES
  // The console sends this many requests together, not the dozen of a page that opens.
  w.u32("lanes", LMP_WEB_LANES);
#endif
  w.beginObject("auth");
  // For who is asking: the same device answers false on the access point, true on the network.
  w.boolean("required", !accessFree(req));
  w.str("user", kUser);
  w.endObject();
  w.beginObject("console");
  w.boolean("installed", consoleInstalled());
  w.boolean("inProgram", webBundleCount() > 0);
  w.boolean("uploaded", consoleOverride());
  w.endObject();
  // What the console needs to draw the tabs of the modules, before the login.
  w.key("modules");
#if LMP_FEATURE_MODULES
  const IModulesService* modules = kernel_->api<IModulesService>();
  if (modules) {
    modules->listPublic(w);
  } else {
    w.beginArray();
    w.endArray();
  }
#else
  w.beginArray();
  w.endArray();
#endif
  w.endObject();
}

void WebService::routeAuth(StringView what, hal::IHttpRequest& req, Reply& reply, JsonWriter& w) {
  const bool post = req.method() == hal::HttpMethod::Post;
  char token[WebAuth::kTokenLength + 8] = "";

  if (what == "me") {
    const bool has = tokenOf(req, token, sizeof(token));
    const uint32_t left = has ? auth_.leftSec(StringView(token), uptimeMs()) : 0;
    w.beginObject();
    w.boolean("required", !accessFree(req));
    w.boolean("auth", left > 0);
    w.str("user", left > 0 ? kUser : "");
    w.u32("ttl", left);
    w.endObject();
    return;
  }

  if (what == "logout") {
    if (!post) {
      answerError(reply, w, 405, "method", "use POST");
      return;
    }
    const bool closed = tokenOf(req, token, sizeof(token)) && auth_.logout(StringView(token));
    w.beginObject();
    w.boolean("ok", closed);
    w.endObject();
    return;
  }

  if (what != "login") {
    answerError(reply, w, 404, "not_found", "no such route");
    return;
  }
  if (!post) {
    answerError(reply, w, 405, "method", "use POST");
    return;
  }

  char given[72] = "";
  bool found;
  if (isForm(req.contentType())) {
    found = formValue(req.body(), "password", given, sizeof(given));
  } else {
    found = JsonValue::parse(req.body()).get("password").getString(given, sizeof(given));
  }
  if (!found) {
    answerError(reply, w, 400, "invalid", "password missing", "password");
    return;
  }

  const hal::Ip4 from = req.clientIp();
  const uint32_t ttlSec = static_cast<uint32_t>(kernel_->config().web.sessionTtlMin) * 60u;
  uint32_t waitSec = 0;
  const Status s =
      auth_.login(StringView(given), password(), from, uptimeMs(), ttlSec, token, waitSec);
  memset(given, 0, sizeof(given));

  if (s == Status::Ok) {
    LMP_LOGI(src_, "login from %u.%u.%u.%u", from.b[0], from.b[1], from.b[2], from.b[3]);
    w.beginObject();
    w.str("token", token);
    w.u32("ttl", ttlSec);
    w.str("user", kUser);
    w.endObject();
    return;
  }
  ++refused_;
  if (s == Status::Busy) {
    reply.retryAfterSec = waitSec;
    answerError(reply, w, 429, "busy", "too many wrong passwords: wait");
    return;
  }
  if (s == Status::Denied) {
    LMP_LOGW(src_, "wrong password from %u.%u.%u.%u%s", from.b[0], from.b[1], from.b[2],
             from.b[3], waitSec ? ": address locked" : "");
    answerError(reply, w, 401, "denied", "wrong password", "password");
    return;
  }
  answerError(reply, w, 500, statusName(s), "login not available");
}

void WebService::writeConfig(StringView target, Reply& reply, JsonWriter& w) {
  if (describeTargetConfig(*kernel_, target, w) == Status::NotFound) {
    answerError(reply, w, 404, "not_found", "no such target", "target");
  }
}

bool WebService::writeLog(hal::IHttpRequest& req, Reply& reply, BufferSink& sink) {
  JsonWriter w(sink);
  ILog* log = kernel_->api<ILog>();
  if (!log) {
    answerError(reply, w, 501, "unsupported", "log service not available");
    return true;
  }
  // As many entries as the answer usually holds, whatever is asked.
  const long room = static_cast<long>((kAnswerMax - 128) / (LMP_LOG_MSG_LEN + 80));
  char text[24];
  char source[20] = "";
  ILog::Query query;
  query.max = static_cast<uint16_t>(room);
  if (req.query("since", text, sizeof(text))) {
    query.since = static_cast<uint32_t>(strtoul(text, nullptr, 10));
  }
  if (req.query("max", text, sizeof(text))) {
    const long asked = strtol(text, nullptr, 10);
    if (asked >= 1 && asked < room) query.max = static_cast<uint16_t>(asked);
  }
  if (req.query("level", text, sizeof(text)) && !logLevelParse(text, query.level)) {
    answerError(reply, w, 400, "invalid", "level not known", "level");
    return true;
  }
  if (req.query("src", source, sizeof(source))) query.source = source;

  // Entries full of characters that JSON writes with several take more room than usual:
  // the page is then made shorter. Who reads goes on from the last number received.
  for (;;) {
    sink.clear();
    JsonWriter page(sink);
    log->writeEntries(page, query);
    if (page.complete()) return true;
    if (query.max <= 1) return false;
    query.max = static_cast<uint16_t>(query.max / 2);
  }
}

void WebService::runCommand(StringView rest, hal::IHttpRequest& req, Reply& reply,
                            JsonWriter& w) {
  const size_t slash = rest.find('/');
  if (slash == StringView::npos || slash == 0 || slash + 1 >= rest.size) {
    answerError(reply, w, 400, "invalid", "use /api/cmd/<target>/<command>");
    return;
  }
  const StringView target = rest.substr(0, slash);
  const StringView command = rest.substr(slash + 1);
  CommandBus& bus = kernel_->commands();
  if (!bus.hasCommand(target, command)) {
    answerError(reply, w, 404, "not_found", "no such command",
         bus.findTarget(target) == CommandBus::kNoTarget ? "target" : "command");
    return;
  }

  Status s;
  if (isForm(req.contentType())) {
    if (!formToArgs(req.body(), args_, sizeof(args_))) {
      answerError(reply, w, 400, "invalid", "arguments not readable");
      return;
    }
    s = bus.answer(target, command, StringView(args_), ArgsForm::Text, CmdSource::Local, w);
  } else {
    s = bus.answer(target, command, req.body(), ArgsForm::Json, CmdSource::Local, w);
  }
  reply.code = codeOf(s);
}

// ---------------------------------------------------------------- files

void WebService::routeFile(hal::IHttpRequest& req, Reply& reply) {
  ++requests_;
  BufferSink sink(body_, kAnswerMax);
  JsonWriter w(sink);
  const StringView path = req.path();
  const Config& config = kernel_->config();

  if (path.startsWith("/api")) {
    answerError(reply, w, 404, "not_found", "no such route");
    bodyLength_ = sink.size();
    return;
  }
  if (routeModule(req, reply, sink)) {
    bodyLength_ = sink.size();
    return;
  }
  if (req.method() != hal::HttpMethod::Get) {
    answerError(reply, w, 405, "method", "use GET");
    bodyLength_ = sink.size();
    return;
  }

  // Captive portal: a request for any other site is sent to the device.
  const INetService* net = kernel_->api<INetService>();
  char host[64];
  if (net && net->captive() && req.header("Host", host, sizeof(host))) {
    StringView name(host);
    const size_t port = name.find(':');
    if (port != StringView::npos) name = name.substr(0, port);
    char own[16], ap[16];
    net->address().toString(own, sizeof(own));
    net->apAddress().toString(ap, sizeof(ap));
    FixedString<40> local;
    local.format("%s.local", config.sys.hostname.c_str());
    // The loopback names reach the device only when it is the simulator on the same PC.
    const bool ours = name == StringView(own) || name == StringView(ap) ||
                      sameNoCase(name, config.sys.hostname.view()) ||
                      sameNoCase(name, local.view()) || sameNoCase(name, "localhost") ||
                      name == StringView("127.0.0.1");
    if (!ours) {
      snprintf(path_, sizeof(path_), "http://%s/", ap);
      reply.redirect = true;
      return;
    }
  }

  hal::IFs* fs = kernel_->board().fs;
  if (!safeWebPath(path)) {
    answerError(reply, w, 400, "invalid", "path not valid");
    bodyLength_ = sink.size();
    return;
  }
  const bool folder = path.endsWith("/");
  char wanted[112];
  snprintf(wanted, sizeof(wanted), "%.*s%s", static_cast<int>(path.size), path.data,
           folder ? "index.html" : "");
  const bool mounted = fs && fs->mounted();

  reply.type = contentTypeOf(StringView(wanted));
  reply.cacheSec = hasFingerprint(StringView(wanted)) ? kLongCacheSec : 0;
  // The console of the program comes first. The files of the filesystem come first only when
  // uploaded by hand; otherwise they are the console of an older firmware, left where it is:
  // if the board goes back to that firmware, it finds its console again.
  const bool uploaded = mounted && fs->exists(kWebOverrideMark);
  if (!uploaded) {
    reply.asset = webBundleFind(StringView(wanted));
    if (reply.asset) return;
  }
  snprintf(path_, sizeof(path_), "%s%s.gz", kWebRoot, wanted);
  reply.gzip = mounted && fs->exists(path_);
  if (!reply.gzip) snprintf(path_, sizeof(path_), "%s%s", kWebRoot, wanted);
  if (reply.gzip || (mounted && fs->exists(path_))) {
    reply.file = true;
    return;
  }
  if (uploaded) {
    reply.asset = webBundleFind(StringView(wanted));
    if (reply.asset) return;
  }
  reply.type = nullptr;
  reply.cacheSec = 0;

  if (StringView(wanted) == "/index.html") {
    reply.type = kHtml;
    sink.write(kPlaceholder, sizeof(kPlaceholder) - 1);
    bodyLength_ = sink.size();
    return;
  }
  reply.code = 404;
  reply.type = "text/plain; charset=utf-8";
  sink.write("not found", 9);
  bodyLength_ = sink.size();
}

bool WebService::routeModule(hal::IHttpRequest& req, Reply& reply, BufferSink& sink) {
#if LMP_FEATURE_MODULES
  // "/<word>/<rest>"
  StringView rest = req.path();
  if (rest.size < 2 || rest.data[0] != '/') return false;
  rest = rest.substr(1);
  const size_t slash = rest.find('/');
  const StringView word = slash == StringView::npos ? rest : rest.substr(0, slash);
  rest = slash == StringView::npos ? StringView("") : rest.substr(slash + 1);
  IModulesService* modules = kernel_->api<IModulesService>();
  sdk::Module* owner = modules ? modules->httpOwner(word) : nullptr;
  if (!owner) return false;

  reply.cors = true;
  reply.type = kJson;
  JsonWriter w(sink);
  if (!localAddress(req.clientIp())) {
    answerError(reply, w, 403, "forbidden", "only from the local network");
    return true;
  }
  // The question a browser asks before a request from a page of another origin.
  if (req.method() == hal::HttpMethod::Options) {
    reply.code = 204;
    reply.type = "text/plain";
    return true;
  }
  sdk::HttpCall call(req, rest, sink);
  owner->onHttp(call);
  reply.code = call.code;
  reply.type = call.type ? call.type : kJson;
  return true;
#else
  (void)req;
  (void)reply;
  (void)sink;
  return false;
#endif
}

// ---------------------------------------------------------------- documents and commands

void WebService::describe(JsonWriter& w) const {
  if (!kernel_ || !server_) return;
  const Config& config = kernel_->config();
  w.str("server", server_->name());
  w.u32("port", LMP_HTTP_PORT);
  w.boolean("running", server_->running());
  w.u32("api", kWebApiVersion);

  w.beginObject("auth");
  // For who is asking. Asked from anywhere else, it is what the network gets.
  w.boolean("required", !(serving_ && accessFree(*serving_)));
  w.str("user", kUser);
  // Without a password of its own the console uses the one of the access point.
  w.boolean("ownPassword", !config.web.adminPsw.empty());
  w.u32("sessions", static_cast<uint32_t>(auth_.sessions(uptimeMs())));
  w.u32("sessionsMax", LMP_MAX_SESSIONS);
  w.u32("sessionTtlMin", config.web.sessionTtlMin);
  w.u32("wrongPasswords", auth_.failures());
  w.endObject();

  w.beginObject("events");
  w.u32("clients", events_ ? static_cast<uint32_t>(events_->clients()) : 0);
  w.u32("clientsMax", LMP_WEB_EVENT_CLIENTS);
  w.endObject();

  w.beginObject("console");
  w.boolean("installed", consoleInstalled());
  w.boolean("inProgram", webBundleCount() > 0);
  w.boolean("uploaded", consoleOverride());
  w.endObject();

  w.u32("requests", requests_);
  w.u32("refused", refused_);
  w.u32("failed", failed_);
  w.boolean("canUpload", canUpload_);
  w.u32("uploads", uploads_);
  w.u32("answerMax", static_cast<uint32_t>(kAnswerMax));

  w.beginArray("urls");
  const INetService* net = kernel_->api<INetService>();
  if (net && net->address().isSet()) {
    FixedString<56> url;
#if LMP_FEATURE_MDNS
    url.format("http://%s.local/", config.sys.hostname.c_str());
    w.str(url.c_str());
#endif
    const hal::Ip4 ip = net->address();
    url.format("http://%u.%u.%u.%u/", ip.b[0], ip.b[1], ip.b[2], ip.b[3]);
    w.str(url.c_str());
  }
  w.endArray();
}

void WebService::commands(CommandSet& set) {
  set.add("setPassword",
          LMP_HELP("Password of the console. Empty: the password of the access point is used"),
          kPasswordArgs,
          [this](const Args& a, CmdContext& ctx) -> Status {
            if (!sameSecret(StringView(a.str("old")), password())) {
              ctx.errorField = "old";
              return Status::Denied;
            }
            const size_t length = strlen(a.str("new"));
            if (length != 0 && length < 8) {
              ctx.errorField = "new";
              return Status::Invalid;
            }
            Config& config = kernel_->config();
            config.web.adminPsw.assign(a.str("new"));
            config.touch(Config::kWeb);
            // Who was in has to come back with the new password.
            auth_.logoutAll();
            if (events_) events_->closeAll();
            kernel_->events().publish(Event(EventId::ConfigChanged, Config::kWeb));
            return Status::Ok;
          });

  set.add("logoutAll", LMP_HELP("Closes every session of the console"),
          [this](const Args&, CmdContext&) -> Status {
            auth_.logoutAll();
            if (events_) events_->closeAll();
            return Status::Ok;
          });

  set.add("files", LMP_HELP("Files of the console in the filesystem, with their size"),
          [this](const Args&, CmdContext& ctx) -> Status {
            hal::IFs* fs = kernel_->board().fs;
            if (!fs || !fs->mounted()) return Status::Unsupported;
            if (!ctx.result) return Status::Ok;
            struct Listing {
              JsonWriter* w;
              uint32_t count;
              uint32_t bytes;
            } listing{ctx.result, 0, 0};
            ctx.result->beginArray("files");
            fs->list(kWebRoot, [&listing](const hal::DirEntry& e) {
              // The names with a dot in front are of the system: the mark, a transfer.
              if (e.isDir || e.name[0] == '.') return true;
              listing.w->beginObject();
              listing.w->str("name", e.name);
              listing.w->u32("size", e.size);
              listing.w->endObject();
              ++listing.count;
              listing.bytes += e.size;
              return true;
            });
            ctx.result->endArray();
            ctx.result->u32("count", listing.count);
            ctx.result->u32("bytes", listing.bytes);
            return Status::Ok;
          });

  set.add("useProgramConsole",
          LMP_HELP("Removes the files of the console uploaded by hand: the one of the program is used"),
          [this](const Args&, CmdContext& ctx) -> Status {
            if (!webBundleCount()) return Status::Unsupported;
            const unsigned removed = removeConsoleFiles();
            if (ctx.result) ctx.result->u32("removed", removed);
            LMP_LOGI(src_, "console of the program in use: %u files removed", removed);
            return Status::Ok;
          },
          kCmdConfirm);

  set.add("removeFile", LMP_HELP("Removes a file of the console"), kFileArgs,
          [this](const Args& a, CmdContext& ctx) -> Status {
            hal::IFs* fs = kernel_->board().fs;
            if (!fs || !fs->mounted()) return Status::Unsupported;
            const StringView name(a.str("name"));
            ctx.errorField = "name";
            if (name.find('/') != StringView::npos || name.find('\\') != StringView::npos ||
                name.startsWith(".") || name.size > 64) {
              return Status::Invalid;
            }
            snprintf(path_, sizeof(path_), "%s/%.*s", kWebRoot, static_cast<int>(name.size),
                     name.data);
            if (!fs->exists(path_)) return Status::NotFound;
            ctx.errorField = nullptr;
            return fs->remove(path_);
          },
          kCmdConfirm);
}

}  // namespace lmp

#endif  // LMP_FEATURE_NET

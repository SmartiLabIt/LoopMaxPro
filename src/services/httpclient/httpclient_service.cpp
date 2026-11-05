#include "services/httpclient/httpclient_service.h"

#if LMP_FEATURE_HTTPCLIENT

#include <string.h>

namespace lmp {

void hostOf(const char* url, char* out, size_t cap) {
  if (!out || cap == 0) return;
  out[0] = '\0';
  if (!url) return;
  const char* p = strstr(url, "://");
  p = p ? p + 3 : url;
  // Name and password, when the address carries them, are not part of the host.
  const char* end = p;
  while (*end && *end != '/' && *end != '?' && *end != '#') ++end;
  for (const char* at = p; at < end; ++at) {
    if (*at == '@') p = at + 1;
  }
  size_t n = 0;
  while (p < end && *p != ':' && n + 1 < cap) out[n++] = *p++;
  out[n] = '\0';
}

bool requestStuck(uint32_t nowMs, uint32_t aliveMs, uint32_t limitMs) {
  // Signed: a sign of life written a moment after nowMs was taken is not four billion ms old.
  return static_cast<int32_t>(nowMs - aliveMs) > static_cast<int32_t>(limitMs);
}

const ServiceInfo& HttpClientService::info() const {
  static const ServiceInfo kInfo{ServiceId::HttpClient, "httpclient", "\xF0\x9F\x92\xAC",
                                 Mode::Lan, RestartPolicy::Retry, 50,
                                 DepList(ServiceId::Net, ServiceId::Log)};
  return kInfo;
}

Status HttpClientService::start(Kernel& kernel) {
  kernel_ = &kernel;
  src_ = kernel.log().addSource(info().name, info().icon);
  client_ = kernel.board().httpClient;
  if (!client_) {
    fail("no client for the requests on this board");
    return Status::Unsupported;
  }
  if (!mutex_ && kernel.board().rtos) mutex_ = kernel.board().rtos->createMutex();
  hal::LockGuard guard(mutex_);
  // A request that was travelling when the service stopped is still out there.
  dropped_ = busy_;
  return Status::Ok;
}

void HttpClientService::stop() {
  // Who was waiting is told: nothing stays pending for ever.
  while (count_) {
    const Entry e = queue_[head_];
    head_ = (head_ + 1) % LMP_HTTPC_QUEUE;
    --count_;
    HttpAnswer gone;
    gone.result = Status::Denied;
    if (e.done) e.done(gone);
  }
  hal::LockGuard guard(mutex_);
  if (busy_ && !dropped_) {
    dropped_ = true;
    HttpAnswer gone;
    gone.result = Status::Denied;
    if (flying_.done) flying_.done(gone);
  }
}

Status HttpClientService::fetch(const HttpFetch& what, HttpDone done) {
  if (!running() || !client_) return Status::Denied;
  if (!what.url || !*what.url) return Status::Invalid;
  if (strncmp(what.url, "http://", 7) != 0 && strncmp(what.url, "https://", 8) != 0) {
    return Status::Invalid;
  }
  if (strncmp(what.url, "https://", 8) == 0 && !client_->secure()) return Status::Unsupported;
  if (count_ >= LMP_HTTPC_QUEUE) return Status::Busy;

  Entry& e = queue_[(head_ + count_) % LMP_HTTPC_QUEUE];
  e.what = what;
  e.done = done;
  e.sinceMs = kernel_->now();
  ++count_;
  if (what.out && what.cap) what.out[0] = '\0';
  launch();
  return Status::Ok;
}

void HttpClientService::launch() {
  for (;;) {
    Entry gone;
    {
      hal::LockGuard guard(mutex_);
      if (busy_ || count_ == 0) return;
      // A secure connection takes memory to be set up: on a board short of it the request
      // waits, tried again at every round, and is given up after a while. Setting it up anyway
      // would take the memory to zero, and with it the Wi-Fi of a first ESP32. The first one
      // that can leave does: a secure request that waits does not hold back the ones in the
      // clear behind it.
      const uint32_t now = kernel_->now();
      size_t pick = count_;
      bool give = false;
      for (size_t i = 0; i < count_ && pick == count_; ++i) {
        const Entry& e = queue_[(head_ + i) % LMP_HTTPC_QUEUE];
        if (client_->roomFor(e.what.url)) {
          pick = i;
        } else if (now - e.sinceMs >= LMP_SECURE_WAIT_S * 1000u) {
          pick = i;
          give = true;
        }
      }
      if (pick == count_) return;   // all of them wait for memory
      const Entry taken = queue_[(head_ + pick) % LMP_HTTPC_QUEUE];
      // The ones before it move one place towards it: the order of the others stays.
      for (size_t j = pick; j > 0; --j) {
        queue_[(head_ + j) % LMP_HTTPC_QUEUE] = queue_[(head_ + j - 1) % LMP_HTTPC_QUEUE];
      }
      head_ = (head_ + 1) % LMP_HTTPC_QUEUE;
      --count_;
      if (!give) {
        flying_ = taken;
        answer_ = HttpAnswer();
        busy_ = true;
        finished_ = false;
        dropped_ = false;
        break;
      }
      gone = taken;
    }
    ++requests_;
    ++failures_;
    hostOf(gone.what.url, lastHost_.data(), lastHost_.capacity + 1);
    lastStatus_ = 0;
    lastResult_ = Status::NoMem;
    if (gone.what.quiet) {
      LMP_LOGD(src_, "%s: no memory for a secure connection", lastHost_.c_str());
    } else {
      LMP_LOGW(src_, "%s: no memory for a secure connection", lastHost_.c_str());
    }
    HttpAnswer none;
    none.result = Status::NoMem;
    if (gone.done) gone.done(none);
  }
  ++requests_;
  hostOf(flying_.what.url, lastHost_.data(), lastHost_.capacity + 1);
  aliveMs_ = kernel_->now();

  const Status posted = kernel_->worker().post([this]() { travel(); });
  if (posted != Status::Ok) {
    hal::LockGuard guard(mutex_);
    answer_.result = posted;
    finished_ = true;
  }
}

bool HttpClientService::keep(Span<const uint8_t> piece) {
  aliveMs_ = kernel_->now();
  const HttpFetch& what = flying_.what;
  if (what.sink) {
    if (!what.sink(piece)) return false;
    answer_.length += piece.size();
    return true;
  }
  if (!what.out || what.cap == 0) {
    answer_.length += piece.size();   // nobody wants the body: it is counted and dropped
    return true;
  }
  const size_t room = what.cap - 1 - (answer_.length < what.cap - 1 ? answer_.length : what.cap - 1);
  const size_t n = piece.size() < room ? piece.size() : room;
  if (n) memcpy(what.out + answer_.length, piece.data(), n);
  if (n < piece.size()) answer_.truncated = true;
  answer_.length += n;
  what.out[answer_.length] = '\0';
  return true;
}

void HttpClientService::travel() {
  const HttpFetch& what = flying_.what;
  hal::HttpClientRequest req;
  req.url = what.url;
  req.method = what.method;
  req.contentType = what.contentType;
  req.body = what.body;
  req.headers = what.headers;
  req.timeoutMs = what.timeoutMs;
  req.tls.caPem = what.caPem;
  req.onHeader = what.headerSink;

  aliveMs_ = kernel_->now();
  const uint32_t leftMs = aliveMs_;
  hal::HttpClientResponse res;
  const Status s = client_->request(
      req, res, [this](Span<const uint8_t> piece) { return keep(piece); });

  hal::LockGuard guard(mutex_);
  answer_.result = s;
  answer_.status = res.status;
  answer_.ms = kernel_->now() - leftMs;
  finished_ = true;
}

void HttpClientService::tick(uint32_t nowMs) {
  // A request that waits for memory is tried again at every round.
  if (!busy_ && count_) launch();
  Entry ended;
  HttpAnswer answer;
  bool tell;
  bool stuck = false;
  int32_t silentMs = 0;
  {
    hal::LockGuard guard(mutex_);
    if (busy_ && !finished_) {
      const uint32_t limitMs = flying_.what.timeoutMs + LMP_HTTPC_STUCK_S * 1000u;
      // The request writes its sign of life from its own task, and it can be later than the
      // time of this round: the difference is then below zero, not four billion (a download
      // restarted half way).
      silentMs = static_cast<int32_t>(nowMs - aliveMs_);
      stuck = !stuck_ && requestStuck(nowMs, aliveMs_, limitMs);
      if (stuck) stuck_ = true;
    }
    if (!stuck && (!busy_ || !finished_)) return;
  }
  if (stuck) {
    LMP_LOGE(src_, "%s: no sign of life for %ld s", lastHost_.c_str(),
             static_cast<long>(silentMs / 1000));
    kernel_->guardRestart("requests to servers stuck");
    return;
  }
  {
    hal::LockGuard guard(mutex_);
    if (!busy_ || !finished_) return;
    ended = flying_;
    answer = answer_;
    tell = !dropped_;
    busy_ = false;
    finished_ = false;
  }

  lastStatus_ = answer.status;
  lastResult_ = answer.result;
  if (answer.result != Status::Ok) {
    ++failures_;
    if (ended.what.quiet) {
      LMP_LOGD(src_, "%s: %s", lastHost_.c_str(), statusName(answer.result));
    } else {
      LMP_LOGW(src_, "%s: %s", lastHost_.c_str(), statusName(answer.result));
    }
  } else {
    LMP_LOGD(src_, "%s: %d, %lu bytes", lastHost_.c_str(), answer.status,
             static_cast<unsigned long>(answer.length));
  }
  if (tell && ended.done) ended.done(answer);
  launch();
}

void HttpClientService::describe(JsonWriter& w) const {
  if (!kernel_) return;
  w.boolean("available", client_ != nullptr);
  w.boolean("secure", secure());
  w.boolean("busy", busy_);
  w.u32("waiting", static_cast<uint32_t>(count_));
  w.u32("waitingMax", LMP_HTTPC_QUEUE);
  w.u32("requests", requests_);
  w.u32("failures", failures_);
  w.beginObject("last");
  // The host only: an address can carry a key.
  w.str("host", lastHost_.c_str());
  w.i32("status", lastStatus_);
  w.str("result", statusName(lastResult_));
  w.endObject();
}

}  // namespace lmp

#endif  // LMP_FEATURE_HTTPCLIENT

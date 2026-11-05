// LoopMaxPro - requests to servers on the network.
// A request lasts seconds, so it never runs on the application task: who asks gets the
// answer later, in its own context. One request travels at a time, the others wait.
#pragma once
#include "lmp_config.h"

#if LMP_FEATURE_HTTPCLIENT

#include "hal/api/ihttp.h"
#include "kernel/kernel.h"

namespace lmp {

// What came back. status is 0 when no answer arrived: result tells why.
struct HttpAnswer {
  Status result = Status::Ok;
  int status = 0;
  size_t length = 0;        // bytes of the body that were received
  bool truncated = false;   // the body did not fit in the buffer of who asked
  uint32_t ms = 0;          // how long it travelled, without the wait in the queue
  // True for the answers that mean "done": 200 to 299.
  bool good() const { return result == Status::Ok && status >= 200 && status < 300; }
};

using HttpDone = Delegate<void(const HttpAnswer&)>;

// A request. Every text and every buffer must stay valid until the answer arrives.
struct HttpFetch {
  const char* url = nullptr;
  hal::HttpMethod method = hal::HttpMethod::Get;
  const char* contentType = nullptr;
  StringView body;
  Span<const hal::HttpHeader> headers;
  uint32_t timeoutMs = LMP_HTTPC_TIMEOUT_MS;
  // Certificates the server is checked against, PEM. Null: those of the board.
  const char* caPem = nullptr;

  // Where the body of the answer goes: a buffer, always NUL terminated...
  char* out = nullptr;
  size_t cap = 0;
  // ...or a function that takes it piece by piece, for what does not fit in memory. It runs
  // in the context of the request, not on the application task, and returns false to stop.
  hal::HttpChunkHandler sink;

  // The headers of the answer, one by one, in the context of the request: who wants them.
  hal::HttpHeaderSink headerSink;

  // True when who asks tells a failure in its own way, as a monitor that asks every minute: the
  // log does not get a warning for each one.
  bool quiet = false;
};

class IHttpClientService {
 public:
  static constexpr ServiceId kId = ServiceId::HttpClient;
  virtual ~IHttpClientService() = default;

  // Queues the request. done runs on the application task. Busy when the queue is full,
  // Denied when the service is not running.
  virtual Status fetch(const HttpFetch& what, HttpDone done) = 0;
  // True when requests are taken: the device is on a network and the board has a client.
  virtual bool ready() const = 0;
  // True when addresses that begin with https can be reached.
  virtual bool secure() const = 0;
  // True when no request is travelling or waiting: a moment to open another secure
  // connection without two of them taking the memory together.
  virtual bool idle() const = 0;
};

class HttpClientService : public Service, public IHttpClientService {
 public:
  const ServiceInfo& info() const override;
  Status start(Kernel& kernel) override;
  void stop() override;
  void tick(uint32_t nowMs) override;
  void describe(JsonWriter& w) const override;
  void* api() override { return static_cast<IHttpClientService*>(this); }

  Status fetch(const HttpFetch& what, HttpDone done) override;
  bool ready() const override { return client_ && running(); }
  bool idle() const override { return !busy_ && count_ == 0; }
  bool secure() const override { return client_ && client_->secure(); }

  size_t waiting() const { return count_; }

 private:
  struct Entry {
    HttpFetch what;
    HttpDone done;
    uint32_t sinceMs = 0;   // when it was asked: a secure one waits for memory only so long
  };

  void launch();
  // Context of the request.
  void travel();
  bool keep(Span<const uint8_t> piece);

  Kernel* kernel_ = nullptr;
  hal::IHttpClient* client_ = nullptr;
  hal::IMutex* mutex_ = nullptr;
  LogSource src_ = kLogSys;

  Entry queue_[LMP_HTTPC_QUEUE];
  size_t head_ = 0;
  size_t count_ = 0;

  // The request that is travelling. Written by its context until finished_ is set, read by
  // the application task after.
  Entry flying_;
  HttpAnswer answer_;
  bool busy_ = false;
  bool finished_ = false;
  bool dropped_ = false;     // the service stopped: the answer goes nowhere
  // Last sign of life of the request that is travelling: when it left, when a piece came. A
  // request silent far beyond its wait has stuck the worker, and only a restart frees it.
  volatile uint32_t aliveMs_ = 0;
  bool stuck_ = false;

  uint32_t requests_ = 0;
  uint32_t failures_ = 0;
  int lastStatus_ = 0;
  Status lastResult_ = Status::Ok;
  FixedString<48> lastHost_;
};

// Host of an address, without scheme, port, path and query: what can be written in a log.
void hostOf(const char* url, char* out, size_t cap);
// True when a request silent since aliveMs is beyond its limit at nowMs. aliveMs can be a moment
// after nowMs: the request writes it from its own task.
bool requestStuck(uint32_t nowMs, uint32_t aliveMs, uint32_t limitMs);

}  // namespace lmp

#endif  // LMP_FEATURE_HTTPCLIENT

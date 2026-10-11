// LoopMaxPro HAL - HTTP server and client, MQTT client. Transport only.
// Routing, authentication and JSON belong to the services above.
#pragma once
#include "hal/api/types.h"

namespace lmp::hal {

enum class HttpMethod : uint8_t { Get = 1, Post = 2, Put = 4, Delete = 8, Options = 16, Any = 31 };

class IHttpRequest {
 public:
  virtual ~IHttpRequest() = default;
  virtual HttpMethod method() const = 0;
  // Path without the query, percent codes already decoded.
  virtual StringView path() const = 0;
  // Each getter copies the value, NUL terminated, and returns false when it is missing.
  // Values of the query are decoded.
  virtual bool query(const char* name, char* out, size_t cap) const = 0;
  virtual bool header(const char* name, char* out, size_t cap) const = 0;
  // Body as received, at most LMP_HTTP_BODY_MAX bytes. tooLong() tells it was longer.
  virtual StringView body() const = 0;
  virtual bool tooLong() const = 0;
  virtual StringView contentType() const = 0;
  virtual Ip4 clientIp() const = 0;
};

class IHttpResponse {
 public:
  virtual ~IHttpResponse() = default;
  virtual void status(uint16_t code) = 0;
  // Both texts must stay valid until the body is sent.
  virtual void header(const char* name, const char* value) = 0;
  // Sends the whole body in one call.
  virtual void send(const char* contentType, StringView body) = 0;
  // Sends a file of the filesystem. gzip tells the file is compressed: the client is told so.
  virtual void sendFile(const char* fsPath, const char* contentType, bool gzip,
                        uint32_t cacheSeconds) = 0;
  // Sends a file that is in the program. The same headers as sendFile().
  virtual void sendData(const char* contentType, StringView data, bool gzip,
                        uint32_t cacheSeconds) {
    char cache[56];
    if (cacheSeconds) {
      snprintf(cache, sizeof(cache), "public, max-age=%lu, immutable",
               static_cast<unsigned long>(cacheSeconds));
    } else {
      snprintf(cache, sizeof(cache), "no-cache");
    }
    header("Cache-Control", cache);
    if (gzip) header("Content-Encoding", "gzip");
    send(contentType, data);
  }
  virtual void redirect(const char* url) = 0;
};

// Handlers run in the context of the server: a task of its own on the boards that have tasks,
// the caller of poll() on the others.
using HttpHandler = Delegate<void(IHttpRequest&, IHttpResponse&)>;
using HttpGate = Delegate<bool(IHttpRequest&)>;

// A long body, such as a firmware image, is never held in memory: it is handed over in
// pieces as it arrives. Begin comes first, with no data; then the pieces; then End, or
// Aborted when the client went away or a piece was refused.
enum class HttpUploadPhase : uint8_t { Begin, Data, End, Aborted };

struct HttpUploadPart {
  HttpUploadPhase phase = HttpUploadPhase::Begin;
  Span<const uint8_t> data;
  size_t total = 0;      // length declared by the client, 0 when it did not say
  size_t received = 0;   // bytes handed over so far, this piece included
};

// Returns false to refuse: at Begin nothing is read, later the rest is dropped.
using HttpUploadHandler = Delegate<bool(IHttpRequest&, const HttpUploadPart&)>;

// Server sent events channel.
class ISseChannel {
 public:
  virtual ~ISseChannel() = default;
  // Decides whether a new client can follow the events. Without a gate everybody can.
  virtual void setGate(HttpGate gate) = 0;
  // Queues one event for every client and returns at once: it never waits for a slow
  // client. When the queue is full the oldest event is lost. id 0 means no id.
  virtual void send(const char* event, StringView data, uint32_t id) = 0;
  virtual size_t clients() const = 0;
  // Closes every client: they have to ask again, and pass the gate again.
  virtual void closeAll() = 0;
};

class IHttpServer {
 public:
  virtual ~IHttpServer() = default;

  virtual const char* name() const = 0;
  virtual Status start(uint16_t port) = 0;
  virtual void stop() = 0;
  virtual bool running() const = 0;
  virtual void poll() = 0;

  // Routes are registered before start(). A path ending with "/*" matches by prefix.
  virtual Status on(HttpMethod methods, const char* path, HttpHandler handler) = 0;
  // A POST whose body is handed over in pieces. done runs at the end and sends the answer,
  // also when the upload was refused or aborted. For these requests body() is empty.
  virtual Status onUpload(const char* path, HttpUploadHandler parts, HttpHandler done) = 0;
  // Called for every request that matches no route.
  virtual void onNotFound(HttpHandler handler) = 0;
  // One channel for each server. The same path gives the same channel.
  virtual ISseChannel* sse(const char* path) = 0;
};

// ---------------------------------------------------------------- HTTP client

struct TlsConfig {
  const char* caPem = nullptr;   // certificate or bundle; null uses the board bundle
  bool insecure = false;         // debug only: skips verification
};

struct HttpHeader {
  const char* name = nullptr;
  const char* value = nullptr;
};

// Each header of an answer, name and value, as it arrives.
using HttpHeaderSink = Delegate<void(const char* name, const char* value)>;

struct HttpClientRequest {
  const char* url = nullptr;
  HttpMethod method = HttpMethod::Get;
  const char* contentType = nullptr;
  StringView body;
  Span<const HttpHeader> headers;
  uint32_t timeoutMs = 10000;
  TlsConfig tls;
  // The headers of the answer, for who wants them: in the context of the request.
  HttpHeaderSink onHeader;
};

struct HttpClientResponse {
  int status = 0;            // HTTP status; 0 when no answer arrived
  size_t contentLength = 0;  // as declared by the server, 0 when it did not say
};

// Receives the body in chunks. Return false to abort the transfer.
using HttpChunkHandler = Delegate<bool(Span<const uint8_t>)>;

class IHttpClient {
 public:
  virtual ~IHttpClient() = default;
  // True when addresses that begin with https can be reached.
  virtual bool secure() const = 0;
  // True when a request to this address can leave now: a secure connection takes memory to be
  // set up, and on a board short of it the request waits (hal/api/memory.h).
  virtual bool roomFor(const char* url) const {
    (void)url;
    return true;
  }
  // Blocking: call it from the worker task. Ok when an answer arrived, whatever its status;
  // Timeout, Io or Denied (certificate not accepted) when it did not.
  virtual Status request(const HttpClientRequest& req, HttpClientResponse& res,
                         HttpChunkHandler onChunk) = 0;
};

// ---------------------------------------------------------------- MQTT client

struct MqttConfig {
  const char* host = nullptr;
  uint16_t port = 8883;
  const char* clientId = nullptr;
  const char* username = nullptr;
  const char* password = nullptr;
  const char* willTopic = nullptr;
  const char* willMessage = nullptr;
  uint16_t keepAliveSec = 60;
  bool useTls = true;
  TlsConfig tls;
};

// Called in the context of the driver, which can be a task of its own.
using MqttMessageHandler = Delegate<void(StringView topic, StringView payload)>;

class IMqtt {
 public:
  virtual ~IMqtt() = default;
  // The texts are copied: they do not need to stay valid.
  virtual Status configure(const MqttConfig& cfg) = 0;
  // Starts connecting and returns at once. The driver connects again by itself when the
  // connection is lost, until disconnect().
  virtual Status connect() = 0;
  virtual void disconnect() = 0;
  virtual bool connected() const = 0;
  // Why the last attempt failed, for the log. Empty when there is nothing to say.
  virtual const char* lastError() const = 0;
  virtual Status publish(const char* topic, StringView payload, uint8_t qos, bool retain) = 0;
  // To be asked again after every new connection.
  virtual Status subscribe(const char* topic, uint8_t qos) = 0;
  virtual void onMessage(MqttMessageHandler handler) = 0;
  virtual void poll() = 0;
};

}  // namespace lmp::hal

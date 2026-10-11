// LoopMaxPro HAL - ESP8266: client of web services, plain HTTP.
#include "lmp_config.h"

#if LMP_FEATURE_NET && LMP_FEATURE_HTTPCLIENT

#include <Arduino.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <string.h>

#include "hal/arduino/esp8266/esp8266_hal.h"

namespace lmp::hal::esp8266 {
namespace {

constexpr uint8_t kRedirects = 3;

const char* methodName(HttpMethod method) {
  switch (method) {
    case HttpMethod::Post:    return "POST";
    case HttpMethod::Put:     return "PUT";
    case HttpMethod::Delete:  return "DELETE";
    case HttpMethod::Options: return "OPTIONS";
    default:                  return "GET";
  }
}

// Where the core writes the body, already free of the chunks of the transfer: every piece
// goes to the handler of the caller.
class ChunkSink : public Stream {
 public:
  explicit ChunkSink(HttpChunkHandler& onChunk) : onChunk_(onChunk) {}

  size_t write(uint8_t c) override { return write(&c, 1); }
  size_t write(const uint8_t* data, size_t len) override {
    if (stopped_) return 0;
    if (onChunk_ && !onChunk_(Span<const uint8_t>(data, len))) {
      stopped_ = true;
      return 0;
    }
    return len;
  }
  // The core copies only as much as the destination says it takes: 0 would stall it.
  int availableForWrite() override { return stopped_ ? 0 : 1460; }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override {}

  bool stopped() const { return stopped_; }

 private:
  HttpChunkHandler& onChunk_;
  bool stopped_ = false;
};

Status whyNot(int error) {
  switch (error) {
    case HTTPC_ERROR_READ_TIMEOUT:
    case HTTPC_ERROR_CONNECTION_LOST: return Status::Timeout;
    case HTTPC_ERROR_TOO_LESS_RAM:    return Status::NoMem;
    default:                          return Status::Io;
  }
}

}  // namespace

Status HttpClient::request(const HttpClientRequest& req, HttpClientResponse& res,
                           HttpChunkHandler onChunk) {
  res = HttpClientResponse();
  if (!req.url || !*req.url) return Status::Invalid;
  if (strncmp(req.url, "http://", 7) != 0) return Status::Unsupported;

  WiFiClient tcp;
  HTTPClient http;
  http.setReuse(false);
  http.setTimeout(static_cast<uint16_t>(req.timeoutMs > 65535 ? 65535 : req.timeoutMs));
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setRedirectLimit(kRedirects);
  if (!http.begin(tcp, req.url)) return Status::Invalid;
  if (req.contentType) http.addHeader("Content-Type", req.contentType);
  for (const HttpHeader& h : req.headers) {
    if (h.name && h.value) http.addHeader(h.name, h.value);
  }

  const int code = http.sendRequest(methodName(req.method),
                                    reinterpret_cast<const uint8_t*>(req.body.data),
                                    req.body.size);
  if (code <= 0) {
    http.end();
    return whyNot(code);
  }
  res.status = code;
  const int declared = http.getSize();
  res.contentLength = declared > 0 ? static_cast<size_t>(declared) : 0;

  Status result = Status::Ok;
  ChunkSink sink(onChunk);
  const int written = http.writeToStream(&sink);
  if (sink.stopped()) {
    result = Status::Error;   // who was taking the pieces asked to stop
  } else if (written < 0) {
    result = whyNot(written);
  }
  http.end();
  return result;
}

}  // namespace lmp::hal::esp8266

#endif  // LMP_FEATURE_NET && LMP_FEATURE_HTTPCLIENT

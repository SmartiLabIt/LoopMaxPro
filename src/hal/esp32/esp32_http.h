// LoopMaxPro HAL - ESP32 family: HTTP server.
// Written with the server of the chip maker, which is part of both the Arduino and the
// ESP-IDF builds: the two implementations of the HAL share this file.
// The server has a task of its own: the handlers run there.
#pragma once
#include <esp_http_server.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "hal/api/ihttp.h"
#include "lmp_config.h"

namespace lmp::hal::esp32 {

class HttpServer : public IHttpServer {
 public:
  HttpServer();

  const char* name() const override { return "esp_http_server"; }
  Status start(uint16_t port) override;
  void stop() override;
  bool running() const override { return server_ != nullptr; }
  void poll() override {}
  Status on(HttpMethod methods, const char* path, HttpHandler handler) override;
  Status onUpload(const char* path, HttpUploadHandler parts, HttpHandler done) override;
  void onNotFound(HttpHandler handler) override { notFound_ = handler; }
  ISseChannel* sse(const char* path) override;

 private:
  static constexpr size_t kRoutes = 8;
  static constexpr size_t kUploads = 4;
  static constexpr size_t kFrame = LMP_WEB_EVENT_LEN + 96;

  struct Route {
    uint8_t methods = 0;
    const char* path = nullptr;
    HttpHandler handler;
  };
  struct Upload {
    const char* path = nullptr;
    HttpUploadHandler parts;
    HttpHandler done;
  };

  // The events. One channel for each server.
  class Channel : public ISseChannel {
   public:
    explicit Channel(HttpServer& server) : server_(server) {}
    void setGate(HttpGate gate) override { gate_ = gate; }
    void send(const char* event, StringView data, uint32_t id) override;
    size_t clients() const override;
    void closeAll() override;

    const char* path = nullptr;
    HttpGate gate_;

   private:
    HttpServer& server_;
  };

  static esp_err_t onRequest(httpd_req_t* req);
  static esp_err_t onOpen(httpd_handle_t server, int socket);
  static void onClose(httpd_handle_t server, int socket);
  static void flushWork(void* self);

  esp_err_t serve(httpd_req_t* req);
  esp_err_t receive(httpd_req_t* req, const Upload& upload);
  esp_err_t follow(httpd_req_t* req, IHttpRequest& request);
  void forget(int socket);
  void flush();

  httpd_handle_t server_ = nullptr;
  Route routes_[kRoutes];
  size_t routeCount_ = 0;
  Upload uploads_[kUploads];
  size_t uploadCount_ = 0;
  HttpHandler notFound_;
  Channel channel_;

  // Who follows the events, and the events waiting to leave.
  SemaphoreHandle_t lock_ = nullptr;
  int followers_[LMP_WEB_EVENT_CLIENTS];
  // Taken at start(), from the external RAM when there is one.
  char (*frames_)[kFrame] = nullptr;
  uint16_t frameLength_[LMP_WEB_EVENT_QUEUE] = {};
  size_t frameHead_ = 0;
  size_t frameCount_ = 0;
  bool flushQueued_ = false;
};

}  // namespace lmp::hal::esp32

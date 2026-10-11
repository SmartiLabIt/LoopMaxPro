// LoopMaxPro HAL for the PC - HTTP server.
// The tests hand it requests directly and read what comes back. When asked to listen it also
// opens a real port on this computer, so a browser can talk to the system running on the PC.
#pragma once
#include <atomic>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "hal/api/ihttp.h"

namespace lmp::hal::native {

class Fs;

using HttpHeaders = std::vector<std::pair<std::string, std::string>>;

// A request as a test writes it.
struct HttpCall {
  std::string method = "GET";
  std::string target = "/";   // path and query, as in the first line of a request
  std::string body;
  std::string contentType;
  HttpHeaders headers;
  Ip4 from = Ip4::of(192, 168, 4, 2);
  // For a long body: the client goes away after this many bytes.
  size_t dropAfter = static_cast<size_t>(-1);
};

// What came back.
struct HttpResult {
  uint16_t status = 0;        // 0: the server is not running
  std::string contentType;
  std::string body;
  HttpHeaders headers;
  std::string file;           // path in the filesystem, when the body is a file
  bool gzip = false;
  uint32_t cacheSeconds = 0;
  std::string location;       // where a redirect points

  // Value of a header, empty when missing. The name is matched without case.
  std::string header(const char* name) const;
};

class HttpServer : public IHttpServer {
 public:
  explicit HttpServer(Fs& fs);
  ~HttpServer() override;

  const char* name() const override { return "native"; }
  Status start(uint16_t port) override;
  void stop() override;
  bool running() const override { return running_; }
  void poll() override {}
  Status on(HttpMethod methods, const char* path, HttpHandler handler) override;
  Status onUpload(const char* path, HttpUploadHandler parts, HttpHandler done) override;
  void onNotFound(HttpHandler handler) override { notFound_ = handler; }
  ISseChannel* sse(const char* path) override;

  // ---- hooks for the tests
  // Forgets routes, clients and counters: a server just created.
  void reset();
  // Opens a real port at start(). Off by default.
  void setListen(bool enabled) { listen_ = enabled; }
  bool listening() const { return listening_; }
  // Size of the pieces a long body is handed over in.
  void setUploadChunk(size_t bytes) { uploadChunk_ = bytes ? bytes : 1; }

  HttpResult request(const HttpCall& call);
  HttpResult get(const char* target, const char* token = nullptr);
  HttpResult post(const char* target, const char* body, const char* token = nullptr,
                  const char* contentType = "application/json");

  // Follows the events as a browser does. Returns the client, or -1 when the gate refuses.
  int follow(const char* token = nullptr);
  void unfollow(int client);
  // Text of the events received by a client since the last call, as it travels.
  std::string taken(int client);

 private:
  struct Route {
    uint8_t methods;
    std::string path;
    HttpHandler handler;
  };
  struct Upload {
    std::string path;
    HttpUploadHandler parts;
    HttpHandler done;
  };
  class Channel;
  class Request;
  class Response;

  void dispatch(Request& req, Response& res);
  const Upload* findUpload(const std::string& method, const std::string& target) const;
  void listenLoop();
  void serveSocket(intptr_t socket, Ip4 from);

  Fs& fs_;
  std::vector<Route> routes_;
  std::vector<Upload> uploads_;
  size_t uploadChunk_ = 1024;
  HttpHandler notFound_;
  Channel* channel_ = nullptr;
  bool running_ = false;
  bool listen_ = false;
  uint16_t port_ = 0;
  std::atomic<bool> listening_{false};
  intptr_t listener_ = -1;
};

}  // namespace lmp::hal::native

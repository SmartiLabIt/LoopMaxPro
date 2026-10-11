// LoopMaxPro - web service: the HTTP interface of the device.
// A few generic routes give documents and commands of every service and module: no service
// declares routes of its own. The files of the console come from the program when the build put
// them there (web_bundle.h), otherwise from the filesystem. Files uploaded by hand in the
// filesystem take the place of those of the program: for development.
//
// Requests arrive in the context of the HTTP server. Each one is carried to the application
// task, answered there into one buffer, and sent back from the context it came from.
#pragma once
#include "lmp_config.h"

#if LMP_FEATURE_NET

#include "base/sha256.h"
#include "hal/api/ihttp.h"
#include "hal/api/istorage.h"
#include "kernel/kernel.h"
#include "services/web/web_auth.h"
#include "services/web/web_bundle.h"

namespace lmp {

// Version of the HTTP interface, sent with every answer.
constexpr unsigned kWebApiVersion = LMP_API_VERSION;
// Written when a file of the console is uploaded: the files of the filesystem are there on
// purpose. Without it, on a program that has its console, they are the leftovers of an older
// firmware and go at the start.
constexpr const char* kWebOverrideMark = "/web/.dev";

// Finds a name in text such as "a=1&b=two+words" and decodes its value.
// Returns false when the name is missing or the value does not fit.
bool formValue(StringView form, const char* name, char* out, size_t cap);
// Rewrites the same text as the arguments of the command line: a=1 b="two words".
// Returns false when it does not fit or a value cannot be written that way.
bool formToArgs(StringView form, char* out, size_t cap);
// Content type of a file, from its name.
const char* contentTypeOf(StringView path);
// True for the names that carry the fingerprint of their content: app.3f9a1c07.js
bool hasFingerprint(StringView path);
// True for a path that cannot climb out of the folder it is joined to.
bool safeWebPath(StringView path);

class WebService : public Service {
 public:
  const ServiceInfo& info() const override;
  Status start(Kernel& kernel) override;
  void stop() override;
  void tick(uint32_t nowMs) override;
  void onEvent(const Event& event) override;
  void describe(JsonWriter& w) const override;
  void commands(CommandSet& set) override;

  WebAuth& auth() { return auth_; }
  uint32_t requests() const { return requests_; }

 private:
  // What goes back to the client. The body is in the buffer of the service.
  struct Reply {
    uint16_t code = 200;
    const char* type = nullptr;
    bool file = false;          // the body is a file: path_
    const WebAsset* asset = nullptr;   // the body is a file of the program
    bool gzip = false;
    uint32_t cacheSec = 0;
    bool redirect = false;      // path_ holds the address
    uint32_t retryAfterSec = 0;
    bool cors = false;          // the answer of a module to another program (sdk/http.h)
  };

  // Context of the server.
  void serveApi(hal::IHttpRequest& req, hal::IHttpResponse& res);
  void serveFiles(hal::IHttpRequest& req, hal::IHttpResponse& res);
  void serve(hal::IHttpRequest& req, hal::IHttpResponse& res, bool api);
  bool gate(hal::IHttpRequest& req);

  // Application task.
  void route(hal::IHttpRequest& req, Reply& reply);
  void routeFile(hal::IHttpRequest& req, Reply& reply);
  // The paths /<word>/... of a module that answers other programs. False when no module takes
  // the word: the path is a file.
  bool routeModule(hal::IHttpRequest& req, Reply& reply, BufferSink& sink);
  void routeAuth(StringView what, hal::IHttpRequest& req, Reply& reply, JsonWriter& w);
  void writeState(const hal::IHttpRequest& req, JsonWriter& w) const;
  void writeConfig(StringView target, Reply& reply, JsonWriter& w);
  // False when not even one entry fits in the answer.
  bool writeLog(hal::IHttpRequest& req, Reply& reply, BufferSink& sink);
  void runCommand(StringView rest, hal::IHttpRequest& req, Reply& reply, JsonWriter& w);
  void answerError(Reply& reply, JsonWriter& w, uint16_t code, const char* error,
                   const char* msg, const char* field = nullptr);

  // Long bodies, in pieces: a firmware image, a file of the console. Context of the server.
  bool uploadPart(hal::IHttpRequest& req, const hal::HttpUploadPart& part, bool firmware);
  bool uploadBegin(hal::IHttpRequest& req, const hal::HttpUploadPart& part);
  bool uploadData(const hal::HttpUploadPart& part);
  void uploadEnd(bool whole);
  void uploadDone(hal::IHttpRequest& req, hal::IHttpResponse& res);
  bool uploadRefused(uint16_t code, const char* error, const char* msg,
                     const char* field = nullptr);

  // True for who needs no login: the clients of the access point.
  bool accessFree(const hal::IHttpRequest& req) const;
  bool allowed(hal::IHttpRequest& req);
  bool tokenOf(hal::IHttpRequest& req, char* out, size_t cap) const;
  StringView password() const;
  bool consoleInstalled() const;
  // True when the files of the console in the filesystem were put there by hand (development):
  // they take the place of those of the program.
  bool consoleOverride() const;
  // Removes the files of the console from the filesystem. How many went.
  unsigned removeConsoleFiles();
  uint64_t uptimeMs() const;
  void pushLogs();

  Kernel* kernel_ = nullptr;
  hal::IHttpServer* server_ = nullptr;
  hal::ISseChannel* events_ = nullptr;
  hal::IMutex* replyMutex_ = nullptr;
  LogSource src_ = kLogSys;
  WebAuth auth_;
  // The request being answered: what a document says about the access depends on who asks.
  const hal::IHttpRequest* serving_ = nullptr;

  // One answer at a time. The room for it is taken once, when the service starts, and
  // kept: memory of the program is scarce on some chips that have plenty of the other.
  static constexpr size_t kAnswerMax = LMP_HTTP_RESP_MAX;
  char* body_ = nullptr;
  size_t bodyLength_ = 0;
  char path_[128];
  // Arguments of a command sent as a form. Kept here, not on the stack: on the smallest
  // boards the stack is a few kilobytes. More than this no command can take.
  char args_[LMP_CMD_TEXT + 128];

  // The long body that is arriving: one at a time.
  struct Upload {
    bool firmware = false;
    bool open = false;          // something was started and has to be closed
    bool restart = false;       // the device restarts after the answer
    uint16_t code = 200;
    const char* error = "";
    const char* msg = "";
    const char* field = nullptr;
    size_t bytes = 0;
    hal::IFile* file = nullptr;
    Sha256 hash;
    uint8_t wanted[Sha256::kSize];
    bool check = false;
    char digest[Sha256::kSize * 2 + 1];
    char path[112];
  };
  Upload upload_;
  uint32_t uploads_ = 0;
  bool canUpload_ = false;

  uint32_t requests_ = 0;
  uint32_t refused_ = 0;
  uint32_t failed_ = 0;
  uint32_t lastLogSeq_ = 0;
  uint32_t lastPingMs_ = 0;
};

}  // namespace lmp

#endif  // LMP_FEATURE_NET

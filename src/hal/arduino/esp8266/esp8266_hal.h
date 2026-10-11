// LoopMaxPro HAL - ESP8266 with the Arduino framework.
// One core, no tasks, 80 KB of memory: the system runs in one context and the parts that
// wait on the others become simple flags.
#pragma once
#include "hal/api/board.h"
#include "lmp_config.h"

namespace lmp::hal::esp8266 {

// Console on the UART of the chip. The chip holds 128 bytes and the core waits for them to
// leave: a buffer keeps the text and feeds the UART a little at every round of the system.
class Serial0 : public ISerial {
 public:
  Status begin(uint32_t baud) override;
  bool ready() const override { return ready_; }
  size_t write(const uint8_t* data, size_t len) override;
  size_t read(uint8_t* out, size_t cap) override;
  size_t available() const override;
  void flush() override;

  // Moves to the UART what it has room for. Called at every round of the system.
  void pump();

 private:
  uint8_t buffer_[LMP_SERIAL_TX_BUFFER];
  size_t head_ = 0;
  size_t used_ = 0;
  bool ready_ = false;
};

// LittleFS on the filesystem area chosen by the memory layout of the build.
class Fs : public IFs {
 public:
  Status mount(bool formatOnFail) override;
  void unmount() override;
  bool mounted() const override { return mounted_; }
  Status info(FsInfo& out) const override;
  bool replacesWhole() const override { return true; }   // LittleFS writes on copies
  IFile* open(const char* path, OpenMode mode) override;
  bool exists(const char* path) const override;
  Status remove(const char* path) override;
  Status rename(const char* from, const char* to) override;
  Status mkdir(const char* path) override;
  Status list(const char* dir, DirVisitor visitor) override;

 private:
  bool mounted_ = false;
};

// The chip has one frequency and one resolution for all the pins.
class Pwm : public IPwm {
 public:
  bool supports(uint8_t pin) const override;
  // Busy when other pins are in use with a different frequency or resolution.
  Status setup(uint8_t pin, uint32_t frequencyHz, uint8_t resolutionBits) override;
  Status write(uint8_t pin, uint32_t duty) override;
  Status release(uint8_t pin) override;

 private:
  uint32_t pins_ = 0;          // one bit for each pin in use
  uint32_t frequencyHz_ = 0;
  uint8_t resolutionBits_ = 0;
};

#if LMP_FEATURE_NET

// Wi-Fi of the chip, through the library of the core. The credentials are never given to
// the chip to keep: they belong to the settings.
class Net : public INet {
 public:
  Status init(const char* hostname) override;
  void setEventHandler(NetEventHandler handler) override { handler_ = handler; }
  void poll() override;

  Status staConnect(const char* ssid, const char* password) override;
  Status staDisconnect() override;
  void staInfo(StaInfo& out) const override;
  void staMac(Mac& out) const override;

  Status apStart(const char* ssid, const char* password, Ip4 ip, uint8_t channel) override;
  Status apStop() override;
  void apInfo(ApInfo& out) const override;

  Status scanStart() override;
  ScanState scanState() const override { return scan_; }
  size_t scanCount() const override { return found_; }
  bool scanResult(size_t index, WifiNetwork& out) const override;
  void scanClear() override;

  Status mdnsStart(const char* hostname, uint16_t httpPort) override;
  void mdnsStop() override;
  bool mdnsActive() const override { return mdns_; }
  Status captiveStart(Ip4 address) override;
  void captiveStop() override;
  bool captiveActive() const override { return captive_; }

  // Called by the library of the core.
  void onGotAddress();
  void onDisconnected(uint8_t reason);
  void onAssociated();
  void onApClient(bool joined);
  void onScanDone(int networks);

 private:
  static constexpr size_t kRecords = LMP_NET_SCAN_MAX + 4;

  void emit(NetEvent event) const;
  void applyMode();

  NetEventHandler handler_;
  bool ready_ = false;
  bool wantSta_ = false;
  bool wantAp_ = false;
  bool scanning_ = false;
  bool modeStale_ = false;
  StaInfo sta_;
  ApInfo ap_;
  ScanState scan_ = ScanState::Idle;
  WifiNetwork records_[kRecords];
  size_t found_ = 0;
  bool mdns_ = false;
  bool captive_ = false;
};

// HTTP server of the core. It has no task: the requests are served inside poll().
class HttpServer : public IHttpServer {
 public:
  const char* name() const override { return "ESP8266WebServer"; }
  Status start(uint16_t port) override;
  void stop() override;
  bool running() const override { return running_; }
  void poll() override;
  Status on(HttpMethod methods, const char* path, HttpHandler handler) override;
  // The server of the core reads a body whole into memory before anybody sees it: a
  // firmware does not fit. No long bodies on this chip.
  Status onUpload(const char*, HttpUploadHandler, HttpHandler) override {
    return Status::Unsupported;
  }
  void onNotFound(HttpHandler handler) override { notFound_ = handler; }
  ISseChannel* sse(const char* path) override;

  // Called by the server of the core for every request.
  void serve();

 private:
  static constexpr size_t kRoutes = 4;

  struct Route {
    uint8_t methods = 0;
    const char* path = nullptr;
    HttpHandler handler;
  };

  class Channel : public ISseChannel {
   public:
    void setGate(HttpGate gate) override { gate_ = gate; }
    void send(const char* event, StringView data, uint32_t id) override;
    size_t clients() const override;
    void closeAll() override;

    const char* path = nullptr;
    HttpGate gate_;
  };

  void follow(IHttpRequest& request);

  Route routes_[kRoutes];
  size_t routeCount_ = 0;
  HttpHandler notFound_;
  Channel channel_;
  bool running_ = false;
  bool hooked_ = false;
};

// Time from a server, with the client of the network stack.
class Ntp : public INtp {
 public:
  Status start(const char* server) override;
  void stop() override;
  bool running() const override { return running_; }
  uint32_t answers() const override;

 private:
  bool running_ = false;
  bool hooked_ = false;
  char server_[49] = "";   // the stack keeps the pointer
};

#if LMP_FEATURE_HTTPCLIENT
// Web services over plain HTTP: a secure connection does not fit in the memory of the chip.
// There is no worker task here: a request holds the application task while it lasts.
class HttpClient : public IHttpClient {
 public:
  bool secure() const override { return false; }
  Status request(const HttpClientRequest& req, HttpClientResponse& res,
                 HttpChunkHandler onChunk) override;
};
#endif

#endif  // LMP_FEATURE_NET

#if LMP_FEATURE_BUS

// The one I2C bus of the core, on any two pins. A transfer holds 128 bytes at most.
class I2c : public II2c {
 public:
  Status begin(uint8_t sda, uint8_t scl, uint32_t frequencyHz) override;
  void end() override { up_ = false; }
  bool probe(uint8_t address) override;
  Status write(uint8_t address, const uint8_t* data, size_t len, bool stop) override;
  Status read(uint8_t address, uint8_t* out, size_t len) override;
  Status writeRead(uint8_t address, const uint8_t* tx, size_t txLen, uint8_t* rx,
                   size_t rxLen) override;

 private:
  bool up_ = false;
};

#endif  // LMP_FEATURE_BUS

// One analog input, A0, from 0 to 1 V at the pin of the chip.
class Adc : public IAdc {
 public:
  bool supports(uint8_t pin) const override;
  uint8_t resolutionBits() const override { return 10; }
  Status readRaw(uint8_t pin, uint32_t& value) override;
  // Voltage at the pin of the chip. Boards with a divider in front of it read lower.
  Status readMillivolts(uint8_t pin, uint32_t& millivolts) override;
};

}  // namespace lmp::hal::esp8266

// LoopMaxPro - where the device is.
// The position is written by who knows it: the user, a module with a GPS, the cloud, or a
// server on Internet that tells it from the public address, asked at every start. Who is
// more precise wins: a GPS, then by hand, then the others. What is written by hand stays until
// it is cleared. The server is a public service unless the settings say another, or none.
#pragma once
#include "lmp_config.h"

#if LMP_FEATURE_GEO

#include "kernel/kernel.h"

namespace lmp {

struct HttpAnswer;

enum class GeoSource : uint8_t { None = 0, Manual = 1, Lookup = 2, Cloud = 3, Gps = 4 };
const char* geoSourceName(GeoSource source);

struct GeoPlace {
  double lat = 0;
  double lon = 0;
  const char* city = "";
  const char* region = "";
  const char* country = "";   // two letters
};

// What a server said about the device. Fields the server did not give are left as they are.
struct GeoFound {
  bool hasPosition = false;
  double lat = 0;
  double lon = 0;
  FixedString<40> city;
  FixedString<40> region;
  FixedString<2> country;
  FixedString<45> ip;
  FixedString<48> isp;
  FixedString<40> tz;         // time zone, IANA name
  FixedString<48> posixTz;    // its rules, as the C library wants them
};

// Reads the answer of a server that tells the position. The names of the most common
// services are understood. False when the text is not JSON or says nothing useful.
bool geoParse(StringView json, GeoFound& out);

class IGeoService {
 public:
  static constexpr ServiceId kId = ServiceId::Geo;
  virtual ~IGeoService() = default;

  virtual bool known() const = 0;
  virtual GeoSource source() const = 0;
  virtual double latitude() const = 0;
  virtual double longitude() const = 0;
  // Invalid when the position is not on the Earth or the country is not two letters.
  // Denied when a more precise source wrote the position: by hand beats everything, a GPS
  // beats the servers. A GPS that barely moves is taken without saving.
  virtual Status set(const GeoPlace& place, GeoSource source) = 0;
  // Address the device has on Internet, as a server saw it. Empty when not known.
  virtual const char* publicIp() const = 0;
};

class GeoService : public Service, public IGeoService {
 public:
  const ServiceInfo& info() const override;
  Status start(Kernel& kernel) override;
  void tick(uint32_t nowMs) override;
  void onEvent(const Event& event) override;
  void describe(JsonWriter& w) const override;
  void commands(CommandSet& set) override;
  const char* reportPart() const override { return "geo"; }
  void* api() override { return static_cast<IGeoService*>(this); }

  bool known() const override;
  GeoSource source() const override;
  double latitude() const override;
  double longitude() const override;
  Status set(const GeoPlace& place, GeoSource source) override;
  const char* publicIp() const override { return ip_.c_str(); }

 private:
  enum class Lookup : uint8_t { Idle, Running, Done, Failed };

  Status lookup(uint32_t ref);
  void onAnswer(const HttpAnswer& answer);
  void changed();

  Kernel* kernel_ = nullptr;
  LogSource src_ = kLogSys;
  Lookup lookup_ = Lookup::Idle;
  uint32_t lookupRef_ = 0;
  bool askedOnce_ = false;      // the server was asked by itself since the start
  // A question asked by itself that failed is asked again a little later, a few times: at
  // the start other secure connections may be opening and take the memory.
  uint8_t retries_ = 0;
  uint32_t retryAtMs_ = 0;
  bool gpsUnsaved_ = false;     // a GPS position newer than the saved one
  uint32_t gpsSavedMs_ = 0;
  FixedString<45> ip_;
  FixedString<48> isp_;
  FixedString<96> url_;         // the address of the request in progress
  char answer_[1024];
};

}  // namespace lmp

#endif  // LMP_FEATURE_GEO

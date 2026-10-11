// LoopMaxPro HAL - monotonic time and wall clock, and the time asked to a server.
// When to ask and what to do with the answer belongs to the clock service.
#pragma once
#include "hal/api/types.h"

namespace lmp::hal {

struct DateTime {
  uint16_t year = 1970;
  uint8_t month = 1;      // 1..12
  uint8_t day = 1;        // 1..31
  uint8_t hour = 0;
  uint8_t minute = 0;
  uint8_t second = 0;
  uint8_t weekday = 0;    // 0 = Sunday
  int32_t utcOffset = 0;  // seconds, daylight saving included
};

class IClock {
 public:
  virtual ~IClock() = default;

  virtual uint32_t millis() const = 0;     // wraps after about 49 days
  virtual uint64_t uptimeMs() const = 0;   // never wraps
  virtual uint64_t micros() const = 0;

  virtual int64_t unixTime() const = 0;    // 0 when the clock was never set
  virtual Status setUnixTime(int64_t seconds) = 0;
  // POSIX TZ string, for example "CET-1CEST,M3.5.0,M10.5.0/3".
  virtual Status setTimezone(const char* posixTz) = 0;
  virtual bool localTime(int64_t unixSeconds, DateTime& out) const = 0;
};

// Time from a server on the network, NTP. Nothing here waits: the answers arrive when they
// arrive, and each one sets the clock of the board.
class INtp {
 public:
  virtual ~INtp() = default;
  // Asks now, and again at the interval the driver has. The text must stay valid.
  virtual Status start(const char* server) = 0;
  virtual void stop() = 0;
  virtual bool running() const = 0;
  // Answers received since the board started: it grows by one each time the clock is set.
  virtual uint32_t answers() const = 0;
};

}  // namespace lmp::hal

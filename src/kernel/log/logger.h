// LoopMaxPro - logger.
// Fixed size entries in a ring: no allocation after construction.
// It is the only kernel component that can be called from any task.
#pragma once
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include "base/ring_buffer.h"
#include "hal/api/iclock.h"
#include "hal/api/irtos.h"
#include "lmp_config.h"

namespace lmp {

enum class LogLevel : uint8_t { Off = 0, Error = 1, Warn = 2, Info = 3, Debug = 4, Trace = 5 };

const char* logLevelName(LogLevel level);      // "error", "warn", ...
char logLevelLetter(LogLevel level);           // 'E', 'W', ...
// Accepts a name or a digit. Returns false when the text is not a level.
bool logLevelParse(const char* text, LogLevel& out);

using LogSource = uint8_t;
constexpr LogSource kLogSys = 0;               // always registered

struct LogEntry {
  uint32_t seq = 0;       // grows by one for each entry, never reused
  uint32_t ms = 0;        // uptime when it was written
  LogLevel level = LogLevel::Off;
  LogSource source = kLogSys;
  bool truncated = false;
  char msg[LMP_LOG_MSG_LEN] = {0};
};

class ILogSink {
 public:
  virtual ~ILogSink() = default;
  // Called for every accepted entry, outside the logger lock.
  virtual void onLog(const LogEntry& entry, const char* sourceName) = 0;
};

class Logger {
 public:
  // The mutex can be null on boards with a single context. The entries kept for the console
  // are taken here, once, from the external RAM when the board has it (hal/api/memory.h).
  Logger(hal::IClock& clock, hal::IMutex* mutex);
  ~Logger();
  Logger(const Logger&) = delete;
  Logger& operator=(const Logger&) = delete;

  // ---- sources
  // Registers a name and returns its id. The text must stay valid: use literals.
  // The same name returns the same id. When the table is full returns kLogSys.
  LogSource addSource(const char* name, const char* icon = "");
  const char* sourceName(LogSource id) const;
  const char* sourceIcon(LogSource id) const;
  size_t sourceCount() const { return sourceCount_; }
  bool findSource(const char* name, LogSource& out) const;

  // ---- levels
  void setLevel(LogLevel level);                    // every source
  void setLevel(LogSource source, LogLevel level);  // one source
  LogLevel level(LogSource source) const;
  bool enabled(LogSource source, LogLevel level) const;

  // ---- writing
  void write(LogLevel level, LogSource source, const char* fmt, ...)
      __attribute__((format(printf, 4, 5)));
  void vwrite(LogLevel level, LogSource source, const char* fmt, va_list args);

  // ---- sinks
  bool addSink(ILogSink* sink);
  void removeSink(ILogSink* sink);

  // ---- reading
  // How many entries are kept: LMP_LOG_RING, or 0 when there was no memory for them.
  size_t capacity() const { return ring_.capacity(); }
  // Copies the entries with seq greater than sinceSeq, oldest first. Returns how many.
  size_t read(uint32_t sinceSeq, LogEntry* out, size_t max) const;
  uint32_t lastSeq() const;
  size_t count() const;
  uint32_t dropped() const;    // entries overwritten before being cleared
  void clear();

  // Marks the end of the startup phase, shown as boot timeline by the console.
  void markBootDone();
  uint32_t bootDoneMs() const { return bootDoneMs_; }

  // ---- process wide instance used by the LMP_LOG macros
  static void setInstance(Logger* logger) { instance_ = logger; }
  static Logger* instance() { return instance_; }

 private:
  struct Source {
    const char* name = "";
    const char* icon = "";
    LogLevel level = LogLevel::Info;
  };

  hal::IClock& clock_;
  hal::IMutex* mutex_;
  LogEntry* storage_ = nullptr;
  RingSpan<LogEntry> ring_;
  Source sources_[LMP_LOG_MAX_SOURCES];
  size_t sourceCount_ = 0;
  ILogSink* sinks_[LMP_LOG_MAX_SINKS] = {};
  uint32_t seq_ = 0;
  uint32_t bootDoneMs_ = 0;

  static Logger* instance_;
};

}  // namespace lmp

// On ESP8266 constant texts live in RAM unless they are asked to stay in flash: the formats of
// the log stay there, and the logger reads them from there.
#if defined(LMP_MCU_ESP8266)
#include <pgmspace.h>
#define LMP_LOG_FMT(fmt) PSTR(fmt)
#else
#define LMP_LOG_FMT(fmt) fmt
#endif

// Calls above LMP_LOG_LEVEL are removed by the compiler, arguments included.
#define LMP_LOG_(lvl, num, src, fmt, ...)                                                    \
  do {                                                                                     \
    if ((num) <= LMP_LOG_LEVEL) {                                                          \
      ::lmp::Logger* lmp_logger_ = ::lmp::Logger::instance();                              \
      if (lmp_logger_) lmp_logger_->write(::lmp::LogLevel::lvl, (src), LMP_LOG_FMT(fmt), ##__VA_ARGS__); \
    }                                                                                      \
  } while (0)

#define LMP_LOGE(src, fmt, ...) LMP_LOG_(Error, 1, src, fmt, ##__VA_ARGS__)
#define LMP_LOGW(src, fmt, ...) LMP_LOG_(Warn, 2, src, fmt, ##__VA_ARGS__)
#define LMP_LOGI(src, fmt, ...) LMP_LOG_(Info, 3, src, fmt, ##__VA_ARGS__)
#define LMP_LOGD(src, fmt, ...) LMP_LOG_(Debug, 4, src, fmt, ##__VA_ARGS__)
#define LMP_LOGT(src, fmt, ...) LMP_LOG_(Trace, 5, src, fmt, ##__VA_ARGS__)

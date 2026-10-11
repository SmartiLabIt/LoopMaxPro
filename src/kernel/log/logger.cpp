#include "kernel/log/logger.h"

#include <stdio.h>
#include <string.h>

#include <new>

#include "hal/api/memory.h"

namespace lmp {

Logger* Logger::instance_ = nullptr;

const char* logLevelName(LogLevel level) {
  switch (level) {
    case LogLevel::Error: return "error";
    case LogLevel::Warn:  return "warn";
    case LogLevel::Info:  return "info";
    case LogLevel::Debug: return "debug";
    case LogLevel::Trace: return "trace";
    case LogLevel::Off:   break;
  }
  return "off";
}

char logLevelLetter(LogLevel level) {
  switch (level) {
    case LogLevel::Error: return 'E';
    case LogLevel::Warn:  return 'W';
    case LogLevel::Info:  return 'I';
    case LogLevel::Debug: return 'D';
    case LogLevel::Trace: return 'T';
    case LogLevel::Off:   break;
  }
  return '-';
}

bool logLevelParse(const char* text, LogLevel& out) {
  if (!text || !*text) return false;
  if (text[0] >= '0' && text[0] <= '5' && text[1] == '\0') {
    out = static_cast<LogLevel>(text[0] - '0');
    return true;
  }
  for (uint8_t i = 0; i <= 5; ++i) {
    const LogLevel level = static_cast<LogLevel>(i);
    if (strcmp(text, logLevelName(level)) == 0) {
      out = level;
      return true;
    }
  }
  return false;
}

Logger::Logger(hal::IClock& clock, hal::IMutex* mutex) : clock_(clock), mutex_(mutex) {
  storage_ = static_cast<LogEntry*>(hal::allocLarge(sizeof(LogEntry) * LMP_LOG_RING));
  if (storage_) {
    for (size_t i = 0; i < LMP_LOG_RING; ++i) new (&storage_[i]) LogEntry();
  }
  ring_.attach(storage_, LMP_LOG_RING);
  sources_[0].name = "sys";
  sources_[0].icon = "";
  sourceCount_ = 1;
  setLevel(static_cast<LogLevel>(LMP_LOG_LEVEL));
}

Logger::~Logger() {
  if (instance_ == this) instance_ = nullptr;
  ring_.attach(nullptr, 0);
  hal::freeLarge(storage_);   // LogEntry has nothing to destroy
}

// ---------------------------------------------------------------- sources

LogSource Logger::addSource(const char* name, const char* icon) {
  if (!name) return kLogSys;
  hal::LockGuard guard(mutex_);
  for (size_t i = 0; i < sourceCount_; ++i) {
    if (strcmp(sources_[i].name, name) == 0) return static_cast<LogSource>(i);
  }
  if (sourceCount_ >= LMP_LOG_MAX_SOURCES) return kLogSys;
  Source& s = sources_[sourceCount_];
  s.name = name;
  s.icon = icon ? icon : "";
  s.level = sources_[0].level;
  return static_cast<LogSource>(sourceCount_++);
}

const char* Logger::sourceName(LogSource id) const {
  return id < sourceCount_ ? sources_[id].name : "?";
}

const char* Logger::sourceIcon(LogSource id) const {
  return id < sourceCount_ ? sources_[id].icon : "";
}

bool Logger::findSource(const char* name, LogSource& out) const {
  if (!name) return false;
  for (size_t i = 0; i < sourceCount_; ++i) {
    if (strcmp(sources_[i].name, name) == 0) {
      out = static_cast<LogSource>(i);
      return true;
    }
  }
  return false;
}

// ---------------------------------------------------------------- levels

void Logger::setLevel(LogLevel level) {
  for (Source& s : sources_) s.level = level;
}

void Logger::setLevel(LogSource source, LogLevel level) {
  if (source < LMP_LOG_MAX_SOURCES) sources_[source].level = level;
}

LogLevel Logger::level(LogSource source) const {
  return source < LMP_LOG_MAX_SOURCES ? sources_[source].level : LogLevel::Off;
}

bool Logger::enabled(LogSource source, LogLevel lvl) const {
  return lvl != LogLevel::Off && lvl <= level(source);
}

// ---------------------------------------------------------------- writing

void Logger::write(LogLevel level, LogSource source, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vwrite(level, source, fmt, args);
  va_end(args);
}

void Logger::vwrite(LogLevel level, LogSource source, const char* fmt, va_list args) {
  if (!fmt || !enabled(source, level)) return;

  // Formatted outside the lock, on the stack of the caller.
  LogEntry entry;
  entry.level = level;
  entry.source = source < sourceCount_ ? source : kLogSys;
#if defined(LMP_MCU_ESP8266)
  // The format can be in flash (LMP_LOG_FMT): read a byte at a time, as the chip wants.
  const int n = vsnprintf_P(entry.msg, sizeof(entry.msg), fmt, args);
#else
  const int n = vsnprintf(entry.msg, sizeof(entry.msg), fmt, args);
#endif
  if (n < 0) {
    entry.msg[0] = '\0';
  } else if (static_cast<size_t>(n) >= sizeof(entry.msg)) {
    entry.truncated = true;
    // Do not leave half of a UTF-8 sequence at the end.
    size_t len = sizeof(entry.msg) - 1;
    while (len > 0 && (static_cast<unsigned char>(entry.msg[len - 1]) & 0xC0) == 0x80) --len;
    if (len > 0 && (static_cast<unsigned char>(entry.msg[len - 1]) & 0xC0) == 0xC0) --len;
    entry.msg[len] = '\0';
  }
  entry.ms = clock_.millis();

  ILogSink* sinks[LMP_LOG_MAX_SINKS];
  {
    hal::LockGuard guard(mutex_);
    entry.seq = ++seq_;
    ring_.push(entry);
    memcpy(sinks, sinks_, sizeof(sinks));
  }

  const char* name = sourceName(entry.source);
  for (ILogSink* sink : sinks) {
    if (sink) sink->onLog(entry, name);
  }
}

// ---------------------------------------------------------------- sinks

bool Logger::addSink(ILogSink* sink) {
  if (!sink) return false;
  hal::LockGuard guard(mutex_);
  for (ILogSink*& slot : sinks_) {
    if (slot == sink) return true;
  }
  for (ILogSink*& slot : sinks_) {
    if (!slot) {
      slot = sink;
      return true;
    }
  }
  return false;
}

void Logger::removeSink(ILogSink* sink) {
  hal::LockGuard guard(mutex_);
  for (ILogSink*& slot : sinks_) {
    if (slot == sink) slot = nullptr;
  }
}

// ---------------------------------------------------------------- reading

size_t Logger::read(uint32_t sinceSeq, LogEntry* out, size_t max) const {
  if (!out || max == 0) return 0;
  hal::LockGuard guard(mutex_);
  size_t n = 0;
  for (size_t i = 0; i < ring_.size() && n < max; ++i) {
    const LogEntry& e = ring_.at(i);
    if (e.seq > sinceSeq) out[n++] = e;
  }
  return n;
}

uint32_t Logger::lastSeq() const {
  hal::LockGuard guard(mutex_);
  return seq_;
}

size_t Logger::count() const {
  hal::LockGuard guard(mutex_);
  return ring_.size();
}

uint32_t Logger::dropped() const {
  hal::LockGuard guard(mutex_);
  return static_cast<uint32_t>(ring_.dropped());
}

void Logger::clear() {
  hal::LockGuard guard(mutex_);
  ring_.clear();  // seq keeps growing: readers never see an old number again
}

void Logger::markBootDone() {
  if (bootDoneMs_ == 0) bootDoneMs_ = clock_.millis();
}

}  // namespace lmp

#include "services/serial/serial_service.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "services/log/log_service.h"

namespace lmp {
namespace {

// Sends JSON to the serial port in small pieces: any size, no large buffer.
class SerialJsonSink : public JsonSink {
 public:
  explicit SerialJsonSink(hal::ISerial& serial) : serial_(serial) {}
  ~SerialJsonSink() override { flush(); }

  bool write(const char* data, size_t len) override {
    while (len) {
      const size_t room = sizeof(buf_) - used_;
      const size_t n = len < room ? len : room;
      memcpy(buf_ + used_, data, n);
      used_ += n;
      data += n;
      len -= n;
      if (used_ == sizeof(buf_)) flush();
    }
    return true;
  }

  void flush() {
    if (used_) serial_.write(reinterpret_cast<const uint8_t*>(buf_), used_);
    used_ = 0;
  }

 private:
  hal::ISerial& serial_;
  char buf_[96];
  size_t used_ = 0;
};

// Splits the next word off the line. Returns null at the end.
char* nextWord(char*& cursor) {
  while (*cursor == ' ' || *cursor == '\t') ++cursor;
  if (!*cursor) return nullptr;
  char* word = cursor;
  while (*cursor && *cursor != ' ' && *cursor != '\t') ++cursor;
  if (*cursor) *cursor++ = '\0';
  return word;
}

}  // namespace

const ServiceInfo& SerialService::info() const {
  static const ServiceInfo kInfo{ServiceId::Serial, "serial", "\xF0\x9F\x92\xBB", Mode::Any,
                                 RestartPolicy::Retry, 50, DepList(ServiceId::Log)};
  return kInfo;
}

Status SerialService::start(Kernel& kernel) {
  kernel_ = &kernel;
  serial_ = kernel.board().serial;
  if (!serial_) return Status::Unsupported;
  if (!serial_->ready()) LMP_TRY(serial_->begin(LMP_SERIAL_BAUD));

  sink_.attach(*serial_);
  // Prints what was logged before this service started, then follows the new entries.
  Logger& log = kernel.log();
  LogEntry block[4];
  uint32_t since = 0;
  size_t n;
  while ((n = log.read(since, block, sizeof(block) / sizeof(block[0]))) > 0) {
    for (size_t i = 0; i < n; ++i) {
      sink_.onLog(block[i], log.sourceName(block[i].source));
      since = block[i].seq;
    }
  }
  log.addSink(&sink_);
  length_ = 0;
  overflow_ = false;
  return Status::Ok;
}

void SerialService::stop() {
  if (kernel_) kernel_->log().removeSink(&sink_);
}

void SerialService::describe(JsonWriter& w) const {
  w.u32("baud", LMP_SERIAL_BAUD);
  w.boolean("ready", serial_ && serial_->ready());
  w.boolean("cli", LMP_FEATURE_CLI != 0);
  w.str("level", logLevelName(sink_.level()));
}

void SerialService::println(const char* text) {
  if (serial_) serial_->println(text);
}

void SerialService::tick(uint32_t) {
#if LMP_FEATURE_CLI
  if (!serial_) return;
  uint8_t chunk[32];
  size_t n;
  while ((n = serial_->read(chunk, sizeof(chunk))) > 0) {
    for (size_t i = 0; i < n; ++i) {
      const char c = static_cast<char>(chunk[i]);
      if (c == '\n' && lastWasCr_) {
        lastWasCr_ = false;
        continue;  // second half of CR LF
      }
      lastWasCr_ = c == '\r';
      if (c == '\r' || c == '\n') {
        if (overflow_) {
          println("error: line too long");
        } else if (length_) {
          line_[length_] = '\0';
          handleLine(line_);
        }
        length_ = 0;
        overflow_ = false;
      } else if (c == '\b' || c == 0x7F) {
        if (length_) --length_;
      } else if (length_ == 0 && (c <= ' ' || static_cast<unsigned char>(c) >= 0x80)) {
        // Spaces and stray bytes before the command, such as a byte order mark.
      } else if (length_ + 1 < sizeof(line_)) {
        line_[length_++] = c;
      } else {
        overflow_ = true;
      }
    }
  }
#endif
}

void SerialService::execute(const char* line) {
  if (!line) return;
  char copy[LMP_CLI_LINE];
  snprintf(copy, sizeof(copy), "%s", line);
  handleLine(copy);
}

void SerialService::printHelp() {
  println("help                                  this list");
  println("info                                  device and system");
  println("report [parts]                        device report, parts separated by commas");
  println("targets                               who accepts documents and commands");
  println("doc <target>                          state of a target");
  println("man <target>                          commands of a target");
  println("cmd <target> <command> [name=value]   runs a command");
  println("log [count]                           last log entries");
}

void SerialService::printLog(uint16_t count) {
  ILog* log = kernel_->api<ILog>();
  if (!log) {
    println("error: log service not available");
    return;
  }
  Logger& logger = kernel_->log();
  ILog::Query query;
  const uint32_t last = logger.lastSeq();
  query.since = last > count ? last - count : 0;
  query.max = count;
  SerialJsonSink sink(*serial_);
  JsonWriter w(sink);
  log->writeEntries(w, query);
  sink.flush();
  println("");
}

void SerialService::handleLine(char* line) {
  if (!kernel_ || !serial_) return;
  char* cursor = line;
  const char* verb = nextWord(cursor);
  if (!verb) return;

  if (strcmp(verb, "help") == 0 || strcmp(verb, "?") == 0) {
    printHelp();
    return;
  }

  if (strcmp(verb, "info") == 0 || strcmp(verb, "report") == 0) {
    const char* parts = strcmp(verb, "info") == 0 ? "device,system" : nextWord(cursor);
    SerialJsonSink sink(*serial_);
    JsonWriter w(sink);
    kernel_->report(w, StringView(parts), true);
    sink.flush();
    println("");
    return;
  }

  if (strcmp(verb, "targets") == 0) {
    SerialJsonSink sink(*serial_);
    JsonWriter w(sink);
    kernel_->commands().targets(w);
    sink.flush();
    println("");
    return;
  }

  if (strcmp(verb, "doc") == 0 || strcmp(verb, "man") == 0) {
    const char* target = nextWord(cursor);
    if (!target) {
      println("error: target missing");
      return;
    }
    SerialJsonSink sink(*serial_);
    JsonWriter w(sink);
    Status s;
    if (verb[0] == 'd') {
      w.beginObject();
      s = kernel_->commands().describe(StringView(target), w);
      w.endObject();
    } else {
      s = kernel_->commands().manifest(StringView(target), w);
    }
    sink.flush();
    println("");
    if (s != Status::Ok) println("error: target not found");
    return;
  }

  if (strcmp(verb, "cmd") == 0) {
    const char* target = nextWord(cursor);
    const char* command = nextWord(cursor);
    if (!target || !command) {
      println("error: use cmd <target> <command> [name=value]");
      return;
    }
    // The answer is built in memory and printed at the end: the log lines written
    // while the command runs do not end up in the middle of it.
    FixedSink<768> sink;
    JsonWriter w(sink);
    kernel_->commands().answer(StringView(target), StringView(command), StringView(cursor),
                               ArgsForm::Text, CmdSource::Serial, w);
    println(w.complete() ? sink.c_str() : "error: answer too long");
    return;
  }

  if (strcmp(verb, "log") == 0) {
    const char* count = nextWord(cursor);
    const long n = count ? strtol(count, nullptr, 10) : 20;
    printLog(static_cast<uint16_t>(n < 1 ? 1 : (n > LMP_LOG_RING ? LMP_LOG_RING : n)));
    return;
  }

  println("error: unknown command, type help");
}

}  // namespace lmp

// LoopMaxPro - command bus.
// Services and modules declare their commands here. The same handler answers requests
// coming from the local console, the remote console, MQTT, the serial port or another module.
//
//   static constexpr ArgSpec kSetArgs[] = { Arg::u8("channel", 0, 5), Arg::boolean("value") };
//   set.add("set", "Switches one channel", kSetArgs,
//           [this](const Args& a, CmdContext&) { return write(a.u8("channel"), a.boolean("value")); });
//
// Argument tables must have static storage: the bus keeps a pointer to them.
// Help texts are written LMP_HELP("..."): the light edition leaves them out of the firmware.
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "base/delegate.h"
#include "base/span.h"
#include "base/status.h"
#include "base/string_view.h"
#include "kernel/event/event.h"
#include "kernel/fields/fields.h"
#include "kernel/json/json_reader.h"
#include "kernel/json/json_writer.h"
#include "kernel/log/logger.h"
#include "lmp_config.h"

#if LMP_CMD_HELP
#define LMP_HELP(text) text
#else
#define LMP_HELP(text) ""
#endif

namespace lmp {

enum class CmdSource : uint8_t { Internal, Local, Remote, Mqtt, Serial };
const char* cmdSourceName(CmdSource source);

enum CmdFlags : uint8_t {
  kCmdNone = 0,
  kCmdConfirm = 1,  // the console asks for confirmation before sending it
  kCmdAsync = 2,    // returns Pending: the outcome arrives with a CommandDone event
  kCmdAnyArgs = 4   // takes any name: the handler reads the request itself, from ctx.raw
};

struct ArgSpec {
  const char* name;
  FieldSpec spec;
  bool required;

  constexpr ArgSpec optional() const { return ArgSpec{name, spec, false}; }
};

// Builders of argument specs.
struct Arg {
  static constexpr ArgSpec boolean(const char* name) { return {name, field::Bool(), true}; }
  static constexpr ArgSpec u8(const char* name, double min = 0, double max = 255) {
    return {name, field::U8(min, max), true};
  }
  static constexpr ArgSpec u16(const char* name, double min = 0, double max = 65535) {
    return {name, field::U16(min, max), true};
  }
  static constexpr ArgSpec u32(const char* name, double min = 0, double max = 4294967295.0) {
    return {name, field::U32(min, max), true};
  }
  static constexpr ArgSpec i32(const char* name, double min = -2147483648.0,
                               double max = 2147483647.0) {
    return {name, field::I32(min, max), true};
  }
  static constexpr ArgSpec i64(const char* name) { return {name, field::I64(), true}; }
  static constexpr ArgSpec f64(const char* name, double min = -1.7e308, double max = 1.7e308) {
    return {name, field::F64(min, max), true};
  }
  static constexpr ArgSpec str(const char* name, double minLen = 0, double maxLen = 64) {
    return {name, field::Str(minLen, maxLen), true};
  }
  // Text that must never appear in logs or documents.
  static constexpr ArgSpec secret(const char* name, double minLen = 0, double maxLen = 64) {
    return {name, field::Str(minLen, maxLen).secret(), true};
  }
};

// Validated arguments of one call. Values are native.
class Args {
 public:
  bool has(const char* name) const;

  bool boolean(const char* name, bool fallback = false) const;
  int64_t i64(const char* name, int64_t fallback = 0) const;
  int32_t i32(const char* name, int32_t fallback = 0) const {
    return static_cast<int32_t>(i64(name, fallback));
  }
  uint32_t u32(const char* name, uint32_t fallback = 0) const {
    return static_cast<uint32_t>(i64(name, fallback));
  }
  uint16_t u16(const char* name, uint16_t fallback = 0) const {
    return static_cast<uint16_t>(i64(name, fallback));
  }
  uint8_t u8(const char* name, uint8_t fallback = 0) const {
    return static_cast<uint8_t>(i64(name, fallback));
  }
  double f64(const char* name, double fallback = 0) const;
  const char* str(const char* name, const char* fallback = "") const;

  size_t count() const { return count_; }

 private:
  friend class CommandBus;

  struct Slot {
    const char* name = nullptr;
    FieldKind kind = FieldKind::Bool;
    int64_t integer = 0;
    double number = 0;
    const char* text = nullptr;
  };

  const Slot* find(const char* name) const;

  Slot slots_[LMP_CMD_MAX_ARGS];
  uint8_t count_ = 0;
  char text_[LMP_CMD_TEXT];
  size_t used_ = 0;
};

struct CmdContext {
  CmdSource source = CmdSource::Internal;
  // Where the handler writes the members of its result. Null when nobody reads it.
  JsonWriter* result = nullptr;
  // Identifies the request in the CommandDone event of asynchronous commands.
  uint32_t ref = 0;
  // Set by the bus when an argument is rejected.
  const char* errorField = nullptr;
  // The arguments as they arrived: an object, or an invalid value when there were none.
  // For the commands that take any name, such as the settings of a module.
  const JsonValue* raw = nullptr;
};

// How the arguments of a request are written.
enum class ArgsForm : uint8_t {
  Json,   // {"channel":1,"value":true}
  Text    // channel=1 value=true name="two words"
};

using CmdHandler = Delegate<Status(const Args&, CmdContext&)>;
using DescribeFn = Delegate<void(JsonWriter&)>;

class CommandBus;

// Registration handle for the commands of one target.
class CommandSet {
 public:
  CommandSet(CommandBus& bus, uint8_t target) : bus_(bus), target_(target) {}

  template <size_t N>
  CommandSet& add(const char* name, const char* help, const ArgSpec (&args)[N],
                  CmdHandler handler, uint8_t flags = kCmdNone) {
    return add(name, help, Span<const ArgSpec>(args, N), handler, flags);
  }
  // Command without arguments.
  CommandSet& add(const char* name, const char* help, CmdHandler handler,
                  uint8_t flags = kCmdNone) {
    return add(name, help, Span<const ArgSpec>(), handler, flags);
  }
  CommandSet& add(const char* name, const char* help, Span<const ArgSpec> args,
                  CmdHandler handler, uint8_t flags = kCmdNone);

  // False when a registration failed: table full, too many arguments or a duplicate.
  bool ok() const { return ok_; }

 private:
  CommandBus& bus_;
  uint8_t target_;
  bool ok_ = true;
};

class CommandBus {
 public:
  static constexpr uint8_t kNoTarget = 0xFF;

  CommandBus() = default;

  // A target is written "<prefix>.<name>", for example "sys.net" or "mod.zappy2".
  // Both texts must stay valid. describe writes the members of the state document.
  Status addTarget(const char* prefix, const char* name, DescribeFn describe, uint8_t& id);
  // More members for the manifest of a target: what it is, the schema of its settings.
  void setAbout(uint8_t target, DescribeFn about);
  // Forgets the commands of a target, which can then declare them again: a module that
  // starts a second time.
  void clearCommands(uint8_t target);
  CommandSet commandsFor(uint8_t target) { return CommandSet(*this, target); }
  Status addCommand(uint8_t target, const char* name, const char* help,
                    Span<const ArgSpec> args, CmdHandler handler, uint8_t flags);

  uint8_t findTarget(StringView fullName) const;
  bool hasCommand(StringView target, StringView command) const;

  // Runs a command with arguments given as a JSON object. An invalid value means no arguments.
  Status execute(StringView target, StringView command, const JsonValue& args, CmdContext& ctx);
  // Arguments given as text: channel=1 value=true name="two words"
  Status executeText(StringView target, StringView command, StringView keyValues,
                     CmdContext& ctx);

  // Runs a command and writes the answer every interface gives back:
  //   {"result":{..},"ok":true,"status":"ok"}   plus "field" when a value was refused, and
  //   "ref" when the command ends later: the event CommandDone carries the same number.
  // Empty arguments mean none. Text that is not JSON, where JSON is expected, is Invalid.
  Status answer(StringView target, StringView command, StringView args, ArgsForm form,
                CmdSource source, JsonWriter& out);

  // State document of a target. The caller opens and closes the object.
  Status describe(StringView target, JsonWriter& w) const;
  // {"target":"sys.net","commands":[{"name":..,"help":..,"args":[..]}]}
  // withHelp false leaves the help texts out: a third of the size, for the small boards.
  Status manifest(StringView target, JsonWriter& w, bool withHelp = true) const;
  // ["sys.system","sys.log",...]
  void targets(JsonWriter& w) const;

  size_t targetCount() const { return targetCount_; }
  size_t commandCount() const { return commandCount_; }
  // Writes "<prefix>.<name>" of a target.
  void targetName(uint8_t target, char* out, size_t cap) const;

 private:
  struct Target {
    const char* prefix = "";
    const char* name = "";
    DescribeFn describe;
    DescribeFn about;
  };
  struct Command {
    const char* name = "";
    const char* help = "";
    const ArgSpec* args = nullptr;
    CmdHandler handler;
    uint8_t argCount = 0;
    uint8_t flags = 0;
    uint8_t target = kNoTarget;
  };

  const Command* findCommand(uint8_t target, StringView name) const;
  Status parse(const Command& cmd, const JsonValue& json, Args& out, CmdContext& ctx) const;

  Target targets_[LMP_MAX_TARGETS];
  Command commands_[LMP_MAX_COMMANDS];
  size_t targetCount_ = 0;
  size_t commandCount_ = 0;
  uint32_t lastRef_ = 0;
};

// The event that closes a command which answered Pending.
inline Event commandDone(uint32_t ref, Status status) {
  return Event(EventId::CommandDone, 0, static_cast<uint8_t>(status), ref);
}

}  // namespace lmp

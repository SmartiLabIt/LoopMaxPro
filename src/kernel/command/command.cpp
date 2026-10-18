#include "kernel/command/command.h"

#include <stdio.h>
#include <string.h>

namespace lmp {

const char* cmdSourceName(CmdSource source) {
  switch (source) {
    case CmdSource::Internal: return "internal";
    case CmdSource::Local:    return "local";
    case CmdSource::Remote:   return "remote";
    case CmdSource::Mqtt:     return "mqtt";
    case CmdSource::Serial:   return "serial";
  }
  return "?";
}

// ---------------------------------------------------------------- Args

const Args::Slot* Args::find(const char* name) const {
  if (!name) return nullptr;
  for (uint8_t i = 0; i < count_; ++i) {
    if (strcmp(slots_[i].name, name) == 0) return &slots_[i];
  }
  return nullptr;
}

bool Args::has(const char* name) const { return find(name) != nullptr; }

bool Args::boolean(const char* name, bool fallback) const {
  const Slot* s = find(name);
  return s ? s->integer != 0 : fallback;
}

int64_t Args::i64(const char* name, int64_t fallback) const {
  const Slot* s = find(name);
  if (!s || s->kind == FieldKind::Str) return fallback;
  return s->integer;
}

double Args::f64(const char* name, double fallback) const {
  const Slot* s = find(name);
  if (!s || s->kind == FieldKind::Str) return fallback;
  return s->number;
}

const char* Args::str(const char* name, const char* fallback) const {
  const Slot* s = find(name);
  return (s && s->text) ? s->text : fallback;
}

// ---------------------------------------------------------------- CommandSet

CommandSet& CommandSet::add(const char* name, const char* help, Span<const ArgSpec> args,
                            CmdHandler handler, uint8_t flags) {
  if (bus_.addCommand(target_, name, help, args, handler, flags) != Status::Ok) ok_ = false;
  return *this;
}

// ---------------------------------------------------------------- registration

Status CommandBus::addTarget(const char* prefix, const char* name, DescribeFn describe,
                             uint8_t& id) {
  id = kNoTarget;
  if (!prefix || !name || !*name) return Status::Invalid;
  for (size_t i = 0; i < targetCount_; ++i) {
    if (strcmp(targets_[i].prefix, prefix) == 0 && strcmp(targets_[i].name, name) == 0) {
      return Status::Busy;
    }
  }
  if (targetCount_ >= LMP_MAX_TARGETS) return Status::NoMem;
  Target& t = targets_[targetCount_];
  t.prefix = prefix;
  t.name = name;
  t.describe = describe;
  id = static_cast<uint8_t>(targetCount_++);
  return Status::Ok;
}

Status CommandBus::addCommand(uint8_t target, const char* name, const char* help,
                              Span<const ArgSpec> args, CmdHandler handler, uint8_t flags) {
  if (target >= targetCount_ || !name || !*name || !handler) return Status::Invalid;
  if (args.size() > LMP_CMD_MAX_ARGS) return Status::Invalid;
  if (findCommand(target, StringView(name))) return Status::Busy;
  if (commandCount_ >= LMP_MAX_COMMANDS) return Status::NoMem;
  Command& c = commands_[commandCount_++];
  c.name = name;
  c.help = help ? help : "";
  c.args = args.data();
  c.argCount = static_cast<uint8_t>(args.size());
  c.handler = handler;
  c.flags = flags;
  c.target = target;
  return Status::Ok;
}

// ---------------------------------------------------------------- lookup

uint8_t CommandBus::findTarget(StringView fullName) const {
  const size_t dot = fullName.find('.');
  if (dot == StringView::npos) return kNoTarget;
  const StringView prefix = fullName.substr(0, dot);
  const StringView name = fullName.substr(dot + 1);
  for (size_t i = 0; i < targetCount_; ++i) {
    if (prefix.equals(StringView(targets_[i].prefix)) && name.equals(StringView(targets_[i].name))) {
      return static_cast<uint8_t>(i);
    }
  }
  return kNoTarget;
}

const CommandBus::Command* CommandBus::findCommand(uint8_t target, StringView name) const {
  for (size_t i = 0; i < commandCount_; ++i) {
    const Command& c = commands_[i];
    if (c.target == target && name.equals(StringView(c.name))) return &c;
  }
  return nullptr;
}

bool CommandBus::hasCommand(StringView target, StringView command) const {
  const uint8_t t = findTarget(target);
  return t != kNoTarget && findCommand(t, command) != nullptr;
}

void CommandBus::targetName(uint8_t target, char* out, size_t cap) const {
  if (!out || cap == 0) return;
  if (target >= targetCount_) {
    out[0] = '\0';
    return;
  }
  snprintf(out, cap, "%s.%s", targets_[target].prefix, targets_[target].name);
}

// ---------------------------------------------------------------- execution

Status CommandBus::parse(const Command& cmd, const JsonValue& json, Args& out,
                         CmdContext& ctx) const {
  out.count_ = 0;
  out.used_ = 0;
  if (json.valid() && !json.isObject()) return Status::Invalid;

  for (uint8_t i = 0; i < cmd.argCount; ++i) {
    const ArgSpec& spec = cmd.args[i];
    const JsonValue v = json.valid() ? json.get(spec.name) : JsonValue();
    if (!v.valid() || v.isNull()) {
      if (spec.required) {
        ctx.errorField = spec.name;
        return Status::Invalid;
      }
      continue;
    }

    Args::Slot& slot = out.slots_[out.count_];
    slot = Args::Slot();
    slot.name = spec.name;
    slot.kind = spec.spec.kind;

    if (spec.spec.kind == FieldKind::Str) {
      const size_t room = sizeof(out.text_) - out.used_;
      double unused = 0;
      // The capacity is what is left in the shared text buffer.
      if (room < 2 || fieldValidate(v, spec.spec, room - 1, unused) != Status::Ok) {
        ctx.errorField = spec.name;
        return Status::Invalid;
      }
      char* dst = out.text_ + out.used_;
      if (!v.getString(dst, room)) {
        ctx.errorField = spec.name;
        return Status::Invalid;
      }
      slot.text = dst;
      out.used_ += strlen(dst) + 1;
    } else {
      double number = 0;
      if (fieldValidate(v, spec.spec, 0, number) != Status::Ok) {
        ctx.errorField = spec.name;
        return Status::Invalid;
      }
      slot.number = number;
      // Whole 64 bit values keep their exact value.
      slot.integer = spec.spec.kind == FieldKind::I64 ? v.asInt() : static_cast<int64_t>(number);
    }
    ++out.count_;
  }
  return Status::Ok;
}

Status CommandBus::execute(StringView target, StringView command, const JsonValue& args,
                           CmdContext& ctx) {
  ctx.errorField = nullptr;
  const uint8_t t = findTarget(target);
  if (t == kNoTarget) return Status::NotFound;
  const Command* cmd = findCommand(t, command);
  if (!cmd) return Status::NotFound;

  Args parsed;
  Status status = parse(*cmd, args, parsed, ctx);
  if (status == Status::Ok) {
    // A command that ends later gets a reference: the event of its end carries it.
    if ((cmd->flags & kCmdAsync) && ctx.ref == 0) {
      if (++lastRef_ == 0) lastRef_ = 1;
      ctx.ref = lastRef_;
    }
    ctx.raw = &args;
    status = cmd->handler(parsed, ctx);
    ctx.raw = nullptr;
  }

  // Arguments are never logged: they may contain secrets.
  if (Logger* log = Logger::instance()) {
    const LogSource src = log->addSource("cmd");
    const bool failed = status != Status::Ok && status != Status::Pending;
    if (ctx.errorField) {
      log->write(LogLevel::Warn, src, "%s.%s %s from %s: %s (%s)", targets_[t].prefix,
                 targets_[t].name, cmd->name, cmdSourceName(ctx.source), statusName(status),
                 ctx.errorField);
    } else {
      log->write(failed ? LogLevel::Warn : LogLevel::Info, src, "%s.%s %s from %s: %s",
                 targets_[t].prefix, targets_[t].name, cmd->name, cmdSourceName(ctx.source),
                 statusName(status));
    }
  }
  return status;
}

namespace {

inline bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

const ArgSpec* findSpec(Span<const ArgSpec> specs, StringView name) {
  for (const ArgSpec& s : specs) {
    if (name.equals(StringView(s.name))) return &s;
  }
  return nullptr;
}

}  // namespace

Status CommandBus::executeText(StringView target, StringView command, StringView keyValues,
                               CmdContext& ctx) {
  ctx.errorField = nullptr;
  const uint8_t t = findTarget(target);
  if (t == kNoTarget) return Status::NotFound;
  const Command* cmd = findCommand(t, command);
  if (!cmd) return Status::NotFound;
  const Span<const ArgSpec> specs(cmd->args, cmd->argCount);

  // The text becomes a JSON object, then follows the same path of every other request.
  char buffer[LMP_CMD_TEXT + 128];
  BufferSink sink(buffer, sizeof(buffer));
  JsonWriter w(sink);
  w.beginObject();

  const char* p = keyValues.begin();
  const char* e = keyValues.end();
  while (p < e) {
    while (p < e && isSpace(*p)) ++p;
    if (p >= e) break;
    const char* keyBegin = p;
    while (p < e && *p != '=' && !isSpace(*p)) ++p;
    const StringView key(keyBegin, static_cast<size_t>(p - keyBegin));
    if (p >= e || *p != '=' || key.empty()) return Status::Invalid;
    ++p;

    const char* valueBegin = p;
    const char* valueEnd = p;
    if (p < e && *p == '"') {
      valueBegin = ++p;
      while (p < e && *p != '"') ++p;
      if (p >= e) return Status::Invalid;
      valueEnd = p++;
    } else {
      while (p < e && !isSpace(*p)) ++p;
      valueEnd = p;
    }
    const StringView value(valueBegin, static_cast<size_t>(valueEnd - valueBegin));

    const ArgSpec* spec = findSpec(specs, key);
    if (!spec && (cmd->flags & kCmdAnyArgs)) {
      // Any name is taken. What reads as a number, true or false is one; the rest is text.
      char name[32];
      if (key.size >= sizeof(name)) return Status::Invalid;
      key.copyTo(name, sizeof(name));
      w.key(name);
      const bool quoted = valueBegin > keyValues.begin() && valueBegin[-1] == '"';
      const JsonValue plain = quoted ? JsonValue() : JsonValue::parse(value);
      if (plain.isNumber() || plain.isBool()) {
        w.raw(value);
      } else {
        w.str(value);
      }
      continue;
    }
    if (!spec) continue;  // unknown names are ignored, as in JSON requests
    w.key(spec->name);
    if (spec->spec.kind == FieldKind::Str) {
      w.str(value);
    } else if (spec->spec.kind == FieldKind::Bool &&
               (value == StringView("on") || value == StringView("yes"))) {
      w.boolean(true);
    } else if (spec->spec.kind == FieldKind::Bool &&
               (value == StringView("off") || value == StringView("no"))) {
      w.boolean(false);
    } else if (JsonValue::parse(value).valid()) {
      w.raw(value);
    } else {
      ctx.errorField = spec->name;
      return Status::Invalid;
    }
  }
  w.endObject();
  if (!w.complete()) return Status::NoMem;

  return execute(target, command, JsonValue::parse(sink.view()), ctx);
}

Status CommandBus::answer(StringView target, StringView command, StringView args, ArgsForm form,
                          CmdSource source, JsonWriter& out) {
  CmdContext ctx;
  ctx.source = source;
  ctx.result = &out;

  out.beginObject();
  out.beginObject("result");
  Status status;
  if (form == ArgsForm::Text) {
    status = executeText(target, command, args, ctx);
  } else {
    const char* p = args.begin();
    while (p < args.end() && isSpace(*p)) ++p;
    const JsonValue json = p < args.end() ? JsonValue::parse(args) : JsonValue();
    // Something was sent and it is not JSON: running the command without arguments would
    // hide the mistake.
    status = (p < args.end() && !json.valid()) ? Status::Invalid
                                               : execute(target, command, json, ctx);
  }
  out.endObject();
  out.boolean("ok", status == Status::Ok || status == Status::Pending);
  out.str("status", statusName(status));
  if (ctx.errorField) out.str("field", ctx.errorField);
  if (status == Status::Pending && ctx.ref) out.u32("ref", ctx.ref);
  out.endObject();
  return status;
}

// ---------------------------------------------------------------- documents

Status CommandBus::describe(StringView target, JsonWriter& w) const {
  const uint8_t t = findTarget(target);
  if (t == kNoTarget) return Status::NotFound;
  if (targets_[t].describe) targets_[t].describe(w);
  return Status::Ok;
}

Status CommandBus::manifest(StringView target, JsonWriter& w, bool withHelp) const {
  const uint8_t t = findTarget(target);
  if (t == kNoTarget) return Status::NotFound;

  char name[48];
  targetName(t, name, sizeof(name));
  w.beginObject();
  w.str("target", name);
  w.beginArray("commands");
  for (size_t i = 0; i < commandCount_; ++i) {
    const Command& c = commands_[i];
    if (c.target != t) continue;
    w.beginObject();
    w.str("name", c.name);
    if (withHelp && c.help[0]) w.str("help", c.help);
    if (c.flags & kCmdConfirm) w.boolean("confirm", true);
    if (c.flags & kCmdAsync) w.boolean("async", true);
    if (c.flags & kCmdAnyArgs) w.boolean("anyArgs", true);
    if (c.flags & kCmdReadOnly) w.boolean("readOnly", true);
    if (c.flags & kCmdSensitive) w.boolean("sensitive", true);
    if (c.flags & kCmdSecret) w.boolean("secret", true);
    if (c.flags & kCmdUi) w.boolean("ui", true);
    if (c.flags & kCmdEveryday) w.boolean("everyday", true);
    w.beginArray("args");
    for (uint8_t a = 0; a < c.argCount; ++a) {
      const ArgSpec& spec = c.args[a];
      w.beginObject();
      w.str("name", spec.name);
      fieldSpecDescribe(w, spec.spec, LMP_CMD_TEXT - 1);
      w.boolean("required", spec.required);
      w.endObject();
    }
    w.endArray();
    w.endObject();
  }
  w.endArray();
  if (targets_[t].about) targets_[t].about(w);
  w.endObject();
  return Status::Ok;
}

void CommandBus::setAbout(uint8_t target, DescribeFn about) {
  if (target < targetCount_) targets_[target].about = about;
}

void CommandBus::clearCommands(uint8_t target) {
  size_t kept = 0;
  for (size_t i = 0; i < commandCount_; ++i) {
    if (commands_[i].target == target) continue;
    if (kept != i) commands_[kept] = commands_[i];
    ++kept;
  }
  for (size_t i = kept; i < commandCount_; ++i) commands_[i] = Command();
  commandCount_ = kept;
}

void CommandBus::targets(JsonWriter& w) const {
  w.beginArray();
  for (size_t i = 0; i < targetCount_; ++i) {
    char name[48];
    targetName(static_cast<uint8_t>(i), name, sizeof(name));
    w.str(name);
  }
  w.endArray();
}

}  // namespace lmp

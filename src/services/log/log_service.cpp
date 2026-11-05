#include "services/log/log_service.h"

#include <string.h>

namespace lmp {
namespace {

constexpr ArgSpec kLevelArgs[] = {
    Arg::str("level", 1, 8),
    Arg::str("source", 1, 16).optional(),
};

}  // namespace

const ServiceInfo& LogService::info() const {
  static const ServiceInfo kInfo{ServiceId::Log, "log", "\xE2\x9C\x8F", Mode::Any,
                                 RestartPolicy::Critical, 0, DepList(ServiceId::System)};
  return kInfo;
}

Status LogService::start(Kernel& kernel) {
  kernel_ = &kernel;
  return Status::Ok;
}

void LogService::describe(JsonWriter& w) const {
  if (!kernel_) return;
  Logger& log = kernel_->log();
  w.u32("count", static_cast<uint32_t>(log.count()));
  w.u32("capacity", static_cast<uint32_t>(kernel_->log().capacity()));
  w.u32("dropped", log.dropped());
  w.u32("seqLast", log.lastSeq());
  w.u32("bootDoneMs", log.bootDoneMs());
  w.str("level", logLevelName(log.level(kLogSys)));
  w.beginArray("sources");
  for (size_t i = 0; i < log.sourceCount(); ++i) {
    const LogSource id = static_cast<LogSource>(i);
    w.beginObject();
    w.str("name", log.sourceName(id));
    w.str("icon", log.sourceIcon(id));
    w.str("level", logLevelName(log.level(id)));
    w.endObject();
  }
  w.endArray();
}

void LogService::commands(CommandSet& set) {
  set.add("clear",
          LMP_HELP("Deletes the stored entries"), [this](const Args&, CmdContext&) -> Status {
    if (!kernel_) return Status::Error;
    kernel_->log().clear();
    return Status::Ok;
  });

  set.add("setLevel", LMP_HELP("Sets the level of every source, or of one source"), kLevelArgs,
          [this](const Args& a, CmdContext& ctx) -> Status {
            if (!kernel_) return Status::Error;
            Logger& log = kernel_->log();
            LogLevel level;
            if (!logLevelParse(a.str("level"), level)) {
              ctx.errorField = "level";
              return Status::Invalid;
            }
            if (!a.has("source")) {
              log.setLevel(level);
              return Status::Ok;
            }
            LogSource source = kLogSys;
            if (!log.findSource(a.str("source"), source)) {
              ctx.errorField = "source";
              return Status::NotFound;
            }
            log.setLevel(source, level);
            return Status::Ok;
          });
}

void LogService::writeEntries(JsonWriter& w, const Query& query) const {
  if (!kernel_) return;
  Logger& log = kernel_->log();

  LogSource wanted = kLogSys;
  const bool filterSource = query.source && *query.source;
  const bool sourceKnown = !filterSource || log.findSource(query.source, wanted);

  w.beginObject();
  w.u32("seqLast", log.lastSeq());
  w.u32("bootDoneMs", log.bootDoneMs());
  w.u32("dropped", log.dropped());
  w.beginArray("items");
  if (sourceKnown) {
    // Small blocks: the entries never sit on the stack all together.
    LogEntry block[4];
    uint32_t since = query.since;
    uint16_t written = 0;
    while (written < query.max) {
      const size_t n = log.read(since, block, sizeof(block) / sizeof(block[0]));
      if (n == 0) break;
      for (size_t i = 0; i < n && written < query.max; ++i) {
        const LogEntry& e = block[i];
        since = e.seq;
        if (e.level > query.level) continue;
        if (filterSource && e.source != wanted) continue;
        w.beginObject();
        w.u32("seq", e.seq);
        w.u32("ms", e.ms);
        w.str("lvl", logLevelName(e.level));
        w.str("src", log.sourceName(e.source));
        w.str("msg", e.msg);
        if (e.truncated) w.boolean("cut", true);
        w.endObject();
        ++written;
      }
    }
  }
  w.endArray();
  w.endObject();
}

}  // namespace lmp

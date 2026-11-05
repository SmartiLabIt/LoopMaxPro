// LoopMaxPro - EspRadio: a web radio in the console of the device.
// The sound is of the browser that looks at the page: there the stations are looked for in more
// than one directory, played and equalized. The device keeps what must be the same from every
// phone and PC, and from the remote console: the favourite stations with their categories, and a
// few settings. Nothing here asks the network. Its interface is ui/espradio.js.
#include "lmp_config.h"

#include <stdio.h>
#include <string.h>

#include "hal/api/memory.h"
#include "kernel/kernel.h"
#include "sdk/module.h"
#include "sdk/state.h"
#include "services/storage/storage_service.h"

namespace espradio {

using lmp::Status;

struct Cfg {
  lmp::FixedString<160> cats;    // the categories of the favourites in their order, a comma between
  lmp::FixedString<3> country;   // the country the search starts from; empty: the page chooses
  uint8_t scanSec;               // seconds on each station while scanning
};

}  // namespace espradio

LMP_FIELDS(espradio::Cfg,
  LMP_F(cats,    "cats",    Str(0, 159, "").technical()),
  LMP_F(country, "country", Str(0, 2, "")),
  LMP_F(scanSec, "scanSec", U8(3, 60, 8).unit("s")))

namespace espradio {

constexpr const char* kFile = "/espradio/favs.jsonl";
// A favourite on file is a line of JSON, with short names:
//   {"i":id,"n":name,"u":address,"c":category,"l":logo,"t":info}
constexpr size_t kLineMax = 480;
constexpr size_t kPage = 2800;   // the characters of a page of the list
// How many favourites the board keeps: the whole file is in memory while a command reads or
// changes it, and only then.
#if LMP_MODULE_MEM >= 3
constexpr uint16_t kFavMax = 200;
#elif LMP_MODULE_MEM == 2
constexpr uint16_t kFavMax = 80;
#else
constexpr uint16_t kFavMax = 30;
#endif
constexpr size_t kFileCap = static_cast<size_t>(kFavMax) * kLineMax + 1;

// A command carries at most LMP_CMD_TEXT characters of text: a new favourite arrives in two
// commands, name and address first, then logo and info.
const lmp::ArgSpec kSetArgs[] = {
    lmp::Arg::str("id", 2, 23),
    lmp::Arg::str("name", 1, 43).optional(),
    lmp::Arg::str("url", 8, 159).optional(),
    lmp::Arg::str("cat", 0, 19).optional(),
    lmp::Arg::str("logo", 0, 119).optional(),
    lmp::Arg::str("info", 0, 59).optional(),
};
const lmp::ArgSpec kIdArgs[] = {lmp::Arg::str("id", 2, 23)};
const lmp::ArgSpec kFromArgs[] = {lmp::Arg::u16("from", 0, 999).optional()};
const lmp::ArgSpec kRenameArgs[] = {lmp::Arg::str("from", 1, 19), lmp::Arg::str("to", 0, 19)};

struct Fav {
  char id[24] = "", name[44] = "", url[160] = "", cat[20] = "", logo[120] = "", info[60] = "";
};

void copyText(char* out, size_t cap, const char* text) { snprintf(out, cap, "%s", text ? text : ""); }

// Letters, digits and . _ : - only: an id is looked for on file as it is written.
bool goodId(const char* id) {
  for (const char* p = id; *p; ++p) {
    const char c = *p;
    const bool good = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                      c == '.' || c == '_' || c == ':' || c == '-';
    if (!good) return false;
  }
  return id[0] != '\0';
}

// A favourite as its line on file, with the end of line. 0 when it does not fit.
size_t lineOf(const Fav& f, char* out, size_t cap) {
  lmp::BufferSink sink(out, cap - 1);
  lmp::JsonWriter w(sink);
  w.beginObject();
  w.str("i", f.id);
  w.str("n", f.name);
  w.str("u", f.url);
  if (f.cat[0]) w.str("c", f.cat);
  if (f.logo[0]) w.str("l", f.logo);
  if (f.info[0]) w.str("t", f.info);
  w.endObject();
  if (!w.complete()) return 0;
  size_t n = sink.view().size;
  out[n++] = '\n';
  return n;
}

void favOf(const char* line, size_t size, Fav& f) {
  const lmp::JsonValue v = lmp::JsonValue::parse(lmp::StringView(line, size));
  v["i"].getString(f.id, sizeof(f.id));
  v["n"].getString(f.name, sizeof(f.name));
  v["u"].getString(f.url, sizeof(f.url));
  v["c"].getString(f.cat, sizeof(f.cat));
  v["l"].getString(f.logo, sizeof(f.logo));
  v["t"].getString(f.info, sizeof(f.info));
}

// The file of the favourites in memory, for the time of one command.
struct Book {
  char* text = nullptr;
  size_t len = 0;

  ~Book() {
    if (text) lmp::hal::freeLarge(text);
  }

  Status open(lmp::IStorage* storage) {
    if (!storage) return Status::Unsupported;
    text = static_cast<char*>(lmp::hal::allocLarge(kFileCap));
    if (!text) return Status::NoMem;
    if (!storage->exists(kFile)) return Status::Ok;
    const Status s = storage->readFile(kFile, text, kFileCap, len);
    if (s != Status::Ok) len = 0;
    return s;
  }

  // The size of the line at p, with its end of line.
  size_t lineAt(size_t p) const {
    const void* end = memchr(text + p, '\n', len - p);
    return end ? static_cast<size_t>(static_cast<const char*>(end) - (text + p)) + 1 : len - p;
  }

  uint16_t count() const {
    uint16_t n = 0;
    for (size_t p = 0; p < len; p += lineAt(p)) ++n;
    return n;
  }

  // Where the favourite with this id is, and how long its line is. False when it is not there.
  bool find(const char* id, size_t& at, size_t& size) const {
    char head[40];
    const size_t n = static_cast<size_t>(snprintf(head, sizeof(head), "{\"i\":\"%s\"", id));
    for (size_t p = 0; p < len;) {
      const size_t line = lineAt(p);
      if (line > n && memcmp(text + p, head, n) == 0) {
        at = p;
        size = line;
        return true;
      }
      p += line;
    }
    return false;
  }

  // Puts a line in the place of size characters at at: a new one at the end with size 0, none
  // in the place of one with lineLen 0. False when the file would not fit.
  bool put(size_t at, size_t size, const char* line, size_t lineLen) {
    if (len - size + lineLen >= kFileCap) return false;
    memmove(text + at + lineLen, text + at + size, len - at - size);
    if (lineLen) memcpy(text + at, line, lineLen);
    len = len - size + lineLen;
    return true;
  }
};

class EspRadio;

// The settings: the page shows them from the document, which is told when they change.
class Settings final : public lmp::sdk::ModuleConfig<Cfg> {
 public:
  EspRadio* owner = nullptr;

 protected:
  void applied() override;
};

class EspRadio final : public lmp::sdk::Module {
  LMP_MODULE("espradio", "EspRadio", "\xF0\x9F\x93\xBB", "1.0.5", lmp::sdk::ModuleKind::Online, 0)

 public:
  Status onInit(lmp::sdk::ModuleContext& c) override {
    cfg_.owner = this;
    LMP_TRY(useSettings(cfg_));
    c.commands->add("favs", LMP_HELP("The favourite stations, a page at a time"), kFromArgs,
                    [this](const lmp::Args& a, lmp::CmdContext& ctx) -> Status {
                      return list(a.has("from") ? a.u16("from") : 0, ctx);
                    },
                    lmp::kCmdReadOnly);
    c.commands->add("fav.set", LMP_HELP("A favourite station, new or changed: name, address, category, logo, info"),
                    kSetArgs,
                    [this](const lmp::Args& a, lmp::CmdContext& ctx) -> Status { return set(a, ctx); });
    c.commands->add("fav.remove", LMP_HELP("Forgets a favourite station"), kIdArgs,
                    [this](const lmp::Args& a, lmp::CmdContext&) -> Status { return remove(a.str("id")); });
    c.commands->add("cat.rename", LMP_HELP("Gives the favourites of a category another one, or none"),
                    kRenameArgs,
                    [this](const lmp::Args& a, lmp::CmdContext&) -> Status {
                      return rename(a.str("from"), a.str("to"));
                    });
    Book book;
    count_ = book.open(ctx().storage) == Status::Ok ? book.count() : 0;
    return Status::Ok;
  }

  void describe(lmp::JsonWriter& w) const override {
    w.u32("favs", count_);
    w.u32("max", kFavMax);
    w.str("cats", cfg_->cats.c_str());
    w.str("country", cfg_->country.c_str());
    w.u32("scanSec", cfg_->scanSec);
  }

  void onSettings() { markChanged(); }

  // What a home system reads of the module (sdk/home.h): how many stations it keeps. The
  // sound is of the browser that looks at the page: here there is nothing to turn on or off.
  void things(lmp::sdk::ThingSink& out) const override {
    out.range(0, kFavMax);
    out.number("favs", {"Favourite stations", "Stazioni preferite"}, count_);
  }

 private:
  Status list(uint16_t from, lmp::CmdContext& ctx) {
    if (!ctx.result) return Status::Ok;
    Book book;
    LMP_TRY(book.open(this->ctx().storage));
    lmp::JsonWriter& w = *ctx.result;
    w.u32("count", book.count());
    w.u32("max", kFavMax);
    w.beginArray("favs");
    uint16_t i = 0;
    size_t used = 0;
    size_t p = 0;
    for (; p < book.len; ++i) {
      const size_t line = book.lineAt(p);
      if (i >= from) {
        if (used && used + line > kPage) break;
        // A line is JSON as it is, less its end of line.
        w.raw(lmp::StringView(book.text + p, book.text[p + line - 1] == '\n' ? line - 1 : line));
        used += line;
      }
      p += line;
    }
    w.endArray();
    if (p < book.len) w.u32("next", i);
    return Status::Ok;
  }

  Status set(const lmp::Args& a, lmp::CmdContext& ctx) {
    const char* id = a.str("id");
    if (!goodId(id)) {
      ctx.errorField = "id";
      return Status::Invalid;
    }
    Book book;
    LMP_TRY(book.open(this->ctx().storage));
    Fav f;
    size_t at = book.len, size = 0;
    const bool known = book.find(id, at, size);
    if (known) {
      favOf(book.text + at, size, f);
    } else {
      // A new one: with its name and its address, and room for it.
      if (!a.has("name") || !a.has("url")) {
        ctx.errorField = a.has("name") ? "url" : "name";
        return Status::Invalid;
      }
      if (book.count() >= kFavMax) return Status::NoMem;
      copyText(f.id, sizeof(f.id), id);
    }
    if (a.has("name")) copyText(f.name, sizeof(f.name), a.str("name"));
    if (a.has("url")) copyText(f.url, sizeof(f.url), a.str("url"));
    if (a.has("cat")) copyText(f.cat, sizeof(f.cat), a.str("cat"));
    if (a.has("logo")) copyText(f.logo, sizeof(f.logo), a.str("logo"));
    if (a.has("info")) copyText(f.info, sizeof(f.info), a.str("info"));
    if (strncmp(f.url, "http://", 7) != 0 && strncmp(f.url, "https://", 8) != 0) {
      ctx.errorField = "url";
      return Status::Invalid;
    }
    char line[kLineMax];
    const size_t n = lineOf(f, line, sizeof(line));
    if (!n) return Status::Invalid;
    if (!book.put(at, size, line, n)) return Status::NoMem;
    return write(book);
  }

  Status remove(const char* id) {
    Book book;
    LMP_TRY(book.open(ctx().storage));
    size_t at = 0, size = 0;
    if (!book.find(id, at, size)) return Status::NotFound;
    book.put(at, size, nullptr, 0);
    return write(book);
  }

  Status rename(const char* from, const char* to) {
    Book book;
    LMP_TRY(book.open(ctx().storage));
    bool changed = false;
    char line[kLineMax];
    for (size_t p = 0; p < book.len;) {
      size_t size = book.lineAt(p);
      Fav f;
      favOf(book.text + p, size, f);
      if (strcmp(f.cat, from) == 0) {
        copyText(f.cat, sizeof(f.cat), to);
        const size_t n = lineOf(f, line, sizeof(line));
        if (!n || !book.put(p, size, line, n)) return Status::NoMem;
        size = n;
        changed = true;
      }
      p += size;
    }
    return changed ? write(book) : Status::Ok;
  }

  Status write(const Book& book) {
    const Status s = ctx().storage->writeFile(kFile, lmp::StringView(book.text, book.len));
    if (s != Status::Ok) {
      LMP_LOGE(ctx().log, "favourites not written: %s", lmp::statusName(s));
      return s;
    }
    count_ = book.count();
    markChanged();
    return Status::Ok;
  }

  Settings cfg_;
  uint16_t count_ = 0;
};

void Settings::applied() {
  if (owner) owner->onSettings();
}

}  // namespace espradio

LMP_REGISTER_MODULE(espradio, espradio::EspRadio)

#include "sdk/things.h"

#if LMP_FEATURE_THINGS

#include <stdio.h>
#include <string.h>

#include <new>

namespace lmp::sdk {

namespace {

// Texts and numbers into one number: enough to tell that something is no longer what it was.
constexpr uint32_t kSeed = 2166136261u;

uint32_t mixText(uint32_t h, const char* text) {
  for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text ? text : ""); *p; ++p) {
    h = (h ^ *p) * 16777619u;
  }
  return (h ^ 0x1Fu) * 16777619u;   // where a text ends: "ab" and "c" are not "a" and "bc"
}

uint32_t mixNumber(uint32_t h, uint32_t number) {
  for (int i = 0; i < 4; ++i) {
    h = (h ^ (number & 0xFFu)) * 16777619u;
    number >>= 8;
  }
  return h;
}

uint16_t fold(uint32_t h) { return static_cast<uint16_t>(h ^ (h >> 16)); }

// A number as it is shown: a change below what is shown is no change.
void numberText(char* out, size_t cap, double value, uint8_t decimals, bool known) {
  if (!known) {
    snprintf(out, cap, "?");
    return;
  }
  snprintf(out, cap, "%.*f", static_cast<int>(decimals > 9 ? 9 : decimals), value);
}

// What a module said for its next thing: its limits, and that it can be set.
struct Next {
  bool range = false;
  bool settable = false;
  double min = 0, max = 0, step = 0;

  void limits(double low, double high) {
    range = true;
    min = low;
    max = high;
  }
  void set(double low, double high, double by) {
    settable = true;
    min = low;
    max = high;
    step = by;
  }
  // For one thing only: taken, it is no longer there.
  Next take() {
    const Next was = *this;
    *this = Next();
    return was;
  }
};

// The longest text a thing takes: what the module said, never more than the SDK carries.
uint32_t textMax(const Next& next) {
  return next.max > 0 && next.max < kThingTextMax ? static_cast<uint32_t>(next.max)
                                                  : static_cast<uint32_t>(kThingTextMax);
}

// A time of measure as it is kept: 0 for one that is not known.
uint32_t stamp(int64_t unixSeconds) {
  return unixSeconds > 0 && unixSeconds < 0xFFFFFFFFLL ? static_cast<uint32_t>(unixSeconds) : 0;
}

}  // namespace

uint32_t thingKey(const char* part, const char* id) { return mixText(mixText(kSeed, part), id); }

// ---------------------------------------------------------------- the look

// Walks the things of a module and tells the book what they are now.
class ThingsLook final : public ThingSink {
 public:
  ThingsLook(ThingsBook& book, uint32_t rev, ThingMeasures* measures, uint32_t since)
      : book_(book), rev_(rev), measures_(measures), since_(since) {}

  void part(const char* id, const char* name) override {
    endPart();
    in_ = id && *id;
    snprintf(partText_, sizeof(partText_), "%s", in_ ? id : "");
    if (!in_) return;
    ++parts_;
    partId_ = mixText(kSeed, id);
    // The parts are part of the description: one more, one less, another name.
    desc_ = mixText(mixText(desc_, id), name);
    partValues_ = kSeed;
  }

  void technical() override { technical_ = true; }
  void range(double min, double max) override { next_.limits(min, max); }
  void settable(double min, double max, double step) override { next_.set(min, max, step); }
  void measured(int64_t unixSeconds) override { at_ = stamp(unixSeconds); }

  void toggle(const char* id, ThingName name, bool on, const char* kind) override {
    thing("toggle", id, name, nullptr, kind, 0);
    value(id, on ? "1" : "0");
  }

  void light(const char* id, ThingName name, bool on, uint8_t brightness, uint8_t r, uint8_t g,
             uint8_t b, bool colours) override {
    thing("light", id, name, nullptr, colours ? "rgb" : "brightness", 0);
    char text[32];
    snprintf(text, sizeof(text), "%d %u %u %u %u", on ? 1 : 0, static_cast<unsigned>(brightness),
             static_cast<unsigned>(colours ? r : 0), static_cast<unsigned>(colours ? g : 0),
             static_cast<unsigned>(colours ? b : 0));
    value(id, text);
  }

  void number(const char* id, ThingName name, double number, const char* unit, const char* kind,
              uint8_t decimals, bool known) override {
    const uint32_t at = at_;
    thing("number", id, name, unit, kind, decimals);
    char text[40];
    numberText(text, sizeof(text), number, decimals, known);
    value(id, text);
    if (!at) return;
    // When it was measured is part of what it is now: measured again, it is told again.
    uint32_t& h = in_ ? partValues_ : ownValues_;
    h = mixNumber(h, at);
    if (at > atMax_) atMax_ = at;
    if (measures_ && known && at > since_) measures_->measure(thingKey(partText_, id), number, at);
  }

  void flag(const char* id, ThingName name, bool on, const char* kind) override {
    thing("flag", id, name, nullptr, kind, 0);
    value(id, on ? "1" : "0");
  }

  void text(const char* id, ThingName name, const char* word) override {
    thing("text", id, name, nullptr, nullptr, 0);
    value(id, word);
  }

  void button(const char* id, ThingName name) override {
    thing("button", id, name, nullptr, nullptr, 0);
  }

  void choice(const char* id, ThingName name, const char* now, const ThingOption* options,
              uint8_t count) override {
    thing("select", id, name, nullptr, nullptr, 0);
    // The values it takes are part of what it is.
    for (uint8_t i = 0; i < count; ++i) {
      desc_ = mixText(mixText(mixText(desc_, options[i].id), options[i].name.en),
                      options[i].name.it);
    }
    value(id, now);
  }

  // After the last thing. True when a part is new or changed.
  bool end() {
    endPart();
    return changed_;
  }

  uint32_t desc() const { return desc_; }
  uint32_t ownValues() const { return ownValues_; }
  uint16_t own() const { return own_; }
  uint16_t parts() const { return parts_; }
  uint32_t atMax() const { return atMax_; }

 private:
  void thing(const char* kind, const char* id, ThingName name, const char* unit, const char* type,
             uint8_t decimals) {
    at_ = 0;   // said for one number only
    if (!in_) ++own_;
    uint32_t h = mixText(mixText(desc_, kind), id);
    h = mixText(mixText(h, name.en), name.it);
    h = mixText(mixText(h, unit), type);
    desc_ = mixNumber(h, static_cast<uint32_t>(decimals) | (technical_ ? 0x100u : 0u) |
                             (in_ ? 0x200u : 0u));
    technical_ = false;
    // Limits, and that it can be set: only a thing that says them changes the number.
    const Next next = next_.take();
    if (next.range || next.settable) {
      char text[96];
      snprintf(text, sizeof(text), "%c %.9g %.9g %.9g", next.settable ? 's' : 'r', next.min,
               next.max, next.step);
      desc_ = mixText(desc_, text);
    }
  }

  void value(const char* id, const char* text) {
    uint32_t& h = in_ ? partValues_ : ownValues_;
    h = mixText(mixText(h, id), text);
  }

  void endPart() {
    if (!in_) return;
    in_ = false;
    if (book_.take(partId_, fold(partValues_), rev_)) changed_ = true;
  }

  ThingsBook& book_;
  uint32_t rev_;
  uint32_t desc_ = kSeed;
  uint32_t ownValues_ = kSeed;
  uint32_t partValues_ = kSeed;
  uint32_t partId_ = 0;
  uint16_t own_ = 0;
  uint16_t parts_ = 0;
  bool in_ = false;
  bool technical_ = false;
  bool changed_ = false;
  Next next_;
  ThingMeasures* measures_;
  uint32_t since_;
  uint32_t at_ = 0;
  uint32_t atMax_ = 0;
  char partText_[24] = "";
};

bool ThingsBook::take(uint32_t id, uint16_t values, uint32_t rev) {
  for (uint16_t i = 0; i < used_; ++i) {
    Part& p = table_[i];
    if (p.id != id) continue;
    p.seen = true;
    if (p.values == values) return false;
    p.values = values;
    p.rev = rev;
    return true;
  }
  if (!table_) table_ = new (std::nothrow) Part[LMP_THINGS_PARTS];
  if (!table_ || used_ >= LMP_THINGS_PARTS) {
    // No room for this one: its values are followed together with the others left out, and
    // who asks takes it as changed every time.
    over_ = static_cast<uint16_t>(over_ * 31u + values + 1u);
    return false;
  }
  table_[used_++] = Part{id, rev, values, true};
  return true;
}

bool ThingsBook::look(const Module& module, ThingMeasures* measures) {
  for (uint16_t i = 0; i < used_; ++i) table_[i].seen = false;
  const uint16_t overBefore = over_;
  over_ = 0;
  ThingsLook now(*this, rev_ + 1, based_ ? measures : nullptr, measuredUpTo_);
  module.thingsAll(now);
  bool changed = now.end();
  if (now.atMax() > measuredUpTo_) measuredUpTo_ = now.atMax();
  based_ = true;

  // The parts that are gone leave their place.
  uint16_t kept = 0;
  for (uint16_t i = 0; i < used_; ++i) {
    if (!table_[i].seen) continue;
    if (kept != i) table_[kept] = table_[i];
    ++kept;
  }
  used_ = kept;

  if (now.own() == 0 && now.parts() == 0) {
    const bool had = any();
    gen_ = 0;
    own_ = parts_ = 0;
    ownValues_ = over_ = 0;
    if (had) ++rev_;
    return had;
  }
  uint16_t gen = fold(now.desc());
  if (gen == 0) gen = 1;   // 0 is "no things"
  const uint16_t ownValues = fold(now.ownValues());
  if (gen != gen_ || ownValues != ownValues_ || over_ != overBefore) changed = true;
  gen_ = gen;
  own_ = now.own();
  parts_ = now.parts();
  ownValues_ = ownValues;
  if (changed) ++rev_;
  return changed;
}

void ThingsBook::take(ThingsBook& other) {
  gen_ = other.gen_;
  rev_ = other.rev_;
  own_ = other.own_;
  ownValues_ = other.ownValues_;
  parts_ = other.parts_;
  over_ = other.over_;
  table_ = other.table_;
  used_ = other.used_;
  measuredUpTo_ = other.measuredUpTo_;
  based_ = other.based_;
  other.table_ = nullptr;
  other.used_ = 0;
}

void ThingsBook::forget() {
  delete[] table_;
  table_ = nullptr;
  used_ = 0;
  gen_ = 0;
  own_ = parts_ = 0;
  ownValues_ = over_ = 0;
  // rev stays: who kept a copy sees that what comes next is not what it has.
  // The measures start from the next look: what the module tells when it is back is where
  // they are, not a measure made while it was stopped.
  based_ = false;
}

uint32_t ThingsBook::changedAt(const char* part) const {
  const uint32_t id = mixText(kSeed, part);
  for (uint16_t i = 0; i < used_; ++i) {
    if (table_[i].id == id) return table_[i].rev;
  }
  return rev_;
}

// ---------------------------------------------------------------- what they are

namespace {

// The things of the module itself, or of its first part: every part has the same.
class DescribeSink final : public ThingSink {
 public:
  DescribeSink(JsonWriter& w, bool ofPart) : w_(w), ofPart_(ofPart) {}

  void part(const char* id, const char*) override {
    in_ = id && *id;
    if (in_) ++partNo_;
  }
  void technical() override { technical_ = true; }
  void range(double min, double max) override { next_.limits(min, max); }
  void settable(double min, double max, double step) override { next_.set(min, max, step); }
  void toggle(const char* id, ThingName name, bool, const char* kind) override {
    thing(Is::Other, "toggle", id, name, nullptr, kind, 0);
  }
  void light(const char* id, ThingName name, bool, uint8_t, uint8_t, uint8_t, uint8_t,
             bool colours) override {
    thing(Is::Other, "light", id, name, nullptr, colours ? "rgb" : "brightness", 0);
  }
  void number(const char* id, ThingName name, double, const char* unit, const char* kind,
              uint8_t decimals, bool) override {
    thing(Is::Number, "number", id, name, unit, kind, decimals);
  }
  void flag(const char* id, ThingName name, bool, const char* kind) override {
    thing(Is::Other, "flag", id, name, nullptr, kind, 0);
  }
  void text(const char* id, ThingName name, const char*) override {
    thing(Is::Text, "text", id, name, nullptr, nullptr, 0);
  }
  void button(const char* id, ThingName name) override {
    thing(Is::Other, "button", id, name, nullptr, nullptr, 0);
  }
  void choice(const char* id, ThingName name, const char*, const ThingOption* options,
              uint8_t count) override {
    thing(Is::Choice, "select", id, name, nullptr, nullptr, 0, options, count);
  }

 private:
  // What decides which limits a thing tells, and whether it can be set.
  enum class Is : uint8_t { Other, Number, Text, Choice };

  // A text of the firmware has its languages; a name the owner gave is one text.
  void name(ThingName name, const char* id) {
    const char* en = name.en && *name.en ? name.en : id;
    const char* it = name.it && *name.it ? name.it : en;
    if (en == it || strcmp(en, it) == 0) {
      w_.str("name", en);
    } else {
      w_.beginObject("name");
      w_.str("en", en);
      w_.str("it", it);
      w_.endObject();
    }
  }

  void thing(Is is, const char* kind, const char* id, ThingName names, const char* unit,
             const char* type, uint8_t decimals, const ThingOption* options = nullptr,
             uint8_t count = 0) {
    const bool apart = technical_;
    technical_ = false;
    const Next next = next_.take();
    if (ofPart_ ? !(in_ && partNo_ == 1) : in_) return;
    w_.beginObject();
    w_.str("id", id);
    w_.str("kind", kind);
    name(names, id);
    if (type && *type) w_.str("type", type);
    if (unit && *unit) w_.str("unit", unit);
    if (decimals) w_.u32("decimals", decimals);
    if (is == Is::Number && (next.range || next.settable)) {
      w_.f64("min", next.min, 6);
      w_.f64("max", next.max, 6);
      if (next.settable && next.step > 0) w_.f64("step", next.step, 6);
    } else if (is == Is::Text && next.settable) {
      if (next.min > 0) w_.u32("min", static_cast<uint32_t>(next.min));
      w_.u32("max", textMax(next));
    }
    if (is == Is::Choice) {
      w_.beginArray("options");
      for (uint8_t i = 0; i < count; ++i) {
        w_.beginObject();
        w_.str("id", options[i].id);
        name(options[i].name, options[i].id);
        w_.endObject();
      }
      w_.endArray();
    }
    if (next.settable && is != Is::Other) w_.boolean("writable", true);
    if (apart) w_.boolean("technical", true);
    w_.endObject();
  }

  JsonWriter& w_;
  bool ofPart_;
  bool in_ = false;
  bool technical_ = false;
  uint16_t partNo_ = 0;
  Next next_;
};

// The parts, by id and name, a page of them.
class PartsSink final : public ThingSink {
 public:
  PartsSink(JsonWriter& w, size_t from, size_t max) : w_(w), from_(from), max_(max) {}

  void part(const char* id, const char* name) override {
    if (!id || !*id) return;
    const size_t at = seen_++;
    if (at < from_) return;
    if (written_ >= max_) {
      if (!next_) next_ = at;
      return;
    }
    w_.beginObject();
    w_.str("id", id);
    w_.str("name", name && *name ? name : id);
    w_.endObject();
    ++written_;
  }
  void toggle(const char*, ThingName, bool, const char*) override {}
  void light(const char*, ThingName, bool, uint8_t, uint8_t, uint8_t, uint8_t, bool) override {}
  void number(const char*, ThingName, double, const char*, const char*, uint8_t, bool) override {}
  void flag(const char*, ThingName, bool, const char*) override {}
  void text(const char*, ThingName, const char*) override {}
  void button(const char*, ThingName) override {}
  void choice(const char*, ThingName, const char*, const ThingOption*, uint8_t) override {}

  // Where the next page starts; 0 when this one was the last.
  size_t next() const { return next_; }

 private:
  JsonWriter& w_;
  size_t from_;
  size_t max_;
  size_t seen_ = 0;
  size_t written_ = 0;
  size_t next_ = 0;
};

// The values. Without a book: the ones of the module itself, as members of the object that is
// open. With a book: one object for each part changed after since, a page of them.
class ValuesSink final : public ThingSink {
 public:
  ValuesSink(JsonWriter& w, const ThingsBook* book, uint32_t since, size_t from, size_t max)
      : w_(w), book_(book), since_(since), from_(from), max_(max) {}

  void part(const char* id, const char*) override {
    closePart();
    in_ = id && *id;
    if (!in_ || !book_) return;
    const size_t at = seen_++;
    if (at < from_ || book_->changedAt(id) <= since_) return;
    if (written_ >= max_) {
      if (!next_) next_ = at;
      return;
    }
    w_.beginObject();
    w_.str("id", id);
    w_.beginObject("values");
    open_ = true;
    dated_ = 0;
    ++written_;
  }
  void measured(int64_t unixSeconds) override { at_ = stamp(unixSeconds); }
  void toggle(const char* id, ThingName, bool on, const char*) override {
    at_ = 0;
    if (writes()) w_.boolean(id, on);
  }
  void light(const char* id, ThingName, bool on, uint8_t brightness, uint8_t r, uint8_t g,
             uint8_t b, bool colours) override {
    at_ = 0;
    if (!writes()) return;
    w_.beginObject(id);
    w_.boolean("on", on);
    w_.u32("brightness", brightness);
    if (colours) {
      w_.u32("r", r);
      w_.u32("g", g);
      w_.u32("b", b);
    }
    w_.endObject();
  }
  void number(const char* id, ThingName, double value, const char*, const char*, uint8_t decimals,
              bool known) override {
    const uint32_t at = at_;
    at_ = 0;
    if (!writes()) return;
    // Not known is not zero: who reads must be able to tell.
    if (known) {
      w_.f64(id, value, decimals);
    } else {
      w_.null(id);
    }
    // The times of a part follow its values: kept until its values are all written.
    if (at && open_ && dated_ < kThingDated) {
      snprintf(times_[dated_].id, sizeof(times_[dated_].id), "%s", id);
      times_[dated_++].at = at;
    }
  }
  void flag(const char* id, ThingName, bool on, const char*) override {
    at_ = 0;
    if (writes()) w_.boolean(id, on);
  }
  void text(const char* id, ThingName, const char* word) override {
    at_ = 0;
    if (writes()) w_.str(id, word ? word : "");
  }
  void button(const char*, ThingName) override { at_ = 0; }
  void choice(const char* id, ThingName, const char* now, const ThingOption*, uint8_t) override {
    at_ = 0;
    if (writes()) w_.str(id, now ? now : "");
  }

  void end() { closePart(); }
  size_t next() const { return next_; }

 private:
  struct Time {
    char id[24];
    uint32_t at;
  };

  bool writes() const { return book_ ? open_ : !in_; }
  void closePart() {
    if (!open_) return;
    open_ = false;
    w_.endObject();
    if (dated_) {
      w_.beginObject("at");
      for (uint8_t i = 0; i < dated_; ++i) w_.u32(times_[i].id, times_[i].at);
      w_.endObject();
      dated_ = 0;
    }
    w_.endObject();
  }

  JsonWriter& w_;
  const ThingsBook* book_;
  uint32_t since_;
  size_t from_;
  size_t max_;
  size_t seen_ = 0;
  size_t written_ = 0;
  size_t next_ = 0;
  bool in_ = false;
  bool open_ = false;
  uint32_t at_ = 0;
  uint8_t dated_ = 0;
  Time times_[kThingDated];
};

// When the numbers of the module itself were measured, as members of "at". The object is
// opened at the first number that says it: none, and nothing is written.
class TimesSink final : public ThingSink {
 public:
  explicit TimesSink(JsonWriter& w) : w_(w) {}

  void part(const char* id, const char*) override { in_ = id && *id; }
  void measured(int64_t unixSeconds) override { at_ = stamp(unixSeconds); }
  void toggle(const char*, ThingName, bool, const char*) override { at_ = 0; }
  void light(const char*, ThingName, bool, uint8_t, uint8_t, uint8_t, uint8_t, bool) override {
    at_ = 0;
  }
  void number(const char* id, ThingName, double, const char*, const char*, uint8_t, bool) override {
    const uint32_t at = at_;
    at_ = 0;
    if (in_ || !at) return;
    if (!open_) w_.beginObject("at");
    open_ = true;
    w_.u32(id, at);
  }
  void flag(const char*, ThingName, bool, const char*) override { at_ = 0; }
  void text(const char*, ThingName, const char*) override { at_ = 0; }
  void button(const char*, ThingName) override { at_ = 0; }
  void choice(const char*, ThingName, const char*, const ThingOption*, uint8_t) override { at_ = 0; }

  void end() {
    if (open_) w_.endObject();
  }

 private:
  JsonWriter& w_;
  bool in_ = false;
  bool open_ = false;
  uint32_t at_ = 0;
};

// Looks for numbers by their keys: what each one is.
class ResolveSink final : public ThingSink {
 public:
  ResolveSink(const uint32_t* keys, ThingRef* refs, size_t count)
      : keys_(keys), refs_(refs), count_(count) {}

  void part(const char* id, const char*) override {
    snprintf(part_, sizeof(part_), "%s", id ? id : "");
  }
  void toggle(const char*, ThingName, bool, const char*) override {}
  void light(const char*, ThingName, bool, uint8_t, uint8_t, uint8_t, uint8_t, bool) override {}
  void number(const char* id, ThingName, double, const char*, const char*, uint8_t decimals,
              bool) override {
    const uint32_t key = thingKey(part_, id);
    for (size_t i = 0; i < count_; ++i) {
      if (keys_[i] != key || refs_[i].found) continue;
      snprintf(refs_[i].part, sizeof(refs_[i].part), "%s", part_);
      snprintf(refs_[i].thing, sizeof(refs_[i].thing), "%s", id);
      refs_[i].decimals = decimals;
      refs_[i].found = true;
    }
  }
  void flag(const char*, ThingName, bool, const char*) override {}
  void text(const char*, ThingName, const char*) override {}
  void button(const char*, ThingName) override {}
  void choice(const char*, ThingName, const char*, const ThingOption*, uint8_t) override {}

 private:
  const uint32_t* keys_;
  ThingRef* refs_;
  size_t count_;
  char part_[24] = "";
};

}  // namespace

void thingsDescribe(const Module& module, const ThingsBook& book, size_t from, JsonWriter& w) {
  w.u32("gen", book.gen());
  if (from == 0) {
    w.beginArray("things");
    DescribeSink own(w, false);
    module.thingsAll(own);
    w.endArray();
    if (book.parts()) {
      w.beginArray("partThings");
      DescribeSink first(w, true);
      module.thingsAll(first);
      w.endArray();
    }
  }
  if (!book.parts()) return;
  w.beginArray("parts");
  PartsSink names(w, from, kThingPartsPage);
  module.thingsAll(names);
  w.endArray();
  if (names.next()) w.u32("next", static_cast<uint32_t>(names.next()));
}

void thingsOwnValues(const Module& module, JsonWriter& w) {
  ValuesSink own(w, nullptr, 0, 0, 0);
  module.thingsAll(own);
}

void thingsOwnTimes(const Module& module, JsonWriter& w) {
  TimesSink times(w);
  module.thingsAll(times);
  times.end();
}

void thingsResolve(const Module& module, const uint32_t* keys, ThingRef* refs, size_t count) {
  for (size_t i = 0; i < count; ++i) refs[i] = ThingRef();
  ResolveSink names(keys, refs, count);
  module.thingsAll(names);
}

void thingsPartValues(const Module& module, const ThingsBook& book, uint32_t since, size_t from,
                      JsonWriter& w) {
  w.u32("rev", book.rev());
  w.beginArray("parts");
  ValuesSink parts(w, &book, since, from, kThingValuesPage);
  module.thingsAll(parts);
  parts.end();
  w.endArray();
  if (parts.next()) w.u32("next", static_cast<uint32_t>(parts.next()));
}

Status thingsSet(Module& module, const Args& a) {
  const char* thing = a.str("thing");
  if (!*thing) return Status::Invalid;
  ThingAsk ask;
  const char* value = a.str("value");
  if (strcmp(value, "ON") == 0 || strcmp(value, "OFF") == 0) {
    ask.hasOn = true;
    ask.on = value[1] == 'N';
  } else if (strcmp(value, "PRESS") == 0) {
    ask.press = true;
  }
  // The same word is what a text or a choice is set to: the thing itself says how it takes
  // it, and a word that means nothing to it is refused there.
  if (a.has("value")) {
    ask.hasText = true;
    ask.text = value;
  }
  if (a.has("brightness")) {
    ask.hasBrightness = true;
    ask.brightness = a.u8("brightness");
  }
  if (a.has("r") || a.has("g") || a.has("b")) {
    ask.hasColour = true;
    ask.r = a.u8("r");
    ask.g = a.u8("g");
    ask.b = a.u8("b");
  }
  if (a.has("number")) {
    ask.hasNumber = true;
    ask.number = a.f64("number");
  }
  if (!ask.hasOn && !ask.press && !ask.hasBrightness && !ask.hasColour && !ask.hasNumber &&
      !ask.hasText) {
    return Status::Invalid;
  }
  return module.thingSetAll(thing, ask);
}

}  // namespace lmp::sdk

#endif  // LMP_FEATURE_THINGS

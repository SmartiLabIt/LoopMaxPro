// LoopMaxPro SDK - the things of a module for who reads them as data: the server through the
// bridge, a console, the serial. They are the same things a home
// system gets (sdk/home.h). Every module has them without writing anything more:
//
//   in its document   "things": {"gen": 40213, "rev": 87,
//                                "values": {"count": 0, "motion": false}, "parts": 2}
//   things            what they are: id, kind, name, type, unit, technical; the limits of a
//                     number, what can be set, the values of a choice; the parts
//   things.values     the values of the parts changed since a rev, a page at a time
//   thing.set         turns, presses, sets a thing: the same command for every module
//
// gen changes when the description changes: a thing or a part added, taken away, renamed.
// rev goes up when a value changes. Who keeps a copy reads again what the two tell.
// The parts of a module, the nodes of a hub, all have the same things: they are described
// once, and their values travel a page at a time.
//
// A number that says when it was measured (sdk/home.h) has its time beside its value:
//
//   in its document   "things": {..., "values": {"liquid": 80}, "at": {"liquid": 1790000000}}
//   things.values     each part: {"id": "t1", "values": {...}, "at": {"liquid": 1790000000}}
//
// Measured again, also with the same value, the time is another and rev goes up. While the
// server of the device is away such measures are kept by who hosts the modules, each with its
// time, and given when the server asks for them.
#pragma once
#include "lmp_config.h"

#if LMP_FEATURE_THINGS

#include "kernel/command/command.h"
#include "kernel/json/json_writer.h"
#include "sdk/module.h"

namespace lmp::sdk {

// Parts in a page of values, and in a page of the description.
constexpr size_t kThingValuesPage = 12;
constexpr size_t kThingPartsPage = 32;
// Numbers of a part whose time of measure travels with its values.
constexpr size_t kThingDated = 8;

// Who takes the measures nobody was there to be told: the numbers that say when they were
// measured, each time they are measured again.
class ThingMeasures {
 public:
  virtual ~ThingMeasures() = default;
  // key: the number, as thingKey() names it. at: Unix seconds.
  virtual void measure(uint32_t key, double value, uint32_t at) = 0;
};

// A thing as one number, to remember it without its texts: the id of its part, empty or null
// for the module itself, and its own.
uint32_t thingKey(const char* part, const char* id);

// What a remembered number is: its ids, and how many decimals it is shown with.
struct ThingRef {
  char part[24];
  char thing[24];
  uint8_t decimals;
  bool found;
};

// What the things of a module were at the last look.
class ThingsBook {
 public:
  ThingsBook() = default;
  ~ThingsBook() { forget(); }
  ThingsBook(const ThingsBook&) = delete;
  ThingsBook& operator=(const ThingsBook&) = delete;
  // A book moves with the place of its module: what the other one knew goes with it.
  ThingsBook(ThingsBook&& other) noexcept { take(other); }
  ThingsBook& operator=(ThingsBook&& other) noexcept {
    if (this != &other) {
      forget();
      take(other);
    }
    return *this;
  }

  // Looks at the things of the module as they are now. True when the description or a value
  // changed since the look of before. With somebody who takes the measures: each number
  // measured after the look of before is given to it. The first look gives none: it says
  // where the measures are, and only what comes after it is new.
  bool look(const Module& module, ThingMeasures* measures = nullptr);
  // The module stopped: no things until it is looked at again.
  void forget();

  bool any() const { return own_ != 0 || parts_ != 0; }
  uint16_t gen() const { return gen_; }
  uint32_t rev() const { return rev_; }
  uint16_t parts() const { return parts_; }
  // The rev a part changed at. The rev of now for a part the book has no room for: it is
  // always taken as changed.
  uint32_t changedAt(const char* part) const;

 private:
  friend class ThingsLook;

  struct Part {
    uint32_t id;       // its id, as a number
    uint32_t rev;      // the rev it changed at
    uint16_t values;   // its values, as a number
    bool seen;
  };

  // A part as the look found it. True when it is new or its values changed.
  bool take(uint32_t id, uint16_t values, uint32_t rev);
  void take(ThingsBook& other);

  uint16_t gen_ = 0;
  uint32_t rev_ = 0;
  uint16_t own_ = 0;         // things of the module itself
  uint16_t ownValues_ = 0;
  uint16_t parts_ = 0;
  uint16_t over_ = 0;        // the values of the parts that found no room, together
  Part* table_ = nullptr;    // taken when the module has parts
  uint16_t used_ = 0;
  uint32_t measuredUpTo_ = 0;   // the latest time of measure seen so far
  bool based_ = false;          // a look was made: what is measured later is new
};

// The description: "gen", and from the first page "things" and "partThings"; "parts" from the
// one at from, and "next" when there are more.
void thingsDescribe(const Module& module, const ThingsBook& book, size_t from, JsonWriter& w);
// The values of the things of the module itself, as members: "count": 0, "motion": false.
void thingsOwnValues(const Module& module, JsonWriter& w);
// When the numbers of the module itself were measured: the object "at", with a member for
// each number that says it. Nothing when none does.
void thingsOwnTimes(const Module& module, JsonWriter& w);
// Looks among the numbers of the module for the ones with these keys: what each one is.
// found stays false for a number that is no longer there.
void thingsResolve(const Module& module, const uint32_t* keys, ThingRef* refs, size_t count);
// "rev", and "parts" with the values of the parts changed after since, from the one at from;
// "next" when there are more.
void thingsPartValues(const Module& module, const ThingsBook& book, uint32_t since, size_t from,
                      JsonWriter& w);
// What thing.set asks: thing, and value ("ON", "OFF", "PRESS", or the text or the choice to
// set), brightness, r g b; number for a number that can be set.
Status thingsSet(Module& module, const Args& args);

}  // namespace lmp::sdk

#endif  // LMP_FEATURE_THINGS

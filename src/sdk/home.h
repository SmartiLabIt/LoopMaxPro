// LoopMaxPro SDK - what a module shows and lets command to a home system.
// A home system is who puts the devices of a house together: Home Assistant through the
// broker today, the server for the voice assistants tomorrow. A module does not know which
// one: it tells its things, one by one, with what they are now, and takes what it is asked.
//
//   void things(lmp::sdk::ThingSink& out) const override {
//     out.number("count", {"Count", "Conteggio"}, count_);
//     out.flag("motion", {"Motion", "Movimento"}, count_ > 0, "motion");
//     out.number("liquid", {"Liquid", "Liquido"}, liquid_, "%");
//     out.technical();
//     out.flag("sensor_fault", {"Liquid sensor fault", "Sensore del liquido guasto"}, !sensorOk_,
//              "problem");
//     out.button("measure", {"Measure the liquid", "Misura il liquido"});
//   }
//   lmp::Status thingSet(const char* id, const lmp::sdk::ThingAsk&) override {
//     if (strcmp(id, "measure") == 0) return measureNow();
//     return lmp::Status::NotFound;
//   }
//
// The parts of the SDK tell theirs by themselves: a bank of pins its channels as switches, the
// lights as lights. The ids never change: they are how the home system remembers a thing.
// When a module has parts that are devices of their own, every part has the same things.
//
// A number can say its limits, and a number, a text or a choice can be set from outside:
//
//     out.range(0, 100);
//     out.number("liquid", {"Liquid", "Liquido"}, liquid_, "%");
//     out.settable(5, 30, 0.5);
//     out.number("target", {"Target", "Voluta"}, target_, "°C", "temperature", 1);
//     out.settable();
//     out.choice("mode", {"Mode", "Modo"}, mode_, kModes);
//
//   lmp::Status thingSet(const char* id, const lmp::sdk::ThingAsk& ask) override {
//     if (strcmp(id, "target") == 0) return setTarget(ask.number);
//     if (strcmp(id, "mode") == 0) return setMode(ask.text);
//
// What is asked is checked before the module sees it: a number out of its limits, a text too
// long, a choice that is not among its values never reach thingSet.
//
// A number that is measured at a moment says when, and tells so again at every measure:
//
//     out.measured(liquidAt_);
//     out.number("liquid", {"Liquid", "Liquido"}, liquid_, "%");
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "base/status.h"

namespace lmp::sdk {

// The name of a thing, in the languages the firmware speaks: English, then Italian. A name the
// owner gave, a channel or a node, is one text, the same in every language. A home system
// gets the one of the language of the device; the server gets them all (sdk/things.h).
struct ThingName {
  const char* en;
  const char* it;
  constexpr ThingName(const char* same) : en(same), it(same) {}   // NOLINT(runtime/explicit)
  constexpr ThingName(const char* english, const char* italian) : en(english), it(italian) {}
  // The text for a language: "it", or anything else for English.
  const char* in(const char* lang) const {
    return lang && lang[0] == 'i' && lang[1] == 't' && it ? it : en;
  }
};

// One of the values a choice takes: how it is written, and how it is shown.
struct ThingOption {
  const char* id;
  ThingName name;
};

// The longest text that can be set.
constexpr size_t kThingTextMax = 64;

class ThingSink {
 public:
  virtual ~ThingSink() = default;

  // The things that follow belong to a part of the module that is a device of its own for who
  // looks at it: a node of a hub. A null id goes back to the device itself.
  virtual void part(const char* id, const char* name) = 0;

  // The next thing, and only that one, is for who looks after the device and not for every
  // day: a broken sensor, a battery, a radio link. A home system keeps these apart, and does
  // not offer them to a voice assistant.
  virtual void technical() {}

  // The next button, and only that one, undoes what does not come back, as the command of the
  // module with its name: clearing a count. The console asks before pressing it; a home system
  // and the voice assistants never get it (F30 of the server).
  virtual void sensitive() {}

  // The next number, and only that one, goes from min to max: what a gauge shows, and where a
  // threshold makes sense.
  virtual void range(double min, double max) {
    (void)min;
    (void)max;
  }

  // The next number, text or choice, and only that one, can be set by who looks at it: what
  // is asked comes in thingSet. A number goes from min to max in steps of step, and what is
  // asked is brought to the nearest step. For a text min and max are its length, up to
  // kThingTextMax. A choice needs none of them.
  virtual void settable(double min = 0, double max = 0, double step = 1) {
    (void)min;
    (void)max;
    (void)step;
  }

  // The next number, and only that one, was measured at this time: Unix seconds, 0 when the
  // time is not known. Said again with a later time when the number is measured again, also
  // when it is the same as before: who reads tells a value that stands still from a sensor
  // that is silent. A measure made while the server of the device is away is kept, with its
  // time, and given when the server is back (sdk/things.h).
  virtual void measured(int64_t unixSeconds) { (void)unixSeconds; }

  // Something that is on or off, and can be turned. kind: "outlet" for a socket; null: a plain
  // switch.
  virtual void toggle(const char* id, ThingName name, bool on, const char* kind = nullptr) = 0;
  // A light: on or off, how bright from 1 to 100, and its colour when it has colours.
  virtual void light(const char* id, ThingName name, bool on, uint8_t brightness, uint8_t r,
                     uint8_t g, uint8_t b, bool colours) = 0;
  // A number to read. unit: "%", "cm", "°C"; null for a count. kind says what it measures in
  // the words home systems share ("temperature", "humidity", "battery", "distance"); null
  // when it is none of them. known false: the module has no value now.
  virtual void number(const char* id, ThingName name, double value, const char* unit = nullptr,
                      const char* kind = nullptr, uint8_t decimals = 0, bool known = true) = 0;
  // Yes or no, to read. kind: "problem", "motion", "occupancy", "connectivity", "battery";
  // null: plain. The voice assistants know few of them: "motion" is the one most of them hear.
  virtual void flag(const char* id, ThingName name, bool on, const char* kind = nullptr) = 0;
  // A word to read: a state with a name.
  virtual void text(const char* id, ThingName name, const char* value) = 0;
  // Something to press: it does one thing, and has no state.
  virtual void button(const char* id, ThingName name) = 0;
  // One among fixed values: a mode, an effect. value is the id of the one of now.
  virtual void choice(const char* id, ThingName name, const char* value,
                      const ThingOption* options, uint8_t count) = 0;
  template <size_t N>
  void choice(const char* id, ThingName name, const char* value, const ThingOption (&options)[N]) {
    choice(id, name, value, options, static_cast<uint8_t>(N));
  }
};

// What a home system asks of a thing. Only what is asked is there.
struct ThingAsk {
  bool hasOn = false;
  bool on = false;
  bool hasBrightness = false;
  uint8_t brightness = 0;      // 1 to 100
  bool hasColour = false;
  uint8_t r = 0, g = 0, b = 0;
  bool press = false;          // a button
  bool hasNumber = false;
  double number = 0;           // a number that can be set: within its limits, on its step
  bool hasText = false;
  const char* text = "";       // a text that can be set, or the id of a value of a choice
};

}  // namespace lmp::sdk

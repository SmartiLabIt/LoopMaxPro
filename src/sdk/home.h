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
#pragma once
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
};

}  // namespace lmp::sdk

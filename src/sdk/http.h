// LoopMaxPro SDK - the requests of other programs to a module (Module::onHttp), from 1.0.9.
// A module whose purpose is to answer other programs, as a set of test endpoints, takes
// a word: what arrives at /<word>/... is its own. No login: only from the local networks (the
// private addresses), with the headers a browser wants from a page of another origin (CORS). The
// web service answers the question a browser asks before (OPTIONS) by itself. Every other module
// goes through commands and documents.
#pragma once
#include "hal/api/ihttp.h"
#include "kernel/json/json_writer.h"

namespace lmp::sdk {

struct HttpCall {
  HttpCall(const hal::IHttpRequest& r, StringView p, BufferSink& o) : req(r), rest(p), out(o) {}

  // Method, query, headers, body, who asks.
  const hal::IHttpRequest& req;
  // The path after the word, without the slash: "weather/today" for /ep/weather/today.
  StringView rest;
  // The answer: its code, its type (a literal), its body written in out.
  uint16_t code = 404;
  const char* type = "application/json";
  BufferSink& out;
};

}  // namespace lmp::sdk

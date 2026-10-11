// LoopMaxPro - the console inside the program.
// The build puts the files of the console and the interfaces of the modules of the firmware in
// the program, compressed (tools/pio/web.py): an update of the program brings its console, the
// filesystem keeps only the data. Boards built without them (ESP8266, the PC) serve the console
// from the filesystem, as before.
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "base/string_view.h"

namespace lmp {

struct WebAsset {
  const char* path;        // "/index.html", "/lmp.c2eacd2b.js"
  const uint8_t* data;     // gzip
  uint32_t size;
};

// The files of the program. Null when it has none.
const WebAsset* webBundleFind(StringView path);
size_t webBundleCount();
const WebAsset& webBundleAt(size_t index);

// Tests only: files that take the place of those of the program. Null and 0 put them back.
void webBundleReplace(const WebAsset* assets, size_t count);

}  // namespace lmp

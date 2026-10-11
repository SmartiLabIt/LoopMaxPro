// LoopMaxPro HAL - ESP32 family: where the filesystem is mounted.
// The storage of both frameworks and the HTTP server read the files from here.
#pragma once

namespace lmp::hal::esp32 {

constexpr const char* kFsMountPoint = "/littlefs";
constexpr const char* kFsPartition = "spiffs";

}  // namespace lmp::hal::esp32

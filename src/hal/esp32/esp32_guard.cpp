// LoopMaxPro HAL - ESP32 family: the last words and the guard of the main task.
//
// The last words are the last entries of the log and the reason of a restart asked on purpose,
// in the RTC memory: a restart does not clear it, a loss of power does. At the next start the
// system writes them in the log, so a board that stopped says why after it starts again.
//
// The guard of the main task: the watchdog of ESP-IDF looks at the idle tasks only, so a main
// task blocked for ever (waiting on a lock, inside a driver) would leave the board dead, deaf on
// the network, until somebody takes the power away. A timer of the system, which runs apart
// from that task, looks every few seconds when the task last fed the watchdog, and restarts
// the board when it has not for too long.
#include <esp_attr.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <stdio.h>
#include <string.h>

#include "hal/esp32/esp32_board.h"

namespace lmp::hal::esp32 {
namespace {

constexpr uint32_t kMagic = 0x4C4D5057;          // "LMPW"
constexpr uint64_t kGuardEveryUs = 5000000;      // the guard looks every 5 s

struct Words {
  uint32_t magic;
  uint32_t uptimeS;
  uint8_t head;     // where the next line goes
  uint8_t count;
  char why[sizeof(LastWords::why)];
  char lines[kLastWordsLines][kLastWordsLine];
};

RTC_NOINIT_ATTR Words g_words;
portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;
LastWords g_previous;
bool g_hasPrevious = false;
bool g_begun = false;

volatile int64_t g_fedUs = 0;
volatile uint32_t g_stallS = 0;
esp_timer_handle_t g_timer = nullptr;

void copyText(char* out, size_t cap, const char* in) {
  size_t i = 0;
  for (; in && in[i] && i + 1 < cap; ++i) out[i] = in[i];
  out[i] = '\0';
}

void onGuard(void*) {
  const uint32_t limit = g_stallS;
  const int64_t fed = g_fedUs;
  if (!limit || !fed) return;
  const int64_t silent = esp_timer_get_time() - fed;
  if (silent < static_cast<int64_t>(limit) * 1000000) return;
  char line[kLastWordsLine];
  snprintf(line, sizeof(line), "guard: the main task has not run for %lu s: restart",
           static_cast<unsigned long>(silent / 1000000));
  lastWordsKeep(line);
  lastWordsWhy("guard: main task stalled");   // the prefix of Kernel::guardRestart()
  esp_restart();
}

}  // namespace

void lastWordsBegin() {
  if (g_begun) return;
  g_begun = true;
  // After a loss of power the RTC memory holds noise: the mark and the numbers tell.
  if (esp_reset_reason() != ESP_RST_POWERON && g_words.magic == kMagic &&
      g_words.count <= kLastWordsLines && g_words.head < kLastWordsLines) {
    g_previous = LastWords();
    copyText(g_previous.why, sizeof(g_previous.why), g_words.why);
    g_previous.why[sizeof(g_previous.why) - 1] = '\0';
    g_previous.uptimeS = g_words.uptimeS;
    const uint8_t first = static_cast<uint8_t>((g_words.head + kLastWordsLines - g_words.count) % kLastWordsLines);
    for (uint8_t i = 0; i < g_words.count; ++i) {
      const char* line = g_words.lines[(first + i) % kLastWordsLines];
      memcpy(g_previous.lines[i], line, kLastWordsLine);
      g_previous.lines[i][kLastWordsLine - 1] = '\0';
    }
    g_previous.count = g_words.count;
    g_hasPrevious = g_previous.count > 0 || g_previous.why[0];
  }
  memset(&g_words, 0, sizeof(g_words));
  g_words.magic = kMagic;
}

void lastWordsKeep(const char* line) {
  if (!g_begun || !line) return;
  portENTER_CRITICAL(&g_mux);
  copyText(g_words.lines[g_words.head], kLastWordsLine, line);
  g_words.head = static_cast<uint8_t>((g_words.head + 1) % kLastWordsLines);
  if (g_words.count < kLastWordsLines) ++g_words.count;
  g_words.uptimeS = static_cast<uint32_t>(esp_timer_get_time() / 1000000);
  portEXIT_CRITICAL(&g_mux);
}

void lastWordsWhy(const char* why) {
  if (!g_begun) return;
  portENTER_CRITICAL(&g_mux);
  copyText(g_words.why, sizeof(g_words.why), why);
  portEXIT_CRITICAL(&g_mux);
}

const LastWords* lastWordsPrevious() { return g_hasPrevious ? &g_previous : nullptr; }

void stallGuard(uint32_t seconds) {
  g_stallS = seconds;
  if (!seconds || g_timer) return;
  esp_timer_create_args_t args = {};
  args.callback = &onGuard;
  args.name = "lmp_guard";
  if (esp_timer_create(&args, &g_timer) == ESP_OK) esp_timer_start_periodic(g_timer, kGuardEveryUs);
}

void stallFeed() {
  const int64_t now = esp_timer_get_time();
  g_fedUs = now;
  g_words.uptimeS = static_cast<uint32_t>(now / 1000000);
}

}  // namespace lmp::hal::esp32

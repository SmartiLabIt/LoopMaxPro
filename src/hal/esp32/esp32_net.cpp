#include "hal/esp32/esp32_net.h"

#include <esp_system.h>

#include "hal/common/wifi_reason.h"
#include <lwip/sockets.h>
#include <stdio.h>
#include <string.h>

#if __has_include(<esp_mac.h>)
#include <esp_mac.h>
#endif
#if __has_include(<mdns.h>)
#include <mdns.h>
#define LMP_HAS_MDNS 1
#else
#define LMP_HAS_MDNS 0
#endif

#if LMP_NET_TRACE
// ---------------------------------------------------------------- trace, for who develops
//
// Where a slow board loses its time, second by second on the serial port (lmp_config.h):
// what the Wi-Fi was given to send and what it refused, what TCP sent a second time
// and what arrived; how long each frame stayed in the radio and how many the router did not
// confirm; how long the board stood still, seen by a timer and by a task that wake every 5 ms;
// what was written on the flash, which stops both cores while it lasts. The functions between
// the stack, the Wi-Fi and the flash are taken with the linker: the build needs
//   -Wl,--wrap=esp_wifi_internal_tx -Wl,--wrap=esp_wifi_internal_tx_by_ref
//   -Wl,--wrap=esp_netif_receive
//   -Wl,--wrap=esp_partition_erase_range -Wl,--wrap=esp_partition_write
// Found with it: a first ESP32 at full power of the radio next to its router, each frame
// 100-350 ms in the radio and the router confirming none for seconds.
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_partition.h>
#include <esp_private/wifi.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

extern "C" {
int __real_esp_wifi_internal_tx(wifi_interface_t ifx, void* buffer, uint16_t len);
esp_err_t __real_esp_wifi_internal_tx_by_ref(wifi_interface_t ifx, void* buffer, size_t len,
                                             void* netstackBuf);
esp_err_t __real_esp_netif_receive(esp_netif_t* netif, void* buffer, size_t len, void* eb);
esp_err_t __real_esp_partition_erase_range(const esp_partition_t* partition, size_t offset,
                                           size_t size);
esp_err_t __real_esp_partition_write(const esp_partition_t* partition, size_t offset,
                                     const void* source, size_t size);
}

namespace {

// Data: TCP with bytes of the application. Tcp: the rest of TCP, such as the acknowledgements.
enum TraceKind : uint8_t { kTraceData, kTraceTcp, kTraceArp, kTraceIcmp, kTraceUdp, kTraceElse, kTraceKinds };
struct Trace {
  uint32_t sent[kTraceKinds];
  uint32_t refused[kTraceKinds];
  uint32_t got[kTraceKinds];
  uint32_t again;      // data TCP gave a second time, after the Wi-Fi had taken it
  uint32_t error;      // the last answer of the Wi-Fi that was not a yes
  uint32_t slowestUs;  // the longest the Wi-Fi took to answer, in this second
  // Pauses over 25 ms in a transfer: ended by an acknowledgement just arrived, ended some time
  // after one, ended with none arrived.
  uint32_t gapsAck, gapsLate, gapsNone, gapsUs;
  uint32_t erases, eraseUs, writes, writeUs;
  // Wakes that came over 15 ms late, and the latest of this second.
  uint32_t timerLate, timerLateUs, taskLate, taskLateUs;
  // The radio: frames it said it sent, the ones the router did not confirm, how long they
  // stayed in it together and the longest, how many it held at most in this second.
  uint32_t done, failed, heldUs, heldLongestUs, heldMost;
};
Trace g_trace;
char g_traceLabel[20] = "";
int64_t g_traceGotUs = 0;
// When each frame was handed to the radio, the oldest first.
int64_t g_traceHanded[64];
uint32_t g_traceIn = 0, g_traceOut = 0;
// Where each connection has got to, to know the data sent a second time.
struct TraceFlow {
  uint32_t key;
  uint32_t end;
  uint32_t count;
  int64_t sentUs;
};
TraceFlow g_traceFlows[8];
uint8_t g_traceNext = 0;

uint32_t traceWord(const uint8_t* b) {
  return (static_cast<uint32_t>(b[0]) << 24) | (static_cast<uint32_t>(b[1]) << 16) |
         (static_cast<uint32_t>(b[2]) << 8) | b[3];
}

// The frame as the stack hands it over: Ethernet, then IPv4.
TraceKind traceKind(const uint8_t* f, size_t len, uint32_t* key, uint32_t* from, uint32_t* to) {
  if (!f || len < 34) return kTraceElse;
  const uint16_t type = static_cast<uint16_t>((f[12] << 8) | f[13]);
  if (type == 0x0806) return kTraceArp;
  if (type != 0x0800) return kTraceElse;
  const size_t ip = (f[14] & 0x0F) * 4u;
  const size_t total = (static_cast<size_t>(f[16]) << 8) | f[17];
  if (f[23] == 1) return kTraceIcmp;
  if (f[23] == 17) return kTraceUdp;
  if (f[23] != 6 || len < 14 + ip + 20) return kTraceElse;
  const uint8_t* t = f + 14 + ip;
  const size_t header = (t[12] >> 4) * 4u;
  if (total <= ip + header) return kTraceTcp;
  if (key) {
    *key = traceWord(f + 26) ^ traceWord(f + 30) ^ traceWord(t);
    *from = traceWord(t + 4);
    *to = *from + static_cast<uint32_t>(total - ip - header);
  }
  return kTraceData;
}

TraceFlow* traceFlow(uint32_t key, uint32_t from) {
  for (TraceFlow& flow : g_traceFlows) {
    if (flow.key == key) return &flow;
  }
  TraceFlow& flow = g_traceFlows[g_traceNext++ % 8];
  flow.key = key;
  flow.end = from;
  flow.count = 0;
  flow.sentUs = 0;
  return &flow;
}

template <typename Send>
int traceSend(const void* buffer, size_t len, Send send) {
  uint32_t key = 0, from = 0, to = 0;
  const TraceKind kind = traceKind(static_cast<const uint8_t*>(buffer), len, &key, &from, &to);
  TraceFlow* flow = kind == kTraceData ? traceFlow(key, from) : nullptr;
  if (flow && static_cast<int32_t>(from - flow->end) < 0) ++g_trace.again;
  const int64_t began = esp_timer_get_time();
  if (flow) {
    const int64_t gap = began - flow->sentUs;
    if (++flow->count > 6 && gap > 25000 && gap < 3000000) {
      g_trace.gapsUs += static_cast<uint32_t>(gap);
      if (g_traceGotUs <= flow->sentUs) {
        ++g_trace.gapsNone;
      } else if (began - g_traceGotUs < 3000) {
        ++g_trace.gapsAck;
      } else {
        ++g_trace.gapsLate;
      }
    }
    flow->sentUs = began;
  }
  const int answer = send();
  const uint32_t took = static_cast<uint32_t>(esp_timer_get_time() - began);
  if (took > g_trace.slowestUs) g_trace.slowestUs = took;
  if (answer == ESP_OK) {
    ++g_trace.sent[kind];
    if (flow && static_cast<int32_t>(to - flow->end) > 0) flow->end = to;
    g_traceHanded[g_traceIn++ % 64] = began;
    const uint32_t held = g_traceIn - g_traceOut;
    if (held > g_trace.heldMost) g_trace.heldMost = held;
  } else {
    ++g_trace.refused[kind];
    g_trace.error = static_cast<uint32_t>(answer);
  }
  return answer;
}

void traceTick(void*) {
  static Trace before;
  static uint32_t seconds = 0;
  const Trace now = g_trace;
  g_trace.slowestUs = 0;
  uint32_t sent[kTraceKinds], refused[kTraceKinds], got[kTraceKinds];
  uint32_t allSent = 0, allRefused = 0;
  for (int k = 0; k < kTraceKinds; ++k) {
    sent[k] = now.sent[k] - before.sent[k];
    refused[k] = now.refused[k] - before.refused[k];
    got[k] = now.got[k] - before.got[k];
    allSent += sent[k];
    allRefused += refused[k];
  }
  const uint32_t again = now.again - before.again;
  const uint32_t gapsAck = now.gapsAck - before.gapsAck, gapsLate = now.gapsLate - before.gapsLate;
  const uint32_t gapsNone = now.gapsNone - before.gapsNone, gapsUs = now.gapsUs - before.gapsUs;
  const uint32_t erases = now.erases - before.erases, eraseUs = now.eraseUs - before.eraseUs;
  const uint32_t writes = now.writes - before.writes, writeUs = now.writeUs - before.writeUs;
  const uint32_t timerLate = now.timerLate - before.timerLate;
  const uint32_t taskLate = now.taskLate - before.taskLate;
  const uint32_t done = now.done - before.done, failed = now.failed - before.failed;
  const uint32_t heldUs = now.heldUs - before.heldUs;
  g_trace.timerLateUs = g_trace.taskLateUs = g_trace.heldLongestUs = g_trace.heldMost = 0;
  before = now;
  ++seconds;
  const uint32_t kHeap = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
  // The line of each second is written by the timer itself: one late wake is its own.
  if (erases || writes || timerLate > 1 || taskLate) {
    ESP_LOGW("nettrace", "%us still: timer %u longest %u ms, task %u longest %u ms | flash %s erase %u %u ms write %u %u ms",
             static_cast<unsigned>(seconds), static_cast<unsigned>(timerLate),
             static_cast<unsigned>(now.timerLateUs / 1000), static_cast<unsigned>(taskLate),
             static_cast<unsigned>(now.taskLateUs / 1000), g_traceLabel,
             static_cast<unsigned>(erases), static_cast<unsigned>(eraseUs / 1000),
             static_cast<unsigned>(writes), static_cast<unsigned>(writeUs / 1000));
  }
  if (allRefused || again || allSent >= 30 || failed) {
    ESP_LOGW("nettrace", "%us pauses %u ms: ack %u late %u none %u | radio sent %u failed %u, held %u, longest %u ms, mean %u us",
             static_cast<unsigned>(seconds), static_cast<unsigned>(gapsUs / 1000),
             static_cast<unsigned>(gapsAck), static_cast<unsigned>(gapsLate),
             static_cast<unsigned>(gapsNone), static_cast<unsigned>(done),
             static_cast<unsigned>(failed), static_cast<unsigned>(now.heldMost),
             static_cast<unsigned>(now.heldLongestUs / 1000),
             static_cast<unsigned>(done ? heldUs / done : 0));
  }
  // A frame the radio never told about would stay counted for ever: with nothing sent in this
  // second, the ones still counted are let go.
  if (!allSent && !done) g_traceOut = g_traceIn;
  if (seconds % 10 == 0) {
    wifi_ps_type_t sleep = WIFI_PS_NONE;
    wifi_ap_record_t router = {};
    int8_t power = 0;
    esp_wifi_get_ps(&sleep);
    esp_wifi_sta_get_ap_info(&router);
    esp_wifi_get_max_tx_power(&power);
    ESP_LOGW("nettrace", "%us radio: sleep %d, router %d dBm on channel %d, n %d, power %d, frames in it %u",
             static_cast<unsigned>(seconds), static_cast<int>(sleep), static_cast<int>(router.rssi),
             static_cast<int>(router.primary), static_cast<int>(router.phy_11n),
             static_cast<int>(power), static_cast<unsigned>(g_traceIn - g_traceOut));
  }
  if (allRefused || again || allSent >= 30) {
    ESP_LOGW("nettrace", "%us tx d%u t%u a%u i%u u%u e%u | no d%u t%u a%u i%u u%u x%x | again %u",
             static_cast<unsigned>(seconds), static_cast<unsigned>(sent[kTraceData]),
             static_cast<unsigned>(sent[kTraceTcp]), static_cast<unsigned>(sent[kTraceArp]),
             static_cast<unsigned>(sent[kTraceIcmp]), static_cast<unsigned>(sent[kTraceUdp]),
             static_cast<unsigned>(sent[kTraceElse]), static_cast<unsigned>(refused[kTraceData]),
             static_cast<unsigned>(refused[kTraceTcp]), static_cast<unsigned>(refused[kTraceArp]),
             static_cast<unsigned>(refused[kTraceIcmp]), static_cast<unsigned>(refused[kTraceUdp]),
             static_cast<unsigned>(now.error), static_cast<unsigned>(again));
    ESP_LOGW("nettrace", "   rx d%u t%u a%u i%u u%u e%u | %u us | free %u largest %u",
             static_cast<unsigned>(got[kTraceData]), static_cast<unsigned>(got[kTraceTcp]),
             static_cast<unsigned>(got[kTraceArp]), static_cast<unsigned>(got[kTraceIcmp]),
             static_cast<unsigned>(got[kTraceUdp]), static_cast<unsigned>(got[kTraceElse]),
             static_cast<unsigned>(now.slowestUs),
             static_cast<unsigned>(heap_caps_get_free_size(kHeap)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(kHeap)));
  }
  if (seconds % 60 == 0) {
    ESP_LOGW("nettrace", "%us in all: sent %u data, refused %u data and %u of the rest, again %u",
             static_cast<unsigned>(seconds), static_cast<unsigned>(now.sent[kTraceData]),
             static_cast<unsigned>(now.refused[kTraceData]),
             static_cast<unsigned>(now.refused[kTraceTcp] + now.refused[kTraceArp] +
                                   now.refused[kTraceIcmp] + now.refused[kTraceUdp] +
                                   now.refused[kTraceElse]),
             static_cast<unsigned>(now.again));
  }
}

void traceLate(int64_t late, uint32_t* times, uint32_t* longest) {
  if (late > 15000) ++*times;
  if (late > static_cast<int64_t>(*longest)) *longest = static_cast<uint32_t>(late);
}

// The timer wakes on core 0 above every task but the Wi-Fi: late only when the core is held
// with the interrupts off or the flash is busy.
void traceBeat(void*) {
  static int64_t last = 0;
  const int64_t now = esp_timer_get_time();
  if (last) traceLate(now - last - 5000, &g_trace.timerLate, &g_trace.timerLateUs);
  last = now;
}

// The task wakes just under the task of the stack, on the core that is free: late when the
// stack would be too.
void traceTask(void*) {
  int64_t last = esp_timer_get_time();
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(5));
    const int64_t now = esp_timer_get_time();
    traceLate(now - last - 5000, &g_trace.taskLate, &g_trace.taskLateUs);
    last = now;
  }
}

// From the task of the Wi-Fi, for every frame the radio has done with.
void traceDone(uint8_t, uint8_t*, uint16_t*, bool sent) {
  ++g_trace.done;
  if (!sent) ++g_trace.failed;
  if (g_traceOut == g_traceIn) return;
  const uint32_t held = static_cast<uint32_t>(esp_timer_get_time() - g_traceHanded[g_traceOut++ % 64]);
  g_trace.heldUs += held;
  if (held > g_trace.heldLongestUs) g_trace.heldLongestUs = held;
}

void traceBegin() {
  static bool begun = false;
  if (begun) return;
  begun = true;
  esp_wifi_set_tx_done_cb(&traceDone);
  esp_timer_create_args_t args = {};
  args.callback = &traceTick;
  args.name = "lmp_nettrace";
  esp_timer_handle_t timer = nullptr;
  if (esp_timer_create(&args, &timer) == ESP_OK) esp_timer_start_periodic(timer, 1000000);
  args.callback = &traceBeat;
  args.name = "lmp_netbeat";
  if (esp_timer_create(&args, &timer) == ESP_OK) esp_timer_start_periodic(timer, 5000);
  xTaskCreate(&traceTask, "lmp_nettrace", 2048, nullptr, 17, nullptr);
}

}  // namespace

extern "C" {

int __wrap_esp_wifi_internal_tx(wifi_interface_t ifx, void* buffer, uint16_t len) {
  return traceSend(buffer, len, [&] { return __real_esp_wifi_internal_tx(ifx, buffer, len); });
}

esp_err_t __wrap_esp_wifi_internal_tx_by_ref(wifi_interface_t ifx, void* buffer, size_t len,
                                             void* netstackBuf) {
  return traceSend(buffer, len, [&] {
    return static_cast<int>(__real_esp_wifi_internal_tx_by_ref(ifx, buffer, len, netstackBuf));
  });
}

esp_err_t __wrap_esp_netif_receive(esp_netif_t* netif, void* buffer, size_t len, void* eb) {
  const TraceKind kind = traceKind(static_cast<const uint8_t*>(buffer), len, nullptr, nullptr, nullptr);
  ++g_trace.got[kind];
  if (kind == kTraceTcp || kind == kTraceData) g_traceGotUs = esp_timer_get_time();
  return __real_esp_netif_receive(netif, buffer, len, eb);
}

esp_err_t __wrap_esp_partition_erase_range(const esp_partition_t* partition, size_t offset,
                                           size_t size) {
  const int64_t began = esp_timer_get_time();
  const esp_err_t e = __real_esp_partition_erase_range(partition, offset, size);
  ++g_trace.erases;
  g_trace.eraseUs += static_cast<uint32_t>(esp_timer_get_time() - began);
  if (partition) snprintf(g_traceLabel, sizeof(g_traceLabel), "%s", partition->label);
  return e;
}

esp_err_t __wrap_esp_partition_write(const esp_partition_t* partition, size_t offset,
                                     const void* source, size_t size) {
  const int64_t began = esp_timer_get_time();
  const esp_err_t e = __real_esp_partition_write(partition, offset, source, size);
  ++g_trace.writes;
  g_trace.writeUs += static_cast<uint32_t>(esp_timer_get_time() - began);
  if (partition) snprintf(g_traceLabel, sizeof(g_traceLabel), "%s", partition->label);
  return e;
}

}  // extern "C"
#endif  // LMP_NET_TRACE

namespace lmp::hal::esp32 {
namespace {

constexpr uint16_t kDnsPort = 53;
constexpr size_t kDnsPacket = 512;
constexpr uint32_t kDnsTtlSec = 60;
constexpr uint8_t kApClients = 4;

class Lock {
 public:
  explicit Lock(SemaphoreHandle_t handle) : handle_(handle) {
    if (handle_) xSemaphoreTake(handle_, portMAX_DELAY);
  }
  ~Lock() {
    if (handle_) xSemaphoreGive(handle_);
  }

 private:
  SemaphoreHandle_t handle_;
};

Ip4 fromEsp(const esp_ip4_addr_t& a) {
  return Ip4::of(esp_ip4_addr1_16(&a), esp_ip4_addr2_16(&a), esp_ip4_addr3_16(&a),
                 esp_ip4_addr4_16(&a));
}

esp_ip4_addr_t toEsp(const Ip4& ip) {
  esp_ip4_addr_t a;
  a.addr = ESP_IP4TOADDR(ip.b[0], ip.b[1], ip.b[2], ip.b[3]);
  return a;
}

// The names of a network are 32 bytes without a terminator when they are full.
void copyName(char* out, size_t cap, const uint8_t* name, size_t length) {
  const size_t n = length < cap - 1 ? length : cap - 1;
  memcpy(out, name, n);
  out[n] = '\0';
}

}  // namespace

// ---------------------------------------------------------------- start

Status Net::init(const char* hostname) {
  if (!ready_) {
    if (!lock_) lock_ = xSemaphoreCreateMutex();
    esp_err_t e = esp_netif_init();
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) return Status::Io;
    e = esp_event_loop_create_default();
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) return Status::Io;
    if (!staNetif_) staNetif_ = esp_netif_create_default_wifi_sta();
    if (!apNetif_) apNetif_ = esp_netif_create_default_wifi_ap();
    if (!staNetif_ || !apNetif_) return Status::NoMem;

    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    // Buffers for sending taken when they are needed: kept always, 8 of them, they are memory
    // a secure connection needs. For receiving 8 are kept, as the Arduino core
    // has: they must be more than the frames the router joins in one burst (6). With 4 the
    // bursts were dropped: pings of 200 ms, pages stuck, the broker lost.
    // The ones taken when needed: 32, unless a module asks for fewer on a board (lmp_config.h).
    // ExpNet on the C3 asks for 16: with 32 a burst of answers of a busy network took up
    // to 50 KB, all the memory the C3 had free, and it left the network.
    config.tx_buf_type = 1;
    config.static_tx_buf_num = 0;
    config.dynamic_tx_buf_num = LMP_WIFI_DYNAMIC_TX_BUFFERS;
    config.cache_tx_buf_num = 4;
    config.static_rx_buf_num = 8;
    config.dynamic_rx_buf_num = LMP_WIFI_DYNAMIC_RX_BUFFERS;
    // The settings of the network are kept by LoopMaxPro, not by the driver.
    config.nvs_enable = 0;
    if (esp_wifi_init(&config) != ESP_OK) return Status::Io;
#if LMP_NET_TRACE
    traceBegin();
#endif
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &Net::onEvent, this,
                                        nullptr);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &Net::onEvent, this,
                                        nullptr);
    ready_ = true;
  }
  if (hostname && *hostname) esp_netif_set_hostname(staNetif_, hostname);
  return Status::Ok;
}

Status Net::applyMode() {
  if (!ready_) return Status::Denied;
  const bool sta = wantSta_ || scanning_;
  wifi_mode_t mode = WIFI_MODE_NULL;
  if (sta && wantAp_) {
    mode = WIFI_MODE_APSTA;
  } else if (sta) {
    mode = WIFI_MODE_STA;
  } else if (wantAp_) {
    mode = WIFI_MODE_AP;
  }

  if (mode == WIFI_MODE_NULL) {
    if (running_) esp_wifi_stop();
    running_ = false;
    return Status::Ok;
  }
  wifi_mode_t current = WIFI_MODE_NULL;
  esp_wifi_get_mode(&current);
  if (current != mode && esp_wifi_set_mode(mode) != ESP_OK) return Status::Io;
  if (!running_) {
    if (esp_wifi_start() != ESP_OK) return Status::Io;
    running_ = true;
    esp_wifi_set_ps(LMP_NET_POWER_SAVE ? WIFI_PS_MIN_MODEM : WIFI_PS_NONE);
#if LMP_WIFI_MAX_TX_QDBM
    esp_wifi_set_max_tx_power(LMP_WIFI_MAX_TX_QDBM);
#endif
  }
  return Status::Ok;
}

void Net::poll() {
  // A scan borrowed the station: when it ends the radio goes back to what is wanted.
  if (modeStale_) {
    modeStale_ = false;
    applyMode();
  }
}

// ---------------------------------------------------------------- events of the driver

void Net::onEvent(void* self, esp_event_base_t base, int32_t id, void* data) {
  static_cast<Net*>(self)->handle(base, id, data);
}

void Net::emit(NetEvent event) const {
  if (handler_) handler_(event);
}

void Net::handle(esp_event_base_t base, int32_t id, void* data) {
  if (base == IP_EVENT) {
    if (id != IP_EVENT_STA_GOT_IP) return;
    const ip_event_got_ip_t* got = static_cast<const ip_event_got_ip_t*>(data);
    esp_netif_dns_info_t dns;
    memset(&dns, 0, sizeof(dns));
    esp_netif_get_dns_info(staNetif_, ESP_NETIF_DNS_MAIN, &dns);
    {
      Lock lock(lock_);
      sta_.state = StaState::Connected;
      sta_.ip = fromEsp(got->ip_info.ip);
      sta_.gateway = fromEsp(got->ip_info.gw);
      sta_.mask = fromEsp(got->ip_info.netmask);
      sta_.dns = fromEsp(dns.ip.u_addr.ip4);
      sta_.failReason = 0;
      sta_.fail = StaFail::None;
    }
    // The driver may put the radio to sleep between the beacons of the router on its own: every
    // packet for the device then waits up to a beacon. Asked again once on the network.
    esp_wifi_set_ps(LMP_NET_POWER_SAVE ? WIFI_PS_MIN_MODEM : WIFI_PS_NONE);
    emit(NetEvent::StaGotIp);
    return;
  }

  switch (id) {
    case WIFI_EVENT_STA_CONNECTED:
      emit(NetEvent::StaConnected);
      break;

    case WIFI_EVENT_STA_DISCONNECTED: {
      const wifi_event_sta_disconnected_t* gone =
          static_cast<const wifi_event_sta_disconnected_t*>(data);
      // Leaving on request is not a loss.
      if (gone->reason == kWifiReasonLeft) break;
      bool was;
      {
        Lock lock(lock_);
        if (sta_.state == StaState::Idle) break;
        was = sta_.state == StaState::Connected;
        sta_.state = was ? StaState::Lost : StaState::Failed;
        sta_.ip = Ip4();
        sta_.failReason = static_cast<uint8_t>(gone->reason);
        sta_.fail = wifiFailOf(sta_.failReason);
      }
      emit(was ? NetEvent::StaLost : NetEvent::StaFailed);
      break;
    }

    case WIFI_EVENT_AP_START:
      emit(NetEvent::ApStarted);
      break;
    case WIFI_EVENT_AP_STOP:
      emit(NetEvent::ApStopped);
      break;
    case WIFI_EVENT_AP_STACONNECTED:
      emit(NetEvent::ApClientJoined);
      break;
    case WIFI_EVENT_AP_STADISCONNECTED:
      emit(NetEvent::ApClientLeft);
      break;

    case WIFI_EVENT_SCAN_DONE: {
      const wifi_event_sta_scan_done_t* done =
          static_cast<const wifi_event_sta_scan_done_t*>(data);
      // The driver hands over its list once: what does not fit here is lost.
      static wifi_ap_record_t records[kRecords];
      uint16_t count = kRecords;
      const bool good = done->status == 0 && esp_wifi_scan_get_ap_records(&count, records) == ESP_OK;
      {
        Lock lock(lock_);
        found_ = 0;
        for (uint16_t i = 0; good && i < count; ++i) {
          WifiNetwork& n = records_[found_++];
          copyName(n.ssid, sizeof(n.ssid), records[i].ssid,
                   strnlen(reinterpret_cast<const char*>(records[i].ssid), 32));
          n.rssi = records[i].rssi;
          n.channel = records[i].primary;
          n.secured = records[i].authmode != WIFI_AUTH_OPEN;
        }
        scan_ = good ? ScanState::Done : ScanState::Failed;
      }
      scanning_ = false;
      modeStale_ = true;
      emit(NetEvent::ScanDone);
      break;
    }

    default:
      break;
  }
}

// ---------------------------------------------------------------- client

Status Net::staConnect(const char* ssid, const char* password) {
  if (!ready_) return Status::Denied;
  if (!ssid || !*ssid || strlen(ssid) > 32) return Status::Invalid;
  const char* key = password ? password : "";
  if (strlen(key) > 63) return Status::Invalid;

  wantSta_ = true;
  LMP_TRY(applyMode());
  // An attempt left in the middle must not answer in place of this one.
  esp_wifi_disconnect();

  wifi_config_t config;
  memset(&config, 0, sizeof(config));
  memcpy(config.sta.ssid, ssid, strlen(ssid));
  memcpy(config.sta.password, key, strlen(key));
  config.sta.threshold.authmode = *key ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
  config.sta.pmf_cfg.capable = true;
  config.sta.pmf_cfg.required = false;
  // Among the access points of the network, the one heard best.
  config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
  config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
  if (esp_wifi_set_config(WIFI_IF_STA, &config) != ESP_OK) return Status::Invalid;

  {
    Lock lock(lock_);
    sta_ = StaInfo();
    sta_.state = StaState::Connecting;
    snprintf(sta_.ssid, sizeof(sta_.ssid), "%s", ssid);
  }
  return esp_wifi_connect() == ESP_OK ? Status::Ok : Status::Io;
}

Status Net::staDisconnect() {
  if (!ready_) return Status::Ok;
  {
    Lock lock(lock_);
    sta_ = StaInfo();
  }
  if (running_ && wantSta_) esp_wifi_disconnect();
  wantSta_ = false;
  return applyMode();
}

void Net::staInfo(StaInfo& out) const {
  {
    Lock lock(lock_);
    out = sta_;
  }
  if (out.state != StaState::Connected) return;
  wifi_ap_record_t ap;
  if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
    out.rssi = ap.rssi;
    out.channel = ap.primary;
  }
}

void Net::staMac(Mac& out) const {
  if (esp_read_mac(out.b, ESP_MAC_WIFI_STA) != ESP_OK) memset(out.b, 0, sizeof(out.b));
}

// ---------------------------------------------------------------- access point

Status Net::apStart(const char* ssid, const char* password, Ip4 ip, uint8_t channel) {
  if (!ready_) return Status::Denied;
  if (!ssid || !*ssid || strlen(ssid) > 32) return Status::Invalid;
  const char* key = password ? password : "";
  if (*key && (strlen(key) < 8 || strlen(key) > 63)) return Status::Invalid;

  wantAp_ = true;
  LMP_TRY(applyMode());

  // Address of the device and of the network it gives out. The clients are told to ask the
  // device for the names too: the captive portal depends on it.
  esp_netif_ip_info_t info;
  memset(&info, 0, sizeof(info));
  info.ip = toEsp(ip);
  info.gw = toEsp(ip);
  info.netmask = toEsp(Ip4::of(255, 255, 255, 0));
  esp_netif_dhcps_stop(apNetif_);
  esp_netif_set_ip_info(apNetif_, &info);
  esp_netif_dns_info_t dns;
  memset(&dns, 0, sizeof(dns));
  dns.ip.type = ESP_IPADDR_TYPE_V4;
  dns.ip.u_addr.ip4 = toEsp(ip);
  esp_netif_set_dns_info(apNetif_, ESP_NETIF_DNS_MAIN, &dns);
  uint8_t offer = 1;
  esp_netif_dhcps_option(apNetif_, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER, &offer,
                         sizeof(offer));
  esp_netif_dhcps_start(apNetif_);

  wifi_config_t config;
  memset(&config, 0, sizeof(config));
  memcpy(config.ap.ssid, ssid, strlen(ssid));
  config.ap.ssid_len = static_cast<uint8_t>(strlen(ssid));
  memcpy(config.ap.password, key, strlen(key));
  config.ap.channel = channel;
  config.ap.authmode = *key ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
  config.ap.max_connection = kApClients;
  if (esp_wifi_set_config(WIFI_IF_AP, &config) != ESP_OK) {
    wantAp_ = false;
    applyMode();
    return Status::Invalid;
  }

  Lock lock(lock_);
  ap_ = ApInfo();
  ap_.up = true;
  snprintf(ap_.ssid, sizeof(ap_.ssid), "%s", ssid);
  ap_.ip = ip;
  ap_.channel = channel;
  return Status::Ok;
}

Status Net::apStop() {
  captive_ = false;
  wantAp_ = false;
  {
    Lock lock(lock_);
    ap_ = ApInfo();
  }
  return applyMode();
}

void Net::apInfo(ApInfo& out) const {
  {
    Lock lock(lock_);
    out = ap_;
  }
  if (!out.up) return;
  wifi_sta_list_t list;
  memset(&list, 0, sizeof(list));
  if (esp_wifi_ap_get_sta_list(&list) == ESP_OK) out.clients = static_cast<uint8_t>(list.num);
  // With one radio the access point follows the channel of the client.
  uint8_t primary = 0;
  wifi_second_chan_t second;
  if (esp_wifi_get_channel(&primary, &second) == ESP_OK && primary) out.channel = primary;
}

// ---------------------------------------------------------------- scan

Status Net::scanStart() {
  if (!ready_) return Status::Denied;
  if (scan_ == ScanState::Running) return Status::Busy;
  scanning_ = true;
  LMP_TRY(applyMode());

  wifi_scan_config_t config;
  memset(&config, 0, sizeof(config));
  config.show_hidden = false;
  {
    Lock lock(lock_);
    found_ = 0;
    scan_ = ScanState::Running;
  }
  if (esp_wifi_scan_start(&config, false) != ESP_OK) {
    scan_ = ScanState::Failed;
    scanning_ = false;
    modeStale_ = true;
    return Status::Io;
  }
  return Status::Ok;
}

bool Net::scanResult(size_t index, WifiNetwork& out) const {
  Lock lock(lock_);
  if (index >= found_) return false;
  out = records_[index];
  return true;
}

void Net::scanClear() {
  if (scan_ == ScanState::Running) {
    esp_wifi_scan_stop();
    scanning_ = false;
    modeStale_ = true;
  }
  Lock lock(lock_);
  found_ = 0;
  scan_ = ScanState::Idle;
}

// ---------------------------------------------------------------- names

Status Net::mdnsStart(const char* hostname, uint16_t httpPort) {
#if LMP_HAS_MDNS
  if (!ready_ || !hostname || !*hostname) return Status::Invalid;
  if (mdns_) mdnsStop();
  if (mdns_init() != ESP_OK) return Status::Io;
  if (mdns_hostname_set(hostname) != ESP_OK) {
    mdns_free();
    return Status::Io;
  }
  mdns_instance_name_set("LoopMaxPro");
  mdns_service_add(nullptr, "_http", "_tcp", httpPort, nullptr, 0);
  mdns_ = true;
  return Status::Ok;
#else
  (void)hostname;
  (void)httpPort;
  return Status::Unsupported;
#endif
}

void Net::mdnsStop() {
#if LMP_HAS_MDNS
  if (mdns_) mdns_free();
#endif
  mdns_ = false;
  announced_ = false;
}

Status Net::mdnsAnnounce(const char* instance, uint16_t port, const NetText* texts, size_t count) {
#if LMP_HAS_MDNS
  if (!mdns_ || !instance || !*instance) return Status::Denied;
  constexpr size_t kTexts = 12;
  mdns_txt_item_t items[kTexts];
  size_t n = 0;
  for (size_t i = 0; i < count && n < kTexts; ++i) {
    if (!texts[i].key || !texts[i].value) continue;
    items[n].key = texts[i].key;
    items[n].value = texts[i].value;
    ++n;
  }
  // Again, with new data: the service is taken away and put back.
  if (announced_) mdns_service_remove(kPeerService, "_tcp");
  announced_ = mdns_service_add(instance, kPeerService, "_tcp", port, items, n) == ESP_OK;
  return announced_ ? Status::Ok : Status::Io;
#else
  (void)instance;
  (void)port;
  (void)texts;
  (void)count;
  return Status::Unsupported;
#endif
}

// It waits for the answers, timeoutMs long: from the worker, never from the round of the system.
Status Net::peersFind(uint32_t timeoutMs, NetPeer* out, size_t cap, size_t& count) {
  count = 0;
#if LMP_HAS_MDNS
  if (!mdns_ || !out || cap == 0) return Status::Denied;
  mdns_result_t* results = nullptr;
  if (mdns_query_ptr(kPeerService, "_tcp", timeoutMs, cap, &results) != ESP_OK) return Status::Io;
  for (const mdns_result_t* r = results; r && count < cap; r = r->next) {
    NetPeer& peer = out[count];
    peer = NetPeer();
    for (const mdns_ip_addr_t* a = r->addr; a; a = a->next) {
      if (a->addr.type == ESP_IPADDR_TYPE_V4) {
        peer.ip = fromEsp(a->addr.u_addr.ip4);
        break;
      }
    }
    // An answer without an address is of no use: nobody could open that device.
    if (!peer.ip.isSet()) continue;
    if (r->hostname) snprintf(peer.host, sizeof(peer.host), "%s", r->hostname);
    peer.port = r->port;
    for (size_t i = 0; i < r->txt_count; ++i) peerTake(peer, r->txt[i].key, r->txt[i].value);
    ++count;
  }
  mdns_query_results_free(results);
  return Status::Ok;
#else
  (void)timeoutMs;
  (void)out;
  (void)cap;
  return Status::Unsupported;
#endif
}

// ---------------------------------------------------------------- captive portal

Status Net::captiveStart(Ip4 address) {
  if (!ready_) return Status::Denied;
  captiveAddress_ = address;
  if (!dnsTask_) {
    // Created once: it sleeps while the captive portal is off.
    if (xTaskCreate(&Net::dnsTask, "lmp_dns", 3072, this, 3, &dnsTask_) != pdPASS) {
      dnsTask_ = nullptr;
      return Status::NoMem;
    }
  }
  captive_ = true;
  return Status::Ok;
}

void Net::dnsTask(void* self) {
  Net* net = static_cast<Net*>(self);
  for (;;) {
    if (net->captive_) {
      net->answerDns();
    } else {
      vTaskDelay(pdMS_TO_TICKS(500));
    }
  }
}

// Answers every question about a name with the address of the device, until the captive
// portal is turned off.
void Net::answerDns() {
  const int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (sock < 0) {
    vTaskDelay(pdMS_TO_TICKS(1000));
    return;
  }
  sockaddr_in local;
  memset(&local, 0, sizeof(local));
  local.sin_family = AF_INET;
  local.sin_port = htons(kDnsPort);
  local.sin_addr.s_addr = htonl(INADDR_ANY);
  timeval wait;
  wait.tv_sec = 1;
  wait.tv_usec = 0;
  setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &wait, sizeof(wait));
  if (bind(sock, reinterpret_cast<sockaddr*>(&local), sizeof(local)) != 0) {
    close(sock);
    vTaskDelay(pdMS_TO_TICKS(1000));
    return;
  }

  static uint8_t packet[kDnsPacket];
  while (captive_) {
    sockaddr_in from;
    socklen_t fromLength = sizeof(from);
    const int n = recvfrom(sock, packet, sizeof(packet), 0, reinterpret_cast<sockaddr*>(&from),
                           &fromLength);
    // Header of 12 bytes, then the question: name, type and class.
    if (n < 17) continue;
    const bool query = (packet[2] & 0x80) == 0;
    const uint16_t questions = static_cast<uint16_t>(packet[4] << 8 | packet[5]);
    if (!query || questions == 0) continue;

    int end = 12;
    while (end < n && packet[end] != 0) end += packet[end] + 1;
    end += 5;  // terminator of the name, type, class
    if (end > n) continue;
    const uint16_t type = static_cast<uint16_t>(packet[end - 4] << 8 | packet[end - 3]);
    const bool address = type == 1;  // A: IPv4 address

    packet[2] = 0x84;  // answer, authoritative
    packet[3] = 0x00;  // no error
    packet[4] = 0;
    packet[5] = 1;     // one question, the first
    packet[6] = 0;
    packet[7] = address ? 1 : 0;
    packet[8] = packet[9] = packet[10] = packet[11] = 0;
    int length = end;
    if (address && length + 16 <= static_cast<int>(sizeof(packet))) {
      const uint8_t answer[16] = {
          0xC0, 0x0C,              // the name of the question
          0x00, 0x01, 0x00, 0x01,  // address, Internet
          static_cast<uint8_t>(kDnsTtlSec >> 24), static_cast<uint8_t>(kDnsTtlSec >> 16),
          static_cast<uint8_t>(kDnsTtlSec >> 8), static_cast<uint8_t>(kDnsTtlSec),
          0x00, 0x04,
          captiveAddress_.b[0], captiveAddress_.b[1], captiveAddress_.b[2], captiveAddress_.b[3]};
      memcpy(packet + length, answer, sizeof(answer));
      length += static_cast<int>(sizeof(answer));
    }
    sendto(sock, packet, static_cast<size_t>(length), 0, reinterpret_cast<sockaddr*>(&from),
           fromLength);
  }
  close(sock);
}

}  // namespace lmp::hal::esp32

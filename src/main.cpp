// LoopMaxPro - entry point.
#include "app/loopmax.h"
#include "hal/api/board.h"
#include "lmp_config.h"

#ifndef PIO_UNIT_TESTING

namespace {

lmp::LoopMax* g_app = nullptr;

// Starts the board and the system. Returns false when the system cannot run.
bool startup() {
  lmp::hal::boot();
  const lmp::hal::Board& board = lmp::hal::board();
  board.serial->begin(LMP_SERIAL_BAUD);

  static lmp::LoopMax app(board);
  g_app = &app;
#ifdef LMP_PRODUCT_SERVICE
  app.setProduct(lmp::LMP_PRODUCT_SERVICE());
#endif

  // What is logged from here is printed by the serial service as soon as it starts.
  const lmp::Status status = app.begin();
  if (status != lmp::Status::Ok) {
    board.serial->println("LoopMaxPro: startup failed");
    board.serial->println(lmp::statusName(status));
    return false;
  }
  return true;
}

}  // namespace

// ---------------------------------------------------------------- Arduino
#if defined(LMP_FW_ARDUINO)

#include <Arduino.h>

namespace {
bool g_tickInLoop = false;
}

void setup() {
  if (!startup()) return;
  // With tasks the system has its own; without, it runs inside loop().
  g_tickInLoop = g_app->kernel().startTask() != lmp::Status::Ok;
#if defined(LMP_MCU_ESP32)
  // The task of setup() and loop() would only wait: its stack, 8 KB, goes back to the heap.
  if (!g_tickInLoop) vTaskDelete(nullptr);
#endif
}

void loop() {
  if (g_app && g_tickInLoop) {
    g_app->kernel().tick();
    delay(2);
  } else {
    delay(1000);
  }
}

// ---------------------------------------------------------------- ESP-IDF
#elif defined(LMP_FW_IDF)

extern "C" void app_main() {
  if (!startup()) return;
  // The system runs in its own task and this one ends. Without it, it runs here.
  if (g_app->kernel().startTask() == lmp::Status::Ok) return;
  lmp::hal::IRtos* rtos = lmp::hal::board().rtos;
  for (;;) {
    g_app->kernel().tick();
    rtos->delayMs(2);
  }
}

// ---------------------------------------------------------------- PC
#elif defined(LMP_FW_NATIVE)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <string>
#include <thread>
#include <vector>

#include "hal/native/native_hal.h"
#include "lmp_version.h"

// Usage: program [milliseconds]
// Without arguments runs until the process is stopped. Lines typed on the keyboard
// reach the command line of the system.
int main(int argc, char** argv) {
  const long runMs = argc > 1 ? strtol(argv[1], nullptr, 10) : 0;

  // The simulator answers a browser on this computer, and has some networks around.
  lmp::hal::native::Objects& pc = lmp::hal::native::objects();
  pc.http.setListen(true);
  pc.net.addNetwork("Home", "home-password", -52, 6);
  pc.net.addNetwork("Office", "office-password", -71, 11);
  pc.net.addNetwork("Guests", "", -80, 1);
  // Other devices of the system, for the page that finds them: two that are not in a network
  // yet, and three on the network "Home". The last one says no title, as an older device.
  pc.net.addNetwork("LoopMax-3F2A", "loopmaxap", -48, 1);
  pc.net.addNetwork("LoopMax-00B7", "loopmaxap", -79, 11);
  static const struct {
    const char* host;
    uint8_t last;
    lmp::hal::NetText texts[9];
  } kNear[] = {
      {"garage-radio", 182,
       {{"api", "2"}, {"id", "02:4C:4D:50:00:09"}, {"name", "Garage radio"}, {"code", "demo_w32"},
        {"mod", "espradio,zappy2"}, {"title", "EspRadio"}, {"icon", "\xF0\x9F\x93\xBB"},
        {"fw", LMP_VERSION}, {"board", "esp32dev"}}},
      {"loopmax-8ca4", 101,
       {{"api", "2"}, {"id", "02:4C:4D:50:8C:A4"}, {"code", "demo_s3"},
        {"mod", "meteowifi,rgb"}, {"title", "MeteoWifi"}, {"icon", "\xF0\x9F\x8C\xA4"},
        {"fw", LMP_VERSION}, {"board", "heltec_v3"}}},
      {"kitchen-plugs", 60,
       {{"api", "2"}, {"id", "02:4C:4D:50:00:03"}, {"name", "Kitchen plugs, ground floor"},
        {"mod", "zappy2"}, {"fw", "1.0.6"}, {"board", "esp32c3"}}},
  };
  for (const auto& near : kNear) {
    lmp::hal::NetPeer peer;
    snprintf(peer.host, sizeof(peer.host), "%s", near.host);
    peer.ip = lmp::hal::Ip4::of(192, 168, 1, near.last);
    peer.port = LMP_HTTP_PORT;
    for (const lmp::hal::NetText& text : near.texts) lmp::hal::peerTake(peer, text.key, text.value);
    pc.net.addPeer(peer);
  }
  // The network as the questions of sys.probe see it, for the tools of ExpNet: who is there
  // with which MAC, who answers a ping and on which ports, what is announced.
  {
    using lmp::hal::Ip4;
    struct Someone {
      uint8_t last;
      uint8_t mac[6];
      int32_t pingMs;           // -1: no answer to the ping
      uint16_t ports[3];
    };
    static const Someone kHosts[] = {
        {1, {0x3C, 0xA6, 0x2F, 0x11, 0x22, 0x33}, 1, {80, 443, 0}},      // the router
        {10, {0xDC, 0xA6, 0x32, 0x44, 0x55, 0x66}, 2, {8123, 22, 1883}},  // Home Assistant
        {20, {0x00, 0x11, 0x32, 0xAA, 0xBB, 0xCC}, 1, {445, 5000, 22}},  // a NAS
        {30, {0x3C, 0xA9, 0xF4, 0x01, 0x02, 0x03}, -1, {3389, 445, 0}},  // a PC: no ping
        {40, {0x00, 0x80, 0x77, 0x12, 0x34, 0x56}, 4, {9100, 80, 0}},    // a printer
        {50, {0xF8, 0x8F, 0xCA, 0x0A, 0x0B, 0x0C}, 9, {0, 0, 0}},        // a television
        {77, {0x7A, 0x1E, 0x52, 0x10, 0x20, 0x30}, 35, {0, 0, 0}},       // a phone
        {60, {0x84, 0xCC, 0xA8, 0x00, 0x00, 0x03}, 3, {80, 0, 0}},
        {101, {0x34, 0x85, 0x18, 0x00, 0x8C, 0xA4}, 5, {80, 0, 0}},
        {182, {0x24, 0x0A, 0xC4, 0x00, 0x00, 0x09}, 4, {80, 0, 0}},
    };
    for (const Someone& h : kHosts) {
      const Ip4 ip = Ip4::of(192, 168, 1, h.last);
      pc.probe.addHost(ip, h.mac, h.pingMs);
      for (uint16_t port : h.ports) {
        if (port) pc.probe.openPort(ip, port, 2);
      }
    }
    lmp::hal::MdnsHit hit;
    snprintf(hit.host, sizeof(hit.host), "homeassistant");
    snprintf(hit.instance, sizeof(hit.instance), "Home");
    hit.ip = Ip4::of(192, 168, 1, 10);
    hit.port = 8123;
    pc.probe.addMdns("_home-assistant", "_tcp", hit);
    snprintf(hit.host, sizeof(hit.host), "brother-office");
    snprintf(hit.instance, sizeof(hit.instance), "Brother HL-L2350DW");
    hit.ip = Ip4::of(192, 168, 1, 40);
    hit.port = 631;
    pc.probe.addMdns("_ipp", "_tcp", hit);
    snprintf(hit.host, sizeof(hit.host), "nas");
    snprintf(hit.instance, sizeof(hit.instance), "NAS");
    hit.ip = Ip4::of(192, 168, 1, 20);
    hit.port = 445;
    pc.probe.addMdns("_smb", "_tcp", hit);
    lmp::hal::SsdpHit tv;
    tv.ip = Ip4::of(192, 168, 1, 50);
    snprintf(tv.server, sizeof(tv.server), "Linux/4.9 UPnP/1.0 Chromecast/1.68");
    pc.probe.addSsdp(tv);
    // Internet: who answers there, the way to it, the name server of the router.
    pc.probe.addHost(Ip4::of(1, 1, 1, 1), nullptr, 14);
    pc.probe.addHost(Ip4::of(93, 184, 216, 34), nullptr, 27);
    pc.probe.setRoute({Ip4::of(192, 168, 1, 1), Ip4::of(100, 64, 0, 1), Ip4(), Ip4::of(151, 6, 2, 41)});
    pc.probe.onUdp([](Ip4, uint16_t port, const std::vector<uint8_t>& q) {
      // Every name is example.com: one address, for the questions of kind A.
      if (port != 53 || q.size() < 17) return std::vector<uint8_t>();
      std::vector<uint8_t> a(q);
      a[2] = 0x81;
      a[3] = 0x80;
      const bool kindA = q[q.size() - 4] == 0 && q[q.size() - 3] == 1;
      a[7] = kindA ? 1 : 0;
      if (kindA) {
        const uint8_t rr[] = {0xC0, 0x0C, 0, 1, 0, 1, 0, 0, 0x0E, 0x10, 0, 4, 93, 184, 216, 34};
        a.insert(a.end(), rr, rr + sizeof(rr));
      }
      return a;
    });
    // The router says its traffic with UPnP; a line of 56 and 21 Mbit/s, busy as a house is.
    lmp::hal::SsdpHit gateway;
    gateway.ip = Ip4::of(192, 168, 1, 1);
    snprintf(gateway.st, sizeof(gateway.st), "urn:schemas-upnp-org:device:InternetGatewayDevice:1");
    snprintf(gateway.location, sizeof(gateway.location), "http://192.168.1.1:38400/description.xml");
    snprintf(gateway.server, sizeof(gateway.server), "Platform 1.0 UPnP/1.0");
    pc.probe.addSsdp(gateway);
    pc.httpClient.when("http://192.168.1.1:38400/description.xml", 200,
                       "<root><device><serviceList><service><serviceType>urn:schemas-upnp-org:service:"
                       "WANCommonInterfaceConfig:1</serviceType><serviceId>urn:upnp-org:serviceId:WANCommonIFC1"
                       "</serviceId><controlURL>/WANCommonInterfaceConfig:1</controlURL></service></serviceList>"
                       "</device></root>");
    pc.httpClient.whenMade("http://192.168.1.1:38400/WANCommonInterfaceConfig:1", 200, [] {
      static const auto start = std::chrono::steady_clock::now();
      const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
      // Bytes so far: a download that swells and calms, an upload that stays low.
      const double rx = s * 2.2e6 + 9e6 * (1 - std::cos(s / 7.0)) + 2e6 * std::sin(s / 2.3);
      const double tx = s * 2.1e5 + 4e5 * (1 - std::cos(s / 5.0));
      char body[400];
      snprintf(body, sizeof(body),
               "<NewTotalBytesReceived>%u</NewTotalBytesReceived><NewTotalBytesSent>%u</NewTotalBytesSent>"
               "<NewTotalPacketsReceived>%u</NewTotalPacketsReceived>"
               "<NewLayer1DownstreamMaxBitRate>56124000</NewLayer1DownstreamMaxBitRate>"
               "<NewLayer1UpstreamMaxBitRate>20983000</NewLayer1UpstreamMaxBitRate>",
               static_cast<unsigned>(static_cast<uint64_t>(rx) & 0xFFFFFFFFu),
               static_cast<unsigned>(static_cast<uint64_t>(tx) & 0xFFFFFFFFu), static_cast<unsigned>(rx / 1400));
      return std::string(body);
    });
  }
  // What there is to listen to and to watch in the house, for the modules that play media: the
  // media server of the NAS with its folders, a folder on its web server, a playlist, a podcast,
  // the list of channels of a television server. The sound and the pictures are asked by the
  // browser to this computer, port 8201: a helper gives them when one runs there, and without
  // it the tracks are listed and do not play. The films are public test films on the Internet.
  {
    lmp::hal::SsdpHit media;
    media.ip = lmp::hal::Ip4::of(192, 168, 1, 20);
    snprintf(media.st, sizeof(media.st), "urn:schemas-upnp-org:device:MediaServer:1");
    snprintf(media.location, sizeof(media.location), "http://192.168.1.20:8200/rootDesc.xml");
    snprintf(media.server, sizeof(media.server), "Linux UPnP/1.0 MiniDLNA/1.3.0");
    pc.probe.addSsdp(media);
    pc.httpClient.when("http://192.168.1.20:8200/rootDesc.xml", 200,
                       "<?xml version=\"1.0\"?><root xmlns=\"urn:schemas-upnp-org:device-1-0\"><device>"
                       "<deviceType>urn:schemas-upnp-org:device:MediaServer:1</deviceType><friendlyName>Home NAS</friendlyName>"
                       "<serviceList><service><serviceType>urn:schemas-upnp-org:service:ContentDirectory:1</serviceType>"
                       "<controlURL>/ctl/ContentDir</controlURL></service></serviceList></device></root>");
    pc.httpClient.whenAsked("http://192.168.1.20:8200/ctl/ContentDir", 200, [](const lmp::hal::native::HttpSeen& asked) {
      const auto value = [&asked](const char* tag) {
        const std::string open = std::string("<") + tag + ">";
        const size_t at = asked.body.find(open);
        const size_t end = at == std::string::npos ? at : asked.body.find("</", at);
        return end == std::string::npos ? std::string() : asked.body.substr(at + open.size(), end - at - open.size());
      };
      const std::string id = value("ObjectID");
      const int from = atoi(value("StartingIndex").c_str());
      const int count = atoi(value("RequestedCount").c_str());
      static const char* const kAlbums[] = {"Dire Straits - Brothers in Arms", "Pink Floyd - The Wall", "The Beatles - Abbey Road",
                                            "Miles Davis - Kind of Blue"};
      std::vector<std::string> all;
      char one[800];
      if (id == "0") {
        all.push_back("<container id=\"1\" parentID=\"0\" childCount=\"4\"><dc:title>Music</dc:title>"
                      "<upnp:class>object.container.storageFolder</upnp:class></container>");
        all.push_back("<container id=\"2\" parentID=\"0\" childCount=\"90\"><dc:title>All tracks</dc:title>"
                      "<upnp:class>object.container</upnp:class></container>");
        all.push_back("<container id=\"3\" parentID=\"0\" childCount=\"2\"><dc:title>Video &amp; photos</dc:title>"
                      "<upnp:class>object.container</upnp:class></container>");
      } else if (id == "3") {
        all.push_back("<item id=\"3$0\" parentID=\"3\"><dc:title>A flower</dc:title><upnp:class>object.item.videoItem</upnp:class>"
                      "<res duration=\"0:00:05.000\" protocolInfo=\"http-get:*:video/mp4:*\">"
                      "https://interactive-examples.mdn.mozilla.net/media/cc0-videos/flower.mp4</res></item>");
        all.push_back("<item id=\"3$1\" parentID=\"3\"><dc:title>The rabbit</dc:title><upnp:class>object.item.videoItem</upnp:class>"
                      "<res protocolInfo=\"http-get:*:application/vnd.apple.mpegurl:*\">https://test-streams.mux.dev/x36xhzz/x36xhzz.m3u8</res></item>");
        all.push_back("<item id=\"3$2\" parentID=\"3\"><dc:title>A photo</dc:title><upnp:class>object.item.imageItem.photo</upnp:class>"
                      "<res protocolInfo=\"http-get:*:image/jpeg:*\">http://192.168.1.20:8200/MediaItems/1.jpg</res></item>");
      } else if (id == "1") {
        for (int a = 0; a < 4; ++a) {
          snprintf(one, sizeof(one), "<container id=\"1$%d\" parentID=\"1\" childCount=\"8\"><dc:title>%s</dc:title>"
                   "<upnp:class>object.container.album.musicAlbum</upnp:class></container>", a, kAlbums[a]);
          all.push_back(one);
        }
      } else if (id == "2" || id.compare(0, 2, "1$") == 0) {
        const int album = id == "2" ? -1 : atoi(id.c_str() + 2);
        const int tracks = album < 0 ? 90 : 8;
        for (int t = 0; t < tracks; ++t) {
          const int of = album < 0 ? t % 4 : album;
          // The tracks of the first album let a page read their sound, the others do not.
          snprintf(one, sizeof(one),
                   "<item id=\"%s$%d\" parentID=\"%s\"><dc:title>Track %02d</dc:title><upnp:artist>%.*s</upnp:artist>"
                   "<upnp:class>object.item.audioItem.musicTrack</upnp:class>"
                   "<upnp:albumArtURI>http://127.0.0.1:8201/art/%d.svg</upnp:albumArtURI>"
                   "<res duration=\"0:01:00.000\" protocolInfo=\"http-get:*:audio/wav:*\">"
                   "http://127.0.0.1:8201/tone/%d.wav?album=%d&amp;cors=%d</res></item>",
                   id.c_str(), t, id.c_str(), t + 1, static_cast<int>(strcspn(kAlbums[of], "-")) - 1, kAlbums[of], of,
                   200 + of * 40 + t * 5, of, of == 0 ? 1 : 0);
          all.push_back(one);
        }
      }
      // The list is XML written inside XML: its marks are written out.
      std::string didl = "&lt;DIDL-Lite xmlns:dc=\"http://purl.org/dc/elements/1.1/\"&gt;";
      for (int i = from; i < static_cast<int>(all.size()) && i < from + (count ? count : 1000); ++i) {
        for (char c : all[static_cast<size_t>(i)]) {
          didl += c == '<' ? "&lt;" : c == '>' ? "&gt;" : c == '"' ? "&quot;" : c == '&' ? "&amp;" : std::string(1, c);
        }
      }
      return "<s:Envelope><s:Body><u:BrowseResponse><Result>" + didl + "&lt;/DIDL-Lite&gt;</Result><TotalMatches>" +
             std::to_string(all.size()) + "</TotalMatches></u:BrowseResponse></s:Body></s:Envelope>";
    });
    pc.httpClient.when("http://192.168.1.20/music/", 200,
                       "<html><head><title>Index of /music</title></head><body><h1>Index of /music</h1><pre>"
                       "<a href=\"?C=N;O=D\">Name</a> <a href=\"/\">Parent Directory</a>\n"
                       "<a href=\"Evening/\">Evening/</a>\n<a href=\"playlist.m3u\">playlist.m3u</a>\n"
                       "<a href=\"http://127.0.0.1:8201/tone/440.wav?cors=1\">A 440.wav</a>\n"
                       "<a href=\"http://127.0.0.1:8201/tone/660.wav\">E 660.wav</a>\n<a href=\"note.txt\">note.txt</a></pre></body></html>");
    pc.httpClient.when("http://192.168.1.20/video/", 200,
                       "<html><body><a href=\"../\">../</a>"
                       "<a href=\"https://interactive-examples.mdn.mozilla.net/media/cc0-videos/flower.mp4\">A flower.mp4</a>"
                       "<a href=\"song.mp3\">song.mp3</a></body></html>");
    // A television server, as TVHeadend gives its channels: a page of the browser cannot read it,
    // the board does.
    pc.httpClient.when("http://192.168.1.9:9981/playlist/channels.m3u", 200,
                       "#EXTM3U\n"
                       "#EXTINF:-1 tvg-logo=\"http://127.0.0.1:8201/art/1.svg\" group-title=\"Tests\",The rabbit (HLS)\n"
                       "https://test-streams.mux.dev/x36xhzz/x36xhzz.m3u8\n"
                       "#EXTINF:-1 tvg-logo=\"http://127.0.0.1:8201/art/2.svg\" group-title=\"Tests\",A flower (MP4)\n"
                       "https://interactive-examples.mdn.mozilla.net/media/cc0-videos/flower.mp4\n"
                       "#EXTINF:-1 tvg-logo=\"http://127.0.0.1:8201/art/3.svg\" group-title=\"Tests\",A piece (MPEG-TS)\n"
                       "https://test-streams.mux.dev/x36xhzz/url_0/url_462/193039199_mp4_h264_aac_hd_7.ts\n"
                       "#EXTINF:-1 group-title=\"Off\",A channel that is not there\nhttp://192.168.1.9:9981/stream/channelid/404\n"
                       "#EXTINF:-1 group-title=\"Off\",Another channel that is off\nhttp://192.168.1.9:9981/stream/channelid/405\n");
    // The pass of Spotify, for the page that asks one: no real account is behind it.
    pc.httpClient.when("https://accounts.spotify.com/api/token", 200,
                       "{\"access_token\":\"pass-of-the-simulator\",\"token_type\":\"Bearer\",\"expires_in\":3600}");
    pc.httpClient.when("http://192.168.1.20/music/Evening/", 200,
                       "<html><body><a href=\"../\">../</a><a href=\"http://127.0.0.1:8201/tone/330.wav?cors=1\">Nocturne.wav</a></body></html>");
    pc.httpClient.when("http://192.168.1.20/music/playlist.m3u", 200,
                       "#EXTM3U\n#EXTINF:60,Test - C\nhttp://127.0.0.1:8201/tone/262.wav?cors=1\n"
                       "#EXTINF:60,Test - G\nhttp://127.0.0.1:8201/tone/392.wav?cors=1\n");
    // A podcast of a hundred and twenty episodes of twelve minutes: at an address of the NAS, and
    // at the ones of a real host of podcasts, for the ones found by their name.
    const auto feed = [] {
      std::string feed = "<?xml version=\"1.0\"?><rss version=\"2.0\"><channel><title>The test podcast</title>"
                         "<description>A story a week</description><itunes:image href=\"http://127.0.0.1:8201/art/9.svg\"/>";
      char one[500];
      for (int e = 120; e > 0; --e) {
        snprintf(one, sizeof(one),
                 "<item><title>Episode %d</title><description><![CDATA[<p>What we talk about in episode %d.</p>]]></description>"
                 "<pubDate>Mon, %02d Oct 2026 05:00:00 +0200</pubDate><itunes:duration>00:12:00</itunes:duration>"
                 "<enclosure url=\"http://127.0.0.1:8201/tone/%d.wav?long=1&amp;cors=1\" type=\"audio/wav\"/></item>",
                 e, e, 1 + e % 28, 200 + e);
        feed += one;
      }
      return feed + "</channel></rss>";
    };
    pc.httpClient.whenMade("http://192.168.1.20/podcast.xml", 200, feed);
    pc.httpClient.whenMade("https://www.spreaker.com/*", 200, feed);
  }
  // The time server answers with the time of this computer, and Internet is reachable.
  pc.ntp.setAuto(true);
  pc.httpClient.when("http://connectivitycheck.gstatic.com/generate_204", 204, "");
  pc.i2c[0].addDevice(0x3C);

  if (!startup()) return 1;

  std::thread([] {
    char line[256];
    while (fgets(line, sizeof(line), stdin)) {
      lmp::hal::native::objects().serial.feedInput(line);
    }
  }).detach();

  lmp::Kernel& kernel = g_app->kernel();
  if (runMs > 0) {
    std::thread([&kernel, runMs] {
      std::this_thread::sleep_for(std::chrono::milliseconds(runMs));
      kernel.stop();
    }).detach();
  }
  kernel.run();
  kernel.shutdown();
  fflush(stdout);
  return 0;
}

#else
#error "No framework selected: define LMP_FW_ARDUINO, LMP_FW_IDF or LMP_FW_NATIVE"
#endif

#endif  // PIO_UNIT_TESTING

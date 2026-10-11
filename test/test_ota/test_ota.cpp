// Tests on the PC of the firmware update: the fingerprint, an image that arrives in pieces
// from a file or from an address, the trial of a new firmware, the files of the console.
#include <fstream>
#include <sstream>

#include "../support/online_system.h"

using namespace lmp;
using namespace lmp::test;
using lmp::hal::native::HttpCall;
using lmp::hal::native::HttpResult;

void setUp() {}
void tearDown() { Logger::setInstance(nullptr); }

namespace {

constexpr const char* kImageUrl = "https://updates.test/fw/loopmax.bin";
constexpr const char* kPassword = "loopmaxap";

// An image: not a real firmware, the simulated board accepts anything.
std::string image(size_t bytes) {
  std::string out(bytes, '\0');
  for (size_t i = 0; i < bytes; ++i) out[i] = static_cast<char>((i * 31 + 7) & 0xFF);
  return out;
}

std::string fingerprint(const std::string& data) {
  Sha256 hash;
  hash.update(reinterpret_cast<const uint8_t*>(data.data()), data.size());
  uint8_t digest[Sha256::kSize];
  hash.finish(digest);
  char text[Sha256::kSize * 2 + 1];
  sha256Hex(digest, text);
  return text;
}

HttpResult upload(OnlineSystem& sys, const std::string& target, const std::string& body,
                  const char* token = nullptr, size_t dropAfter = static_cast<size_t>(-1)) {
  HttpCall call;
  call.method = "POST";
  call.target = target;
  call.body = body;
  call.contentType = "application/octet-stream";
  call.dropAfter = dropAfter;
  if (token) call.headers.emplace_back("Authorization", std::string("Bearer ") + token);
  return sys.pc().http.request(call);
}

std::string login(OnlineSystem& sys) {
  const std::string body = std::string("{\"password\":\"") + kPassword + "\"}";
  const HttpResult r = sys.pc().http.post("/api/auth/login", body.c_str());
  if (r.status != 200) return std::string();
  char token[40] = "";
  JsonValue::parse(StringView(r.body.data(), r.body.size()))["token"].getString(token, sizeof(token));
  return token;
}

std::string fileContent(OnlineSystem& sys, const char* path) {
  std::ifstream in(sys.pc().fs.root() + path, std::ios::binary);
  std::ostringstream all;
  all << in.rdbuf();
  return all.str();
}

struct Body {
  std::string text;
  JsonValue v;
  explicit Body(const HttpResult& r) : text(r.body) {
    v = JsonValue::parse(StringView(text.data(), text.size()));
  }
};

}  // namespace

// ---------------------------------------------------------------- fingerprint

void test_fingerprints_of_known_texts() {
  TEST_ASSERT_EQUAL_STRING("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
                           fingerprint("").c_str());
  TEST_ASSERT_EQUAL_STRING("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
                           fingerprint("abc").c_str());
  TEST_ASSERT_EQUAL_STRING(
      "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
      fingerprint("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq").c_str());
  TEST_ASSERT_EQUAL_STRING("cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0",
                           fingerprint(std::string(1000000, 'a')).c_str());
}

void test_a_fingerprint_does_not_depend_on_the_pieces() {
  const std::string data = image(5000);
  const std::string whole = fingerprint(data);
  const size_t steps[] = {1, 7, 55, 56, 63, 64, 65, 119, 120, 1000};
  for (size_t step : steps) {
    Sha256 hash;
    for (size_t at = 0; at < data.size(); at += step) {
      const size_t n = data.size() - at < step ? data.size() - at : step;
      hash.update(reinterpret_cast<const uint8_t*>(data.data()) + at, n);
    }
    uint8_t digest[Sha256::kSize];
    hash.finish(digest);
    char text[Sha256::kSize * 2 + 1];
    sha256Hex(digest, text);
    TEST_ASSERT_EQUAL_STRING(whole.c_str(), text);
  }
}

void test_fingerprints_are_read_strictly() {
  uint8_t out[Sha256::kSize];
  TEST_ASSERT_TRUE(sha256Parse(
      StringView("BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD"), out));
  TEST_ASSERT_EQUAL_HEX8(0xBA, out[0]);
  TEST_ASSERT_EQUAL_HEX8(0xAD, out[31]);
  TEST_ASSERT_FALSE(sha256Parse(StringView(""), out));
  TEST_ASSERT_FALSE(sha256Parse(StringView("ba7816bf"), out));
  TEST_ASSERT_FALSE(sha256Parse(
      StringView("ga7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"), out));
  TEST_ASSERT_FALSE(sha256Parse(
      StringView("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad00"), out));
}

// ---------------------------------------------------------------- an image, piece by piece

void test_an_image_is_installed() {
  OnlineSystem sys(false);
  const std::string fw = image(40000);
  const std::string sha = fingerprint(fw);
  IOtaService* ota = sys.kernel().api<IOtaService>();
  TEST_ASSERT_NOT_NULL(ota);
  TEST_ASSERT_TRUE(sys.stateOf(ServiceId::Ota) == ServiceState::Ready);
  TEST_ASSERT_TRUE(ota->otaState() == OtaState::Idle);

  TEST_ASSERT_TRUE(ota->begin(hal::OtaTarget::App, fw.size(), StringView(sha.c_str()),
                              OtaOrigin::File) == Status::Ok);
  TEST_ASSERT_TRUE(ota->otaState() == OtaState::Receiving);
  // One image at a time.
  TEST_ASSERT_TRUE(ota->begin(hal::OtaTarget::App, 10, StringView(), OtaOrigin::File) ==
                   Status::Busy);

  for (size_t at = 0; at < fw.size(); at += 4000) {
    TEST_ASSERT_TRUE(ota->write(reinterpret_cast<const uint8_t*>(fw.data()) + at, 4000) ==
                     Status::Ok);
    sys.advance(300);
  }
  TEST_ASSERT_EQUAL_UINT(fw.size(), ota->received());
  Doc half(sys.kernel(), "sys.ota");
  TEST_ASSERT_TRUE(half.v["state"].stringEquals("receiving"));
  TEST_ASSERT_TRUE(half.v["target"].stringEquals("app"));
  TEST_ASSERT_TRUE(half.v["from"].stringEquals("file"));
  TEST_ASSERT_EQUAL_INT64(100, half.v["percent"].asInt());

  // The progress was told along the way, not at every piece.
  const int steps = sys.count(EventId::OtaProgress);
  TEST_ASSERT_TRUE(steps >= 9 && steps <= 11);
  Event last;
  sys.count(EventId::OtaProgress, &last);
  TEST_ASSERT_EQUAL_UINT8(100, last.b);
  TEST_ASSERT_EQUAL_UINT32(fw.size(), last.value);

  TEST_ASSERT_FALSE(sys.kernel().restartPending());
  TEST_ASSERT_TRUE(ota->finish() == Status::Ok);
  TEST_ASSERT_TRUE(ota->otaState() == OtaState::Installed);
  TEST_ASSERT_EQUAL_UINT(1, sys.pc().ota.installs());
  TEST_ASSERT_TRUE(sys.pc().ota.installed() == fw);
  TEST_ASSERT_TRUE(sys.pc().ota.installedTarget() == hal::OtaTarget::App);
  TEST_ASSERT_EQUAL_UINT(fw.size(), sys.pc().ota.declared());

  // The device restarts by itself, a moment later.
  sys.advance(300);
  TEST_ASSERT_TRUE(sys.kernel().restartPending());
  Event done;
  TEST_ASSERT_EQUAL_INT(1, sys.count(EventId::OtaDone, &done));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Status::Ok), done.b);
  TEST_ASSERT_EQUAL_UINT(0, sys.pc().sys.restartCount());
  sys.advance(2000);
  TEST_ASSERT_EQUAL_UINT(1, sys.pc().sys.restartCount());

  Doc doc(sys.kernel(), "sys.ota");
  TEST_ASSERT_TRUE(doc.v["state"].stringEquals("installed"));
  TEST_ASSERT_TRUE(doc.v["sha256"].stringEquals(sha.c_str()));
  TEST_ASSERT_EQUAL_INT64(1, doc.v["installs"].asInt());
}

void test_an_image_with_the_wrong_fingerprint() {
  OnlineSystem sys(false);
  const std::string fw = image(3000);
  const std::string other = fingerprint(image(3001));
  IOtaService* ota = sys.kernel().api<IOtaService>();

  TEST_ASSERT_TRUE(ota->begin(hal::OtaTarget::App, fw.size(), StringView(other.c_str()),
                              OtaOrigin::File) == Status::Ok);
  TEST_ASSERT_TRUE(ota->write(reinterpret_cast<const uint8_t*>(fw.data()), fw.size()) ==
                   Status::Ok);
  TEST_ASSERT_TRUE(ota->finish() == Status::Invalid);
  TEST_ASSERT_TRUE(ota->otaState() == OtaState::Failed);
  TEST_ASSERT_EQUAL_UINT(0, sys.pc().ota.installs());
  TEST_ASSERT_EQUAL_UINT(1, sys.pc().ota.aborts());
  sys.advance(3000);
  TEST_ASSERT_FALSE(sys.kernel().restartPending());
  Event done;
  TEST_ASSERT_EQUAL_INT(1, sys.count(EventId::OtaDone, &done));
  TEST_ASSERT_TRUE(done.b != static_cast<uint8_t>(Status::Ok));
  Doc doc(sys.kernel(), "sys.ota");
  TEST_ASSERT_TRUE(doc.v["state"].stringEquals("failed"));
  TEST_ASSERT_TRUE(doc.v["error"].stringEquals("fingerprint does not match"));
  TEST_ASSERT_EQUAL_INT64(1, doc.v["failures"].asInt());
  TEST_ASSERT_TRUE(sys.logHas("fingerprint does not match"));

  // A fingerprint that is not one is refused before anything is written.
  TEST_ASSERT_TRUE(ota->begin(hal::OtaTarget::App, fw.size(), StringView("abc"),
                              OtaOrigin::File) == Status::Invalid);
  // After a failure another image can come. Without fingerprint nothing is compared.
  TEST_ASSERT_TRUE(ota->begin(hal::OtaTarget::App, fw.size(), StringView(), OtaOrigin::File) ==
                   Status::Ok);
  TEST_ASSERT_TRUE(ota->write(reinterpret_cast<const uint8_t*>(fw.data()), fw.size()) ==
                   Status::Ok);
  TEST_ASSERT_TRUE(ota->finish() == Status::Ok);
  TEST_ASSERT_TRUE(sys.pc().ota.installed() == fw);
}

void test_an_image_of_the_wrong_length() {
  OnlineSystem sys(false);
  const std::string fw = image(3000);
  IOtaService* ota = sys.kernel().api<IOtaService>();
  const uint8_t* data = reinterpret_cast<const uint8_t*>(fw.data());

  // Shorter than declared.
  TEST_ASSERT_TRUE(ota->begin(hal::OtaTarget::App, 3000, StringView(), OtaOrigin::File) ==
                   Status::Ok);
  TEST_ASSERT_TRUE(ota->write(data, 2000) == Status::Ok);
  TEST_ASSERT_TRUE(ota->finish() == Status::Invalid);
  TEST_ASSERT_TRUE(ota->otaState() == OtaState::Failed);

  // Longer than declared.
  TEST_ASSERT_TRUE(ota->begin(hal::OtaTarget::App, 2000, StringView(), OtaOrigin::File) ==
                   Status::Ok);
  TEST_ASSERT_TRUE(ota->write(data, 1500) == Status::Ok);
  TEST_ASSERT_TRUE(ota->write(data, 1500) == Status::Invalid);
  TEST_ASSERT_TRUE(ota->otaState() == OtaState::Failed);
  TEST_ASSERT_TRUE(ota->write(data, 10) == Status::Denied);
  TEST_ASSERT_TRUE(ota->finish() == Status::Denied);

  // Empty.
  TEST_ASSERT_TRUE(ota->begin(hal::OtaTarget::App, 0, StringView(), OtaOrigin::File) ==
                   Status::Ok);
  TEST_ASSERT_TRUE(ota->finish() == Status::Invalid);

  // Length not known: what arrives is the image.
  TEST_ASSERT_TRUE(ota->begin(hal::OtaTarget::App, 0, StringView(), OtaOrigin::File) ==
                   Status::Ok);
  TEST_ASSERT_TRUE(ota->write(data, 3000) == Status::Ok);
  TEST_ASSERT_TRUE(ota->finish() == Status::Ok);
  TEST_ASSERT_EQUAL_UINT(1, sys.pc().ota.installs());
  TEST_ASSERT_EQUAL_UINT(3, sys.pc().ota.aborts());
}

void test_a_board_that_cannot_write_the_image() {
  OnlineSystem sys(false);
  const std::string fw = image(3000);
  IOtaService* ota = sys.kernel().api<IOtaService>();
  const uint8_t* data = reinterpret_cast<const uint8_t*>(fw.data());

  sys.pc().ota.failBegin(Status::NoMem);
  TEST_ASSERT_TRUE(ota->begin(hal::OtaTarget::App, 3000, StringView(), OtaOrigin::File) ==
                   Status::NoMem);
  TEST_ASSERT_TRUE(ota->otaState() == OtaState::Failed);
  Doc large(sys.kernel(), "sys.ota");
  TEST_ASSERT_TRUE(large.v["error"].stringEquals("image too large"));

  sys.pc().ota.failBegin(Status::Ok);
  sys.pc().ota.failWriteAfter(2000);
  TEST_ASSERT_TRUE(ota->begin(hal::OtaTarget::App, 3000, StringView(), OtaOrigin::File) ==
                   Status::Ok);
  TEST_ASSERT_TRUE(ota->write(data, 1500) == Status::Ok);
  TEST_ASSERT_TRUE(ota->write(data + 1500, 1500) == Status::Io);
  TEST_ASSERT_TRUE(ota->otaState() == OtaState::Failed);
  Doc written(sys.kernel(), "sys.ota");
  TEST_ASSERT_TRUE(written.v["error"].stringEquals("write error"));

  // The board looks at the image when it is whole, and does not like it.
  sys.pc().ota.failWriteAfter(static_cast<size_t>(-1));
  sys.pc().ota.failEnd(Status::Invalid);
  TEST_ASSERT_TRUE(ota->begin(hal::OtaTarget::App, 3000, StringView(), OtaOrigin::File) ==
                   Status::Ok);
  TEST_ASSERT_TRUE(ota->write(data, 3000) == Status::Ok);
  TEST_ASSERT_TRUE(ota->finish() == Status::Invalid);
  Doc checked(sys.kernel(), "sys.ota");
  TEST_ASSERT_TRUE(checked.v["error"].stringEquals("image not valid"));
  sys.advance(3000);
  TEST_ASSERT_FALSE(sys.kernel().restartPending());
}

void test_an_image_given_up_by_command() {
  OnlineSystem sys(false);
  IOtaService* ota = sys.kernel().api<IOtaService>();
  TEST_ASSERT_TRUE(sys.command("sys.ota", "abort", "{}") == Status::Denied);
  TEST_ASSERT_TRUE(ota->begin(hal::OtaTarget::Filesystem, 0, StringView(), OtaOrigin::File) ==
                   Status::Ok);
  Doc doc(sys.kernel(), "sys.ota");
  TEST_ASSERT_TRUE(doc.v["target"].stringEquals("fs"));
  TEST_ASSERT_TRUE(sys.command("sys.ota", "abort", "{}") == Status::Ok);
  TEST_ASSERT_TRUE(ota->otaState() == OtaState::Failed);
  TEST_ASSERT_EQUAL_UINT(1, sys.pc().ota.aborts());
}

void test_a_board_without_update() {
  TestBoard tb;
  tb.board.ota = nullptr;
  LoopMax app(tb.board);
  Logger::setInstance(&app.kernel().log());
  TEST_ASSERT_TRUE(app.begin() == Status::Ok);
  run(app.kernel(), tb.clock, 500);
  Service* s = app.kernel().services().find(ServiceId::Ota);
  TEST_ASSERT_FALSE(s->running());
  TEST_ASSERT_EQUAL_STRING("no firmware update on this board", s->reason());
  IOtaService* ota = app.kernel().api<IOtaService>();
  TEST_ASSERT_TRUE(ota->begin(hal::OtaTarget::App, 10, StringView(), OtaOrigin::File) ==
                   Status::Denied);
  // The rest of the system is there.
  TEST_ASSERT_TRUE(app.web().running());
}

// ---------------------------------------------------------------- from a file of the console

void test_a_firmware_sent_by_the_console() {
  OnlineSystem sys(false);
  const std::string fw = image(10000);
  const std::string sha = fingerprint(fw);
  sys.pc().http.setUploadChunk(1500);

  const HttpResult r = upload(sys, "/api/upload/ota?target=app&sha256=" + sha, fw);
  TEST_ASSERT_EQUAL_UINT16(200, r.status);
  TEST_ASSERT_EQUAL_STRING("application/json", r.contentType.c_str());
  TEST_ASSERT_EQUAL_STRING("2", r.header("X-LMP-Api").c_str());
  Body b(r);
  TEST_ASSERT_TRUE(b.v["ok"].asBool());
  TEST_ASSERT_EQUAL_INT64(10000, b.v["result"]["bytes"].asInt());
  TEST_ASSERT_TRUE(b.v["result"]["sha256"].stringEquals(sha.c_str()));
  TEST_ASSERT_TRUE(b.v["result"]["restart"].asBool());
  TEST_ASSERT_TRUE(sys.pc().ota.installed() == fw);
  TEST_ASSERT_EQUAL_UINT(10000, sys.pc().ota.declared());

  sys.advance(300);
  TEST_ASSERT_TRUE(sys.kernel().restartPending());
  Doc web(sys.kernel(), "sys.web");
  TEST_ASSERT_EQUAL_INT64(1, web.v["uploads"].asInt());
}

void test_a_filesystem_sent_by_the_console() {
  OnlineSystem sys(false);
  const std::string fs = image(6000);
  const HttpResult r = upload(sys, "/api/upload/ota?target=fs", fs);
  TEST_ASSERT_EQUAL_UINT16(200, r.status);
  TEST_ASSERT_TRUE(sys.pc().ota.installedTarget() == hal::OtaTarget::Filesystem);
  TEST_ASSERT_TRUE(sys.pc().ota.installed() == fs);
  Body b(r);
  TEST_ASSERT_TRUE(b.v["result"]["sha256"].stringEquals(fingerprint(fs).c_str()));
}

void test_a_firmware_that_is_refused() {
  OnlineSystem sys(false);
  const std::string fw = image(5000);

  HttpResult r = upload(sys, "/api/upload/ota?target=kernel", fw);
  TEST_ASSERT_EQUAL_UINT16(400, r.status);
  Body target(r);
  TEST_ASSERT_TRUE(target.v["error"].stringEquals("invalid"));
  TEST_ASSERT_TRUE(target.v["field"].stringEquals("target"));

  r = upload(sys, "/api/upload/ota?sha256=abc", fw);
  TEST_ASSERT_EQUAL_UINT16(400, r.status);
  Body sha(r);
  TEST_ASSERT_TRUE(sha.v["field"].stringEquals("sha256"));

  r = upload(sys, "/api/upload/ota?sha256=" + fingerprint(image(10)), fw);
  TEST_ASSERT_EQUAL_UINT16(400, r.status);
  Body wrong(r);
  TEST_ASSERT_TRUE(wrong.v["msg"].stringEquals("the image is not valid"));

  r = upload(sys, "/api/upload/ota", "");
  TEST_ASSERT_EQUAL_UINT16(400, r.status);

  sys.pc().ota.failWriteAfter(2000);
  r = upload(sys, "/api/upload/ota", fw);
  TEST_ASSERT_EQUAL_UINT16(500, r.status);
  Body io(r);
  TEST_ASSERT_TRUE(io.v["error"].stringEquals("io"));
  sys.pc().ota.failWriteAfter(static_cast<size_t>(-1));

  TEST_ASSERT_EQUAL_UINT(0, sys.pc().ota.installs());
  sys.advance(3000);
  TEST_ASSERT_FALSE(sys.kernel().restartPending());

  // A client that goes away half way.
  r = upload(sys, "/api/upload/ota", fw, nullptr, 3000);
  TEST_ASSERT_EQUAL_UINT16(400, r.status);
  Doc doc(sys.kernel(), "sys.ota");
  TEST_ASSERT_TRUE(doc.v["state"].stringEquals("failed"));
  TEST_ASSERT_TRUE(doc.v["error"].stringEquals("transfer interrupted"));

  // After all this a good image is taken.
  r = upload(sys, "/api/upload/ota", fw);
  TEST_ASSERT_EQUAL_UINT16(200, r.status);
  TEST_ASSERT_TRUE(sys.pc().ota.installed() == fw);

  // And while the device is about to restart, nothing else.
  r = upload(sys, "/api/upload/ota", fw);
  TEST_ASSERT_EQUAL_UINT16(409, r.status);
}

void test_a_firmware_needs_a_login() {
  OnlineSystem sys(true);
  const std::string fw = image(5000);
  HttpCall lan;
  lan.method = "POST";
  lan.target = "/api/upload/ota";
  lan.body = fw;
  lan.from = hal::Ip4::of(192, 168, 1, 77);
  HttpResult r = sys.pc().http.request(lan);
  TEST_ASSERT_EQUAL_UINT16(401, r.status);
  Body b(r);
  TEST_ASSERT_TRUE(b.v["error"].stringEquals("auth"));
  // Nothing was started.
  TEST_ASSERT_TRUE(sys.app.ota().otaState() == OtaState::Idle);
  TEST_ASSERT_EQUAL_UINT(0, sys.pc().ota.aborts());

  lan.headers.emplace_back("Authorization", "Bearer 0123456789abcdef0123456789abcdef");
  TEST_ASSERT_EQUAL_UINT16(401, sys.pc().http.request(lan).status);

  const std::string token = login(sys);
  TEST_ASSERT_EQUAL_UINT(32, token.size());
  lan.headers.clear();
  lan.headers.emplace_back("Authorization", "Bearer " + token);
  r = sys.pc().http.request(lan);
  TEST_ASSERT_EQUAL_UINT16(200, r.status);
  TEST_ASSERT_TRUE(sys.pc().ota.installed() == fw);

  // A GET on the same address is not an upload.
  TEST_ASSERT_TRUE(sys.pc().http.get("/api/upload/ota", token.c_str()).status >= 400);
}

// ---------------------------------------------------------------- from an address

void test_a_firmware_from_an_address() {
  OnlineSystem sys;
  const std::string fw = image(30000);
  const std::string sha = fingerprint(fw);
  sys.pc().httpClient.when(kImageUrl, 200, fw);
  sys.pc().httpClient.setChunk(1024);

  const std::string args = std::string("{\"url\":\"") + kImageUrl + "\",\"sha256\":\"" + sha +
                           "\",\"size\":30000}";
  uint32_t ref = 0;
  TEST_ASSERT_TRUE(sys.command("sys.ota", "fromUrl", args.c_str(), nullptr, &ref) ==
                   Status::Pending);
  TEST_ASSERT_TRUE(ref != 0);
  TEST_ASSERT_TRUE(sys.command("sys.ota", "fromUrl", args.c_str()) == Status::Busy);
  Doc going(sys.kernel(), "sys.ota");
  TEST_ASSERT_TRUE(going.v["state"].stringEquals("receiving"));
  TEST_ASSERT_TRUE(going.v["from"].stringEquals("url"));
  TEST_ASSERT_TRUE(going.v["fromUrl"].asBool());

  sys.advance(500);
  Status outcome = Status::Error;
  TEST_ASSERT_TRUE(sys.done(ref, outcome));
  TEST_ASSERT_TRUE(outcome == Status::Ok);
  TEST_ASSERT_TRUE(sys.pc().ota.installed() == fw);
  TEST_ASSERT_TRUE(sys.kernel().restartPending());
  TEST_ASSERT_EQUAL_INT(1, sys.count(EventId::OtaDone));
}

void test_a_download_that_fails() {
  OnlineSystem sys;
  const std::string fw = image(8000);
  const std::string args = std::string("{\"url\":\"") + kImageUrl + "\"}";
  uint32_t ref = 0;
  Status outcome = Status::Ok;

  // The server says the file is not there: its page is not a firmware.
  sys.pc().httpClient.when(kImageUrl, 404, "<html>not found</html>");
  TEST_ASSERT_TRUE(sys.command("sys.ota", "fromUrl", args.c_str(), nullptr, &ref) ==
                   Status::Pending);
  sys.advance(500);
  TEST_ASSERT_TRUE(sys.done(ref, outcome));
  TEST_ASSERT_TRUE(outcome == Status::NotFound);
  TEST_ASSERT_EQUAL_UINT(0, sys.pc().ota.installs());
  Doc refused(sys.kernel(), "sys.ota");
  TEST_ASSERT_TRUE(refused.v["error"].stringEquals("the server refused"));

  // The server is not there.
  sys.pc().httpClient.whenFails(kImageUrl, Status::Timeout);
  TEST_ASSERT_TRUE(sys.command("sys.ota", "fromUrl", args.c_str(), nullptr, &ref) ==
                   Status::Pending);
  sys.advance(500);
  TEST_ASSERT_TRUE(sys.done(ref, outcome));
  TEST_ASSERT_TRUE(outcome == Status::Timeout);

  // The image is not the one expected.
  sys.pc().httpClient.when(kImageUrl, 200, fw);
  const std::string other = std::string("{\"url\":\"") + kImageUrl + "\",\"sha256\":\"" +
                            fingerprint(image(9)) + "\"}";
  TEST_ASSERT_TRUE(sys.command("sys.ota", "fromUrl", other.c_str(), nullptr, &ref) ==
                   Status::Pending);
  sys.advance(500);
  TEST_ASSERT_TRUE(sys.done(ref, outcome));
  TEST_ASSERT_TRUE(outcome == Status::Invalid);

  // The flash gives an error half way: the download stops there.
  sys.pc().ota.failWriteAfter(3000);
  sys.pc().httpClient.setChunk(1000);
  TEST_ASSERT_TRUE(sys.command("sys.ota", "fromUrl", args.c_str(), nullptr, &ref) ==
                   Status::Pending);
  sys.advance(500);
  TEST_ASSERT_TRUE(sys.done(ref, outcome));
  TEST_ASSERT_TRUE(outcome == Status::Io);
  Doc io(sys.kernel(), "sys.ota");
  TEST_ASSERT_TRUE(io.v["error"].stringEquals("write error"));
  TEST_ASSERT_EQUAL_INT64(3000, io.v["received"].asInt());

  TEST_ASSERT_EQUAL_UINT(0, sys.pc().ota.installs());
  sys.advance(3000);
  TEST_ASSERT_FALSE(sys.kernel().restartPending());
  TEST_ASSERT_EQUAL_INT(4, sys.count(EventId::OtaDone));
}

void test_the_arguments_of_a_download() {
  OnlineSystem sys;
  const char* field = nullptr;
  TEST_ASSERT_TRUE(sys.command("sys.ota", "fromUrl", "{}", &field) == Status::Invalid);
  TEST_ASSERT_EQUAL_STRING("url", field);
  TEST_ASSERT_TRUE(sys.command("sys.ota", "fromUrl", "{\"url\":\"ftp://updates.test/fw\"}",
                               &field) == Status::Invalid);
  TEST_ASSERT_EQUAL_STRING("url", field);
  TEST_ASSERT_TRUE(sys.command("sys.ota", "fromUrl",
                               "{\"url\":\"https://updates.test/fw\",\"target\":\"all\"}",
                               &field) == Status::Invalid);
  TEST_ASSERT_EQUAL_STRING("target", field);
  TEST_ASSERT_TRUE(sys.command("sys.ota", "fromUrl",
                               "{\"url\":\"https://updates.test/fw\",\"sha256\":\"abc\"}",
                               &field) == Status::Invalid);
  TEST_ASSERT_EQUAL_STRING("sha256", field);
  TEST_ASSERT_TRUE(sys.app.ota().otaState() == OtaState::Idle);
  TEST_ASSERT_EQUAL_UINT(0, sys.pc().httpClient.count());

  // From the access point there is no way out to a server.
  OnlineSystem ap(false);
  TEST_ASSERT_TRUE(ap.command("sys.ota", "fromUrl", "{\"url\":\"https://updates.test/fw\"}") ==
                   Status::Denied);
  TEST_ASSERT_TRUE(ap.app.ota().otaState() == OtaState::Idle);
  TEST_ASSERT_EQUAL_UINT(0, ap.pc().ota.aborts());
}

// ---------------------------------------------------------------- trial of a new firmware

void test_a_new_firmware_is_confirmed_by_itself() {
  hal::native::objects().ota.reset();
  TestBoard tb;
  tb.native().ota.setPendingVerify(true);
  tb.native().ota.setCanRollback(true);
  LoopMax app(tb.board);
  Logger::setInstance(&app.kernel().log());
  TEST_ASSERT_TRUE(app.begin() == Status::Ok);
  run(app.kernel(), tb.clock, 1000);

  Doc before(app.kernel(), "sys.ota");
  TEST_ASSERT_TRUE(before.v["trial"].asBool());
  TEST_ASSERT_TRUE(before.v["canRollback"].asBool());
  TEST_ASSERT_EQUAL_UINT(0, tb.native().ota.confirms());

  run(app.kernel(), tb.clock, (LMP_OTA_CONFIRM_SEC - 2) * 1000);
  TEST_ASSERT_EQUAL_UINT(0, tb.native().ota.confirms());
  run(app.kernel(), tb.clock, 2000);
  TEST_ASSERT_EQUAL_UINT(1, tb.native().ota.confirms());
  Doc after(app.kernel(), "sys.ota");
  TEST_ASSERT_FALSE(after.v["trial"].asBool(true));
  run(app.kernel(), tb.clock, 120000);
  TEST_ASSERT_EQUAL_UINT(1, tb.native().ota.confirms());
}

void test_a_new_firmware_confirmed_or_given_up_by_command() {
  TestBoard tb;
  tb.native().ota.setPendingVerify(true);
  tb.native().ota.setCanRollback(true);
  LoopMax app(tb.board);
  Logger::setInstance(&app.kernel().log());
  TEST_ASSERT_TRUE(app.begin() == Status::Ok);
  run(app.kernel(), tb.clock, 1000);
  CmdContext ctx;

  TEST_ASSERT_TRUE(app.kernel().commands().execute("sys.ota", "confirm", JsonValue(), ctx) ==
                   Status::Ok);
  TEST_ASSERT_EQUAL_UINT(1, tb.native().ota.confirms());
  Doc doc(app.kernel(), "sys.ota");
  TEST_ASSERT_FALSE(doc.v["trial"].asBool(true));

  TEST_ASSERT_TRUE(app.kernel().commands().execute("sys.ota", "rollback", JsonValue(), ctx) ==
                   Status::Ok);
  TEST_ASSERT_EQUAL_UINT(1, tb.native().ota.rollbacks());
  TEST_ASSERT_TRUE(app.kernel().restartPending());

  // A board that has nothing to go back to.
  tb.native().ota.setCanRollback(false);
  TEST_ASSERT_TRUE(app.kernel().commands().execute("sys.ota", "rollback", JsonValue(), ctx) ==
                   Status::Denied);
  TEST_ASSERT_EQUAL_UINT(1, tb.native().ota.rollbacks());
}

// ---------------------------------------------------------------- files of the console

void test_a_file_of_the_console_is_replaced() {
  OnlineSystem sys(false);
  const std::string first = "console.log('one');";
  const std::string second = "console.log('two'); // " + std::string(3000, 'x');
  sys.pc().http.setUploadChunk(512);

  HttpResult r = upload(sys, "/api/upload/file?name=lmp-console.3f9a1c07.js", first);
  TEST_ASSERT_EQUAL_UINT16(200, r.status);
  Body b(r);
  TEST_ASSERT_TRUE(b.v["ok"].asBool());
  TEST_ASSERT_TRUE(b.v["result"]["path"].stringEquals("/web/lmp-console.3f9a1c07.js"));
  TEST_ASSERT_EQUAL_INT64(static_cast<int64_t>(first.size()), b.v["result"]["bytes"].asInt());
  TEST_ASSERT_TRUE(b.v["result"]["sha256"].stringEquals(fingerprint(first).c_str()));
  TEST_ASSERT_TRUE(fileContent(sys, "/web/lmp-console.3f9a1c07.js") == first);

  // The console serves it at once: no restart.
  const HttpResult page = sys.pc().http.get("/lmp-console.3f9a1c07.js");
  TEST_ASSERT_EQUAL_UINT16(200, page.status);
  TEST_ASSERT_TRUE(page.body == first);
  sys.advance(3000);
  TEST_ASSERT_FALSE(sys.kernel().restartPending());

  r = upload(sys, "/api/upload/file?name=lmp-console.3f9a1c07.js&sha256=" + fingerprint(second),
             second);
  TEST_ASSERT_EQUAL_UINT16(200, r.status);
  TEST_ASSERT_TRUE(fileContent(sys, "/web/lmp-console.3f9a1c07.js") == second);
  TEST_ASSERT_FALSE(sys.pc().fs.exists("/web/.upload"));
  TEST_ASSERT_TRUE(sys.logHas("file /web/lmp-console.3f9a1c07.js received"));
}

void test_a_file_that_does_not_arrive_whole() {
  OnlineSystem sys(false);
  const std::string good = "<!doctype html><title>console</title>";
  const std::string bad = std::string(4000, 'y');
  sys.pc().http.setUploadChunk(512);
  TEST_ASSERT_EQUAL_UINT16(200, upload(sys, "/api/upload/file?name=index.html", good).status);

  // The client goes away: the file of before stays.
  HttpResult r = upload(sys, "/api/upload/file?name=index.html", bad, nullptr, 1500);
  TEST_ASSERT_EQUAL_UINT16(400, r.status);
  TEST_ASSERT_TRUE(fileContent(sys, "/web/index.html") == good);
  TEST_ASSERT_FALSE(sys.pc().fs.exists("/web/.upload"));

  // The fingerprint does not match: the same.
  r = upload(sys, "/api/upload/file?name=index.html&sha256=" + fingerprint(good), bad);
  TEST_ASSERT_EQUAL_UINT16(400, r.status);
  Body b(r);
  TEST_ASSERT_TRUE(b.v["field"].stringEquals("sha256"));
  TEST_ASSERT_TRUE(fileContent(sys, "/web/index.html") == good);
  TEST_ASSERT_FALSE(sys.pc().fs.exists("/web/.upload"));
}

void test_files_stay_in_the_folder_of_the_console() {
  OnlineSystem sys(false);
  const char* names[] = {"",
                         "../kv/sys.bin",
                         "..%2Fkv%2Fsys.bin",
                         "a/b.js",
                         "a%5Cb.js",
                         ".upload",
                         ".hidden",
                         "x..y",
                         "a-name-that-is-decidedly-too-long-to-be-a-file-of-the-real-web-console.js"};
  for (const char* name : names) {
    const HttpResult r = upload(sys, std::string("/api/upload/file?name=") + name, "data");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(400, r.status, name);
    Body b(r);
    TEST_ASSERT_TRUE_MESSAGE(b.v["field"].stringEquals("name"), name);
  }
  TEST_ASSERT_EQUAL_UINT16(400, upload(sys, "/api/upload/file", "data").status);
  TEST_ASSERT_FALSE(sys.pc().fs.exists("/web/.upload"));
  TEST_ASSERT_FALSE(sys.pc().fs.exists("/kv/sys.bin"));

  // With a login, on a network.
  OnlineSystem lan(true);
  HttpCall call;
  call.method = "POST";
  call.target = "/api/upload/file?name=app.js";
  call.body = "data";
  call.from = hal::Ip4::of(192, 168, 1, 77);
  TEST_ASSERT_EQUAL_UINT16(401, lan.pc().http.request(call).status);
  TEST_ASSERT_FALSE(lan.pc().fs.exists("/web/app.js"));
  call.headers.emplace_back("Authorization", "Bearer " + login(lan));
  TEST_ASSERT_EQUAL_UINT16(200, lan.pc().http.request(call).status);
  TEST_ASSERT_TRUE(lan.pc().fs.exists("/web/app.js"));
}

void test_files_are_listed_and_removed() {
  OnlineSystem sys(false);
  upload(sys, "/api/upload/file?name=index.html", "0123456789");
  upload(sys, "/api/upload/file?name=app.0123abcd.js", "01234567890123456789");

  HttpResult r = sys.pc().http.post("/api/cmd/sys.web/files", "{}");
  TEST_ASSERT_EQUAL_UINT16(200, r.status);
  Body list(r);
  TEST_ASSERT_EQUAL_INT64(2, list.v["result"]["count"].asInt());
  TEST_ASSERT_EQUAL_INT64(30, list.v["result"]["bytes"].asInt());
  TEST_ASSERT_EQUAL_UINT(2, list.v["result"]["files"].size());
  bool found = false;
  for (const JsonMember m : list.v["result"]["files"]) {
    if (m.value["name"].stringEquals("app.0123abcd.js")) {
      found = true;
      TEST_ASSERT_EQUAL_INT64(20, m.value["size"].asInt());
    }
  }
  TEST_ASSERT_TRUE(found);

  const char* field = nullptr;
  TEST_ASSERT_TRUE(sys.command("sys.web", "removeFile", "{\"name\":\"app.0123abcd.js\"}") ==
                   Status::Ok);
  TEST_ASSERT_FALSE(sys.pc().fs.exists("/web/app.0123abcd.js"));
  TEST_ASSERT_TRUE(sys.pc().fs.exists("/web/index.html"));
  TEST_ASSERT_TRUE(sys.command("sys.web", "removeFile", "{\"name\":\"app.0123abcd.js\"}") ==
                   Status::NotFound);
  TEST_ASSERT_TRUE(sys.command("sys.web", "removeFile", "{\"name\":\"../kv/sys.bin\"}", &field) ==
                   Status::Invalid);
  TEST_ASSERT_EQUAL_STRING("name", field);
  TEST_ASSERT_TRUE(sys.command("sys.web", "removeFile", "{\"name\":\".upload\"}") ==
                   Status::Invalid);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_fingerprints_of_known_texts);
  RUN_TEST(test_a_fingerprint_does_not_depend_on_the_pieces);
  RUN_TEST(test_fingerprints_are_read_strictly);
  RUN_TEST(test_an_image_is_installed);
  RUN_TEST(test_an_image_with_the_wrong_fingerprint);
  RUN_TEST(test_an_image_of_the_wrong_length);
  RUN_TEST(test_a_board_that_cannot_write_the_image);
  RUN_TEST(test_an_image_given_up_by_command);
  RUN_TEST(test_a_board_without_update);
  RUN_TEST(test_a_firmware_sent_by_the_console);
  RUN_TEST(test_a_filesystem_sent_by_the_console);
  RUN_TEST(test_a_firmware_that_is_refused);
  RUN_TEST(test_a_firmware_needs_a_login);
  RUN_TEST(test_a_firmware_from_an_address);
  RUN_TEST(test_a_download_that_fails);
  RUN_TEST(test_the_arguments_of_a_download);
  RUN_TEST(test_a_new_firmware_is_confirmed_by_itself);
  RUN_TEST(test_a_new_firmware_confirmed_or_given_up_by_command);
  RUN_TEST(test_a_file_of_the_console_is_replaced);
  RUN_TEST(test_a_file_that_does_not_arrive_whole);
  RUN_TEST(test_files_stay_in_the_folder_of_the_console);
  RUN_TEST(test_files_are_listed_and_removed);
  return UNITY_END();
}

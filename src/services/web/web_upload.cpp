// LoopMaxPro - web service: long bodies.
// A firmware image goes to the update service, a file of the console goes to the
// filesystem. Both arrive in pieces, in the context of the server.
#include "services/web/web_service.h"

#if LMP_FEATURE_NET

#include <stdio.h>
#include <string.h>

#if LMP_FEATURE_OTA
#include "services/ota/ota_service.h"
#endif

namespace lmp {
namespace {

constexpr const char* kJson = "application/json";
constexpr const char* kWebFolder = "/web/";
// A file is written here and takes its name at the end: a transfer that stops half way
// leaves the file of before.
constexpr const char* kTemporary = "/web/.upload";

#if LMP_FEATURE_OTA
uint16_t codeOfBegin(Status status) {
  switch (status) {
    case Status::Busy:        return 409;
    case Status::Invalid:     return 400;
    case Status::NoMem:       return 413;
    case Status::Unsupported: return 501;
    case Status::Denied:      return 503;
    default:                  break;
  }
  return 500;
}
#endif

}  // namespace

bool WebService::uploadRefused(uint16_t code, const char* error, const char* msg,
                               const char* field) {
  upload_.code = code;
  upload_.error = error;
  upload_.msg = msg;
  upload_.field = field;
  return false;
}

bool WebService::uploadPart(hal::IHttpRequest& req, const hal::HttpUploadPart& part,
                            bool firmware) {
  switch (part.phase) {
    case hal::HttpUploadPhase::Begin:
      upload_ = Upload();
      upload_.firmware = firmware;
      upload_.digest[0] = '\0';
      upload_.path[0] = '\0';
      return uploadBegin(req, part);
    case hal::HttpUploadPhase::Data:
      return uploadData(part);
    case hal::HttpUploadPhase::End:
      uploadEnd(true);
      return true;
    case hal::HttpUploadPhase::Aborted:
      uploadEnd(false);
      return true;
  }
  return false;
}

bool WebService::uploadBegin(hal::IHttpRequest& req, const hal::HttpUploadPart& part) {
  if (!gate(req)) return uploadRefused(401, "auth", "login required");

  char sha[Sha256::kSize * 2 + 8] = "";
  if (req.query("sha256", sha, sizeof(sha)) && sha[0]) {
    if (!sha256Parse(StringView(sha), upload_.wanted)) {
      return uploadRefused(400, "invalid", "fingerprint not well written", "sha256");
    }
    upload_.check = true;
  }

  if (upload_.firmware) {
#if LMP_FEATURE_OTA
    IOtaService* ota = kernel_->api<IOtaService>();
    if (!ota) return uploadRefused(501, "unsupported", "no firmware update in this build");
    char name[8] = "app";
    req.query("target", name, sizeof(name));
    hal::OtaTarget target = hal::OtaTarget::App;
    if (strcmp(name, "fs") == 0) {
      target = hal::OtaTarget::Filesystem;
    } else if (strcmp(name, "app") != 0) {
      return uploadRefused(400, "invalid", "target is app or fs", "target");
    }
    const Status s = ota->begin(target, part.total, StringView(sha), OtaOrigin::File);
    if (s != Status::Ok) {
      return uploadRefused(codeOfBegin(s), statusName(s), "the image cannot be received");
    }
    upload_.open = true;
    return true;
#else
    return uploadRefused(501, "unsupported", "no firmware update in this build");
#endif
  }

  // A file of the console: only there, and never a name that begins with a dot.
  char name[72] = "";
  if (!req.query("name", name, sizeof(name)) || !name[0]) {
    return uploadRefused(400, "invalid", "name missing", "name");
  }
  if (strchr(name, '/') || strchr(name, '\\') || name[0] == '.' || strlen(name) > 64) {
    return uploadRefused(400, "invalid", "name not valid", "name");
  }
  snprintf(upload_.path, sizeof(upload_.path), "%s%s", kWebFolder, name);
  if (!safeWebPath(StringView(upload_.path))) {
    return uploadRefused(400, "invalid", "name not valid", "name");
  }
  hal::IFs* fs = kernel_->board().fs;
  if (!fs || !fs->mounted()) return uploadRefused(503, "unsupported", "no filesystem");
  hal::FsInfo space;
  if (part.total && fs->info(space) == Status::Ok &&
      space.totalBytes - space.usedBytes < part.total) {
    return uploadRefused(507, "no_mem", "no room in the filesystem");
  }
  fs->mkdir("/web");
  upload_.file = fs->open(kTemporary, hal::OpenMode::Write);
  if (!upload_.file) return uploadRefused(500, "io", "the file cannot be written");
  upload_.open = true;
  return true;
}

bool WebService::uploadData(const hal::HttpUploadPart& part) {
  if (!upload_.open) return false;
  if (upload_.firmware) {
#if LMP_FEATURE_OTA
    IOtaService* ota = kernel_->api<IOtaService>();
    const Status s = ota ? ota->write(part.data.data(), part.data.size()) : Status::Denied;
    if (s != Status::Ok) {
      // The update service has already given up the image, with its reason.
      upload_.open = false;
      return uploadRefused(s == Status::Invalid ? 400 : 500, statusName(s),
                           "the image was given up");
    }
#endif
  } else {
    if (upload_.file->write(part.data.data(), part.data.size()) != part.data.size()) {
      return uploadRefused(507, "no_mem", "no room in the filesystem");
    }
    upload_.hash.update(part.data.data(), part.data.size());
  }
  upload_.bytes += part.data.size();
  return true;
}

void WebService::uploadEnd(bool whole) {
  if (!upload_.open) return;
  upload_.open = false;

  if (upload_.firmware) {
#if LMP_FEATURE_OTA
    IOtaService* ota = kernel_->api<IOtaService>();
    if (!ota) return;
    if (!whole) {
      ota->abort("transfer interrupted");
      if (upload_.code == 200) uploadRefused(400, "invalid", "transfer interrupted");
      return;
    }
    const Status s = ota->finish();
    if (s != Status::Ok) {
      uploadRefused(s == Status::Invalid ? 400 : 500, statusName(s), "the image is not valid");
      return;
    }
    upload_.restart = true;
    ++uploads_;
#endif
    return;
  }

  hal::IFs* fs = kernel_->board().fs;
  upload_.file->close();
  upload_.file = nullptr;
  uint8_t digest[Sha256::kSize];
  upload_.hash.finish(digest);
  sha256Hex(digest, upload_.digest);

  if (!whole) {
    fs->remove(kTemporary);
    if (upload_.code == 200) uploadRefused(400, "invalid", "transfer interrupted");
    return;
  }
  if (upload_.check && memcmp(digest, upload_.wanted, sizeof(digest)) != 0) {
    fs->remove(kTemporary);
    uploadRefused(400, "invalid", "fingerprint does not match", "sha256");
    return;
  }
  if (fs->exists(upload_.path)) fs->remove(upload_.path);
  if (fs->rename(kTemporary, upload_.path) != Status::Ok) {
    fs->remove(kTemporary);
    uploadRefused(500, "io", "the file cannot be written");
    return;
  }
  ++uploads_;
  // Put there on purpose: it stays, and takes the place of the file of the program.
  if (!fs->exists(kWebOverrideMark)) {
    hal::IFile* mark = fs->open(kWebOverrideMark, hal::OpenMode::Write);
    if (mark) mark->close();
  }
  LMP_LOGI(src_, "file %s received, %lu bytes", upload_.path,
           static_cast<unsigned long>(upload_.bytes));
}

void WebService::uploadDone(hal::IHttpRequest&, hal::IHttpResponse& res) {
  // Something left open: the pieces stopped without a word.
  if (upload_.open) uploadEnd(false);

  char version[8];
  snprintf(version, sizeof(version), "%u", kWebApiVersion);
  res.header("X-LMP-Api", version);
  res.header("Cache-Control", "no-store");

  char text[256];
  BufferSink sink(text, sizeof(text));
  JsonWriter w(sink);
  w.beginObject();
  if (upload_.code == 200) {
    w.beginObject("result");
    w.u32("bytes", static_cast<uint32_t>(upload_.bytes));
    if (upload_.firmware) {
#if LMP_FEATURE_OTA
      const OtaService* ota = static_cast<const OtaService*>(
          kernel_->services().find(ServiceId::Ota));
      if (ota) w.str("sha256", ota->lastSha256());
#endif
      w.boolean("restart", upload_.restart);
    } else {
      w.str("sha256", upload_.digest);
      w.str("path", upload_.path);
    }
    w.endObject();
    w.boolean("ok", true);
    w.str("status", "ok");
  } else {
    w.str("error", upload_.error);
    w.str("msg", upload_.msg);
    if (upload_.field) w.str("field", upload_.field);
  }
  w.endObject();
  res.status(upload_.code);
  res.send(kJson, sink.view());
}

}  // namespace lmp

#endif  // LMP_FEATURE_NET

#include "file_api.h"

#include "log.h"

#include <ArduinoJson.h>

#include "config.h"
#include "storage_manager.h"

namespace {

constexpr uint32_t kUploadRebootDelayMs = 1500;

}  // namespace

// -----------------------------------------------------------------------------
// FileApi
// -----------------------------------------------------------------------------

// Primary, recovery files, and staging files for uploads to those paths all
// contain the same credentials. Apply this before either full or ranged reads.
bool FileApi::isCredentialBearingPath(const String& path) {
  return (path == "/config.json") || (path == "/config.bak") ||
         (path == "/config.tmp") || (path == "/config.json.tmp") ||
         (path == "/config.bak.tmp") || (path == "/config.tmp.tmp");
}

const char* FileApi::mimeTypeForPath(const String& path) {
  const int dot = path.lastIndexOf('.');
  if (dot < 0) return "application/octet-stream";
  String ext = path.substring(dot + 1);
  ext.toLowerCase();
  if (ext == "json") return "application/json";
  if (ext == "pdf") return "application/pdf";
  if ((ext == "html") || (ext == "htm")) return "text/html";
  if (ext == "css") return "text/css";
  if (ext == "js") return "application/javascript";
  if ((ext == "txt") || (ext == "log") || (ext == "csv")) return "text/plain";
  if (ext == "png") return "image/png";
  if ((ext == "jpg") || (ext == "jpeg")) return "image/jpeg";
  if (ext == "gif") return "image/gif";
  if (ext == "svg") return "image/svg+xml";
  return "application/octet-stream";
}

String FileApi::normalizedFilePath(const String& requestedName) {
  if (requestedName.isEmpty() || (requestedName.indexOf("..") >= 0) ||
      (requestedName.indexOf('\\') >= 0)) {
    return String();
  }

  String path = requestedName;
  if (!path.startsWith("/")) {
    path = "/" + path;
  }
  if ((path.length() <= 1) || path.endsWith("/")) {
    return String();
  }
  return path;
}

String FileApi::uploadFilePath(const String& uploadName) {
  String name = uploadName;
  const int slash = name.lastIndexOf('/');
  if (slash >= 0) {
    name = name.substring(slash + 1);
  }
  return normalizedFilePath(name);
}

// Escapes into one buffer and sends it with a single sendContent(): in a
// chunked reply every call is its own chunk, so per-character sends multiplied
// the bytes and writes on the fragile AP link.
void FileApi::sendJsonEscapedString(ESP8266WebServer& server, const String& value) {
  String escaped;
  escaped.reserve(value.length() + 2);
  escaped += '"';
  for (size_t index = 0; index < value.length(); ++index) {
    const char c = value[index];
    switch (c) {
      case '"':  escaped += F("\\\""); break;
      case '\\': escaped += F("\\\\"); break;
      case '\b': escaped += F("\\b"); break;
      case '\f': escaped += F("\\f"); break;
      case '\n': escaped += F("\\n"); break;
      case '\r': escaped += F("\\r"); break;
      case '\t': escaped += F("\\t"); break;
      default:
        if (static_cast<uint8_t>(c) < 0x20) {
          char unicode[7];
          snprintf(unicode, sizeof(unicode), "\\u%04x", static_cast<uint8_t>(c));
          escaped += unicode;
        } else {
          escaped += c;
        }
        break;
    }
  }
  escaped += '"';
  server.sendContent(escaped);
}

void FileApi::handleListFiles() {
  if (!storageManager.ensureMounted("list files")) {
    m_responder.sendJsonError(500, F("Storage mount failed"));
    return;
  }

  size_t txBytes = strlen("{\"files\":[");
  m_server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  m_server.send(200, "application/json", "");
  m_server.sendContent("{\"files\":[");

  bool first = true;
  Dir dir = STORAGE.openDir("/");
  while (dir.next()) {
    if (!first) {
      m_server.sendContent(",");
      ++txBytes;
    }
    first = false;

    const String name = dir.fileName();
    m_server.sendContent("{\"name\":");
    sendJsonEscapedString(m_server, name);

    char item[24];
    snprintf(item, sizeof(item), ",\"size\":%u}", static_cast<unsigned>(dir.fileSize()));
    m_server.sendContent(item);
    txBytes += strlen("{\"name\":") + name.length() + strlen(item);
    yield();
  }

  FSInfo fs;
  char footer[80];
  if (STORAGE.info(fs)) {
    snprintf(footer, sizeof(footer), "],\"total\":%u,\"used\":%u}",
             static_cast<unsigned>(fs.totalBytes),
             static_cast<unsigned>(fs.usedBytes));
  } else {
    snprintf(footer, sizeof(footer), "]}");
  }
  txBytes += strlen(footer);
  m_responder.logRequest(200, txBytes);
  m_server.sendContent(footer);
}

void FileApi::handleReadFile() {
  const String path = normalizedFilePath(m_server.arg("name"));
  if (path.isEmpty()) {
    m_responder.sendText(400, F("Invalid file name"));
    return;
  }
  // /config.json holds the WiFi station password in plain text. serializeWifi-
  // Status() deliberately withholds that field from /api/config, and streaming
  // the raw file here would hand it straight back - so this endpoint refuses
  // the file outright and the viewer reads /api/config instead.
  //
  // Deliberately a refusal rather than serving a sanitized body under the same
  // URL: "GET this path returns something other than the bytes at this path"
  // is the kind of leaky abstraction that surprises the next reader.
  if (isCredentialBearingPath(path)) {
    LOG_PRINTF("/api/file refused %s: contains credentials; use /api/config",
               path.c_str());
    m_responder.sendText(403, F("Not served: contains credentials. Use /api/config"));
    return;
  }
  if (!storageManager.ensureMounted("read file")) {
    m_responder.sendText(500, F("Storage mount failed"));
    return;
  }

  File file = STORAGE.open(path, "r");
  if (!file) {
    m_responder.sendText(404, F("Not found"));
    return;
  }

  if (m_server.hasArg("offset") && m_server.hasArg("limit")) {
    constexpr size_t kMaxViewerChunk = 512U * 1024U;
    const size_t fileSize = file.size();
    const size_t offset = static_cast<size_t>(m_server.arg("offset").toInt());
    size_t length = static_cast<size_t>(m_server.arg("limit").toInt());
    if ((length == 0) || (length > kMaxViewerChunk)) length = kMaxViewerChunk;
    if (offset >= fileSize) length = 0;
    else length = min(length, fileSize - offset);

    file.seek(offset, SeekSet);
    m_responder.logRequest(200, length);
    // Lets the /view chunk loader show total progress ("x of y bytes").
    char totalSize[16];
    snprintf(totalSize, sizeof(totalSize), "%u", static_cast<unsigned>(fileSize));
    m_server.sendHeader("X-File-Size", totalSize);
    m_server.send(200, mimeTypeForPath(path), &file, length);
    file.close();
    return;
  }

  const size_t fileSize = file.size();
  m_responder.logRequest(200, fileSize);
  m_server.streamFile(file, mimeTypeForPath(path));
  file.close();
}

void FileApi::handleDeleteFile() {
  const String path = normalizedFilePath(m_server.arg("name"));
  if (path.isEmpty()) {
    m_responder.sendJsonError(400, F("Invalid file name"));
    return;
  }
  if (!storageManager.ensureMounted("delete file")) {
    m_responder.sendJsonError(500, F("Storage mount failed"));
    return;
  }
  if (!STORAGE.exists(path)) {
    m_responder.sendJsonError(404, F("Not found"));
    return;
  }
  if (!STORAGE.remove(path)) {
    m_responder.sendJsonError(500, F("Delete failed"));
    return;
  }
  m_responder.sendJson(200, F("{\"message\":\"Deleted\"}"));
}

void FileApi::handleUploadData() {
  HTTPUpload& upload = m_server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    m_uploadError = false;
    const String path = uploadFilePath(upload.filename);
    if (path.isEmpty()) {
      LOG_PRINTF("File upload failed: invalid name=\"%s\"", upload.filename.c_str());
      m_uploadError = true;
      return;
    }
    if (!storageManager.ensureMounted("upload file")) {
      LOG_PRINTLN("File upload failed: storage mount failed");
      m_uploadError = true;
      return;
    }
    // Written beside the target so a dropped upload never truncates the real file.
    m_uploadPath = path;
    m_uploadFile = STORAGE.open(uploadTempPath(), "w");
    if (!m_uploadFile) {
      LOG_PRINTF("File upload failed: could not open %s for writing",
                 uploadTempPath().c_str());
      m_uploadError = true;
      return;
    }
    return;
  }

  if (upload.status == UPLOAD_FILE_WRITE) {
    if (!m_uploadFile ||
        (m_uploadFile.write(upload.buf, upload.currentSize) != upload.currentSize)) {
      if (!m_uploadError) LOG_PRINTLN("File upload failed while writing data");
      m_uploadError = true;
    }
    return;
  }

  if (upload.status == UPLOAD_FILE_END) {
    closeUploadFile();
    if (m_uploadError) {
      STORAGE.remove(uploadTempPath());
      return;
    }
    // LittleFS rename replaces an existing target in one step, so the original
    // survives intact if this fails; deleting it first would lose both copies.
    if (!STORAGE.rename(uploadTempPath(), m_uploadPath)) {
      LOG_PRINTF("File upload failed: could not rename onto %s", m_uploadPath.c_str());
      STORAGE.remove(uploadTempPath());
      m_uploadError = true;
    }
    return;
  }

  if (upload.status == UPLOAD_FILE_ABORTED) {
    closeUploadFile();
    STORAGE.remove(uploadTempPath());
    LOG_PRINTLN("File upload aborted by client");
    m_uploadError = true;
  }
}

void FileApi::handleUpload() {
  // The config is cached in RAM at boot; a reboot is what makes an uploaded
  // config.json take effect instead of being overwritten by the next save.
  const bool needsReboot = (strcmp_P(m_uploadPath.c_str(), PSTR("/config.json")) == 0);
  if (m_uploadError) {
    m_responder.sendJsonError(500, F("Upload failed"));
  } else if (needsReboot) {
    m_responder.sendJson(200, F("{\"message\":\"Saved - rebooting...\",\"reboot\":true}"));
    m_rebootScheduler.scheduleReboot(kUploadRebootDelayMs);
  } else {
    m_responder.sendJson(200, F("{\"message\":\"Uploaded\"}"));
  }
  m_uploadError = false;
  m_uploadPath = String();
}

String FileApi::uploadTempPath() const {
  return m_uploadPath + F(".tmp");
}

void FileApi::closeUploadFile() {
  if (m_uploadFile) {
    m_uploadFile.close();
  }
}

#include "file_api.h"

#include "log.h"

#include <ArduinoJson.h>

#include "config.h"
#include "storage_manager.h"

namespace {

// Indents a compact JSON byte stream on its way to Serial, one byte at a time.
//
// Deliberately not a parse-then-serializeJsonPretty(): this mirror exists to
// show what is actually on disk, and a file worth reading on the console is
// often one that no longer parses. A byte filter cannot fail, allocates
// nothing, and needs no lookahead beyond the one character of state below, so
// it also survives the 4 KB truncation the caller may apply mid-document.
//
// Output matches serializeJsonPretty(): two spaces per level, ": " after a
// key, and empty containers left on one line.
class JsonIndenter {
 public:
  void write(uint8_t byte);

 private:
  void newline();

  uint8_t m_depth = 0;    // Container nesting level; sets the indent width.
  bool m_inString = false;  // True between the quotes of a string literal.
  bool m_escaped = false;   // True when the previous byte was a backslash.
  bool m_pendingBreak = false;  // A container just opened; break before its first item.
};

void JsonIndenter::newline() {
  Serial.println();
  for (uint8_t level = 0; level < m_depth; ++level) Serial.print(F("  "));
}

void JsonIndenter::write(uint8_t byte) {
  // Inside a string every byte is content, including the structural characters
  // and whitespace this otherwise rewrites.
  if (m_inString) {
    Serial.write(byte);
    if (m_escaped) m_escaped = false;
    else if (byte == '\\') m_escaped = true;
    else if (byte == '"') m_inString = false;
    return;
  }

  // Existing layout is discarded rather than added to, so a file that is
  // already indented does not come out doubly so.
  if ((byte == ' ') || (byte == '\t') || (byte == '\n') || (byte == '\r')) return;

  const bool closer = ((byte == '}') || (byte == ']'));
  if (closer && (m_depth > 0)) --m_depth;
  if (m_pendingBreak) {
    m_pendingBreak = false;
    if (!closer) newline();  // "{}" and "[]" stay on one line.
  } else if (closer) {
    newline();
  }

  Serial.write(byte);
  if (byte == '"') {
    m_inString = true;
  } else if ((byte == '{') || (byte == '[')) {
    ++m_depth;
    m_pendingBreak = true;
  } else if (byte == ',') {
    newline();
  } else if (byte == ':') {
    Serial.write(' ');
  }
}

}  // namespace

// -----------------------------------------------------------------------------
// FileApi
// -----------------------------------------------------------------------------

// Paths whose bytes must never leave the device over HTTP. Only /config.json
// qualifies today: it is the one file that stores a secret (the WiFi station
// password). Kept as a named predicate so a future secret-bearing file is one
// line away from being covered, and so handleDeleteFile()/handleUpload() can
// be pointed at it too if that is ever wanted.
bool FileApi::isCredentialBearingPath(const String& path) {
  return path == "/config.json";
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

void FileApi::sendJsonEscapedString(ESP8266WebServer& server, const String& value) {
  server.sendContent("\"");
  for (size_t index = 0; index < value.length(); ++index) {
    const char c = value[index];
    switch (c) {
      case '"':
        server.sendContent("\\\"");
        break;
      case '\\':
        server.sendContent("\\\\");
        break;
      case '\b':
        server.sendContent("\\b");
        break;
      case '\f':
        server.sendContent("\\f");
        break;
      case '\n':
        server.sendContent("\\n");
        break;
      case '\r':
        server.sendContent("\\r");
        break;
      case '\t':
        server.sendContent("\\t");
        break;
      default:
        if (static_cast<uint8_t>(c) < 0x20) {
          char escaped[7];
          snprintf(escaped, sizeof(escaped), "\\u%04x", static_cast<uint8_t>(c));
          server.sendContent(escaped);
        } else {
          char text[2] = {c, '\0'};
          server.sendContent(text);
        }
        break;
    }
  }
  server.sendContent("\"");
}

void FileApi::handleListFiles() {
  if (!storageManager.ensureMounted("list files")) {
    m_responder.sendJsonError(500, "Storage mount failed");
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
    m_responder.sendText(400, "Invalid file name");
    return;
  }
  // /config.json holds the WiFi station password in plain text. serializeWifi-
  // Status() deliberately withholds that field from /api/config, and streaming
  // the raw file here would hand it straight back - so this endpoint refuses
  // the file outright and the viewer reads /api/config instead.
  //
  // Deliberately a refusal rather than serving a sanitized body under the same
  // URL: "GET this path returns something other than the bytes at this path"
  // is the kind of leaky abstraction that surprises the next reader. The serial
  // mirror below still shows the real on-disk bytes, which is physically local.
  if (isCredentialBearingPath(path)) {
    LOG_PRINTF("/api/file refused %s: contains credentials; use /api/config",
               path.c_str());
    m_responder.sendText(403, "Not served: contains credentials. Use /api/config");
    return;
  }
  if (!storageManager.ensureMounted("read file")) {
    m_responder.sendText(500, "Storage mount failed");
    return;
  }

  File file = STORAGE.open(path, "r");
  if (!file) {
    m_responder.sendText(404, "Not found");
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
    logFileContent(file, path, offset, length);
    file.close();
    return;
  }

  const size_t fileSize = file.size();
  m_responder.logRequest(200, fileSize);
  m_server.streamFile(file, mimeTypeForPath(path));
  logFileContent(file, path, 0, fileSize);
  file.close();
}

void FileApi::logFileContent(File& file, const String& path, size_t offset,
                             size_t length) {
  // Only the config file is mirrored. Serial drains at roughly 7.5 KB/s, so
  // dumping whatever the browser happens to open would stall the loop for
  // minutes on a large file (/zipcodes.bin is 164 KB).
  static constexpr char kMirroredPath[] = "/config.json";
  if (path != kMirroredPath) return;

  // Kept as a safety net in case the config file ever grows: 4 KB is about
  // half a second of serial time, and the current file is under 1 KB.
  static constexpr size_t kMaxSerialDumpBytes = 4096;
  const size_t dumped = min(length, kMaxSerialDumpBytes);

  LOG_PRINTF("file body: %s offset=%u bytes=%u of %u", path.c_str(),
             static_cast<unsigned>(offset), static_cast<unsigned>(dumped),
             static_cast<unsigned>(length));
  // The response has already been streamed, so the read head sits at the end
  // of the range; rewind to replay exactly the bytes the browser received.
  // They are re-indented on the way out - only the console reads this copy.
  if (!file.seek(offset, SeekSet)) {
    LOG_PRINTLN("file body: seek failed");
    return;
  }

  JsonIndenter indenter;
  uint8_t buffer[64];
  size_t remaining = dumped;
  while (remaining > 0) {
    const size_t wanted = min(sizeof(buffer), remaining);
    // File::read() returns int: a negative error must not be widened into a
    // huge size_t length for Serial.write().
    const int read = file.read(buffer, wanted);
    if (read <= 0) break;  // Truncated or unreadable; stop rather than spin.
    for (int index = 0; index < read; ++index) indenter.write(buffer[index]);
    remaining -= static_cast<size_t>(read);
    yield();
  }
  Serial.println();
}

void FileApi::handleDeleteFile() {
  const String path = normalizedFilePath(m_server.arg("name"));
  if (path.isEmpty()) {
    m_responder.sendJsonError(400, "Invalid file name");
    return;
  }
  if (!storageManager.ensureMounted("delete file")) {
    m_responder.sendJsonError(500, "Storage mount failed");
    return;
  }
  if (!STORAGE.exists(path)) {
    m_responder.sendJsonError(404, "Not found");
    return;
  }
  if (!STORAGE.remove(path)) {
    m_responder.sendJsonError(500, "Delete failed");
    return;
  }
  m_responder.sendJson(200, "{\"message\":\"Deleted\"}");
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
    m_uploadFile = STORAGE.open(path, "w");
    if (!m_uploadFile) {
      LOG_PRINTF("File upload failed: could not open %s for writing", path.c_str());
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
    return;
  }

  if (upload.status == UPLOAD_FILE_ABORTED) {
    closeUploadFile();
    LOG_PRINTLN("File upload aborted by client");
    m_uploadError = true;
  }
}

void FileApi::handleUpload() {
  if (m_uploadError) {
    m_responder.sendJsonError(500, "Upload failed");
  } else {
    m_responder.sendJson(200, "{\"message\":\"Uploaded\"}");
  }
  m_uploadError = false;
}

void FileApi::closeUploadFile() {
  if (m_uploadFile) {
    m_uploadFile.close();
  }
}

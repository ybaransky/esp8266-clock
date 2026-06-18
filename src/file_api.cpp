#include "file_api.h"

#include <ArduinoJson.h>

#include "config.h"
#include "storage_manager.h"

const char* FileApi::mimeTypeForPath(const String& path) {
  const int dot = path.lastIndexOf('.');
  if (dot < 0) return "application/octet-stream";
  String ext = path.substring(dot + 1);
  ext.toLowerCase();
  if (ext == "json") return "application/json";
  if (ext == "pdf") return "application/pdf";
  if (ext == "html" || ext == "htm") return "text/html";
  if (ext == "css") return "text/css";
  if (ext == "js") return "application/javascript";
  if (ext == "txt" || ext == "log" || ext == "csv") return "text/plain";
  if (ext == "png") return "image/png";
  if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
  if (ext == "gif") return "image/gif";
  if (ext == "svg") return "image/svg+xml";
  return "application/octet-stream";
}

String FileApi::normalizedFilePath(const String& requestedName) {
  if (requestedName.isEmpty() || requestedName.indexOf("..") >= 0 ||
      requestedName.indexOf('\\') >= 0) {
    return String();
  }

  String path = requestedName;
  if (!path.startsWith("/")) {
    path = "/" + path;
  }
  if (path.length() <= 1 || path.endsWith("/")) {
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
    responder_.sendJson(500, "{\"error\":\"Storage mount failed\"}");
    return;
  }

  size_t txBytes = strlen("{\"files\":[");
  server_.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server_.send(200, "application/json", "");
  server_.sendContent("{\"files\":[");

  bool first = true;
  Dir dir = STORAGE.openDir("/");
  while (dir.next()) {
    if (!first) {
      server_.sendContent(",");
      ++txBytes;
    }
    first = false;

    const String name = dir.fileName();
    server_.sendContent("{\"name\":");
    sendJsonEscapedString(server_, name);

    char item[24];
    snprintf(item, sizeof(item), ",\"size\":%u}", static_cast<unsigned>(dir.fileSize()));
    server_.sendContent(item);
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
    strcpy(footer, "]}");
  }
  txBytes += strlen(footer);
  responder_.logRequest(200, txBytes);
  server_.sendContent(footer);
}

void FileApi::handleReadFile() {
  const String path = normalizedFilePath(server_.arg("name"));
  if (path.isEmpty()) {
    responder_.sendText(400, "Invalid file name");
    return;
  }
  if (!storageManager.ensureMounted("read file")) {
    responder_.sendText(500, "Storage mount failed");
    return;
  }

  File file = STORAGE.open(path, "r");
  if (!file) {
    responder_.sendText(404, "Not found");
    return;
  }

  if (path == "/config.json") {
    printConfigFileToSerial(file);
    file.seek(0, SeekSet);
  }

  responder_.logRequest(200, file.size());
  server_.streamFile(file, mimeTypeForPath(path));
  file.close();
}

void FileApi::handleDeleteFile() {
  const String path = normalizedFilePath(server_.arg("name"));
  if (path.isEmpty()) {
    responder_.sendJson(400, "{\"error\":\"Invalid file name\"}");
    return;
  }
  if (!storageManager.ensureMounted("delete file")) {
    responder_.sendJson(500, "{\"error\":\"Storage mount failed\"}");
    return;
  }
  if (!STORAGE.exists(path)) {
    responder_.sendJson(404, "{\"error\":\"Not found\"}");
    return;
  }
  if (!STORAGE.remove(path)) {
    responder_.sendJson(500, "{\"error\":\"Delete failed\"}");
    return;
  }
  responder_.sendJson(200, "{\"message\":\"Deleted\"}");
}

void FileApi::handleUploadData() {
  HTTPUpload& upload = server_.upload();
  if (upload.status == UPLOAD_FILE_START) {
    uploadError_ = false;
    const String path = uploadFilePath(upload.filename);
    if (path.isEmpty()) {
      uploadError_ = true;
      return;
    }
    if (!storageManager.ensureMounted("upload file")) {
      uploadError_ = true;
      return;
    }
    uploadFile_ = STORAGE.open(path, "w");
    if (!uploadFile_) {
      uploadError_ = true;
      return;
    }
    return;
  }

  if (upload.status == UPLOAD_FILE_WRITE) {
    if (!uploadFile_ ||
        uploadFile_.write(upload.buf, upload.currentSize) != upload.currentSize) {
      uploadError_ = true;
    }
    return;
  }

  if (upload.status == UPLOAD_FILE_END) {
    closeUploadFile();
    return;
  }

  if (upload.status == UPLOAD_FILE_ABORTED) {
    closeUploadFile();
    uploadError_ = true;
  }
}

void FileApi::handleUpload() {
  if (uploadError_) {
    responder_.sendJson(500, "{\"error\":\"Upload failed\"}");
  } else {
    responder_.sendJson(200, "{\"message\":\"Uploaded\"}");
  }
  uploadError_ = false;
}

void FileApi::printConfigFileToSerial(File& file) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, file);
  if (err) {
    Serial.printf("config.json parse error=%s\n", err.c_str());
    return;
  }
  serializeJsonPretty(doc, Serial);
  Serial.println();
}

void FileApi::closeUploadFile() {
  if (uploadFile_) {
    uploadFile_.close();
  }
}

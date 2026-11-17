#pragma once

#include <Arduino.h>
#include <ESP8266WebServer.h>
#include <LittleFS.h>

#include "http_responder.h"
#include "reboot_scheduler.h"

// Handles LittleFS listing, reading, deletion, and streamed-upload HTTP endpoints.
class FileApi {
 public:
  FileApi(ESP8266WebServer& server, HttpResponder& responder,
          RebootScheduler& rebootScheduler)
      : m_server(server), m_responder(responder), m_rebootScheduler(rebootScheduler) {}

  void handleListFiles();
  void handleReadFile();
  void handleDeleteFile();
  void handleUpload();
  void handleUploadData();

 private:
  static const char* mimeTypeForPath(const String& path);
  static String normalizedFilePath(const String& requestedName);
  static String uploadFilePath(const String& uploadName);
  // True for a file whose raw bytes must not be served over HTTP because it
  // stores a secret. See handleReadFile().
  static bool isCredentialBearingPath(const String& path);
  static void sendJsonEscapedString(ESP8266WebServer& server, const String& value);

  void closeUploadFile();
  String uploadTempPath() const;
  // Mirrors the byte range just served to the serial monitor, but only for
  // the config file - every other path returns immediately. Call it after the
  // response has been sent: serial runs at roughly 7.5 KB/s, so this is far
  // slower than the HTTP transfer and must not sit on the browser's wait. The
  // per-chunk yield() keeps the watchdog fed and the display alive.
  static void logFileContent(File& file, const String& path, size_t offset,
                             size_t length);

  ESP8266WebServer& m_server;  // Source of file-management requests.
  HttpResponder& m_responder;  // Sends file-management responses.
  RebootScheduler& m_rebootScheduler;  // Reboots after config.json is replaced, so it takes effect.
  File m_uploadFile;           // Temp file receiving the active upload.
  String m_uploadPath;         // Final path; the temp file is renamed onto it on success.
  bool m_uploadError = false;  // True after an upload write/open failure.
};

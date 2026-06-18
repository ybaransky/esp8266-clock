#pragma once

#include <Arduino.h>
#include <ESP8266WebServer.h>
#include <LittleFS.h>

#include "http_responder.h"

class FileApi {
 public:
  FileApi(ESP8266WebServer& server, HttpResponder& responder)
      : server_(server), responder_(responder) {}

  void handleListFiles();
  void handleReadFile();
  void handleDeleteFile();
  void handleUpload();
  void handleUploadData();

 private:
  static const char* mimeTypeForPath(const String& path);
  static String normalizedFilePath(const String& requestedName);
  static String uploadFilePath(const String& uploadName);
  static void sendJsonEscapedString(ESP8266WebServer& server, const String& value);

  void printConfigFileToSerial(File& file);
  void closeUploadFile();

  ESP8266WebServer& server_;
  HttpResponder& responder_;
  File uploadFile_;
  bool uploadError_ = false;
};

#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESP8266WebServer.h>

class HttpResponder {
 public:
  explicit HttpResponder(ESP8266WebServer& server) : server_(server) {}

  void send(int status, const char* contentType, const char* body);
  void sendText(int status, const char* body);
  void sendJson(int status, const char* body);
  void sendJsonDocument(int status, JsonDocument& doc);
  void sendJsonError(int status, const char* message);
  void sendProgmem(int status, const char* contentType, PGM_P body);
  void logRequest(int status, size_t txBytes = 0);

 private:
  static const char* methodName(HTTPMethod method);

  ESP8266WebServer& server_;  // Server used to send HTTP responses.
};

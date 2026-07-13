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
  void sendGzipProgmem(int status, const char* contentType,
                       const uint8_t* body, size_t length);
  void logRequest(int status, size_t txBytes = 0);
  uint32_t responseSequence() const { return responseSequence_; }
  void logCompletion(uint32_t elapsedUs);

 private:
  static const char* methodName(HTTPMethod method);
  static const char* abbreviatedRoute(const char* uri);
  void captureClientState();

  ESP8266WebServer& server_;  // Server used to send HTTP responses.
  uint32_t responseSequence_ = 0;
  int lastStatus_ = 0;
  size_t lastTxBytes_ = 0;       // Bytes the response promised (Content-Length).
  size_t lastActualTxBytes_ = 0; // Body bytes confirmed written to the socket.
  bool actualTxKnown_ = false;   // True when the send path counted real writes.
  bool clientGoneAfterSend_ = false;  // Client dropped before/while sending.
  size_t lastRxBytes_ = 0;
  HTTPMethod lastMethod_ = HTTP_ANY;
  char lastUri_[48] = {};
  char lastClientIp_[16] = {};
};

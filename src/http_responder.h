#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESP8266WebServer.h>

// Centralizes HTTP response sending and records request/transfer diagnostics.
class HttpResponder {
 public:
  explicit HttpResponder(ESP8266WebServer& server) : m_server(server) {}

  void send(int status, const char* contentType, const char* body);
  void sendText(int status, const char* body);
  void sendJson(int status, const char* body);
  void sendJsonDocument(int status, JsonDocument& doc);
  void sendJsonError(int status, const char* message);
  void sendGzipProgmem(int status, const char* contentType,
                       const uint8_t* body, size_t length,
                       bool cacheImmutable = false);

  // Deserializes the "plain" request body into doc. On failure, logs
  // against `route`, sends a 400 error response, and returns false.
  bool parseJsonBody(JsonDocument& doc, const char* route);
  void logRequest(int status, size_t txBytes = 0);
  uint32_t responseSequence() const { return m_responseSequence; }
  void logCompletion(uint32_t elapsedUs);

 private:
  static const char* methodName(HTTPMethod method);
  static const char* abbreviatedRoute(const char* uri);
  void captureClientState();

  ESP8266WebServer& m_server;  // Server used to send HTTP responses.
  uint32_t m_responseSequence = 0;  // Count of responses started since boot.
  int m_lastStatus = 0;  // HTTP status of the most recent response.
  size_t m_lastTxBytes = 0;       // Bytes the response promised (Content-Length).
  size_t m_lastActualTxBytes = 0; // Body bytes confirmed written to the socket.
  bool m_actualTxKnown = false;   // True when the send path counted real writes.
  bool m_clientGoneAfterSend = false;  // Client dropped before/while sending.
  size_t m_lastRxBytes = 0;  // Request-body bytes received for the last request.
  HTTPMethod m_lastMethod = HTTP_ANY;  // Method of the last request.
  char m_lastUri[48] = {};  // Truncated URI of the last request.
  char m_lastClientIp[16] = {};  // Remote IPv4 text for the last request.
};

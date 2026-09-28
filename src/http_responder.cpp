#include "http_responder.h"

#include <ESP8266WiFi.h>

#include "log.h"
#include "number_format.h"

// -----------------------------------------------------------------------------
// HttpResponder
// -----------------------------------------------------------------------------

void HttpResponder::send(int status, const char* contentType, const char* body) {
  const size_t length = body == nullptr ? 0 : strlen(body);
  logRequest(status, length);
  m_server.sendHeader("Cache-Control", "no-store, max-age=0");
  m_server.send(status, contentType, body == nullptr ? "" : body);
  captureClientState();
}

void HttpResponder::sendText(int status, const char* body) {
  send(status, "text/plain", body);
}

void HttpResponder::sendJson(int status, const char* body) {
  send(status, "application/json", body);
}

void HttpResponder::sendJsonDocument(int status, JsonDocument& doc) {
  String json;
  json.reserve(measureJson(doc));
  serializeJson(doc, json);
  sendJson(status, json.c_str());
}

void HttpResponder::sendJsonError(int status, const char* message) {
  char body[96];
  snprintf(body, sizeof(body), "{\"error\":\"%s\"}", message);
  sendJson(status, body);
}

bool HttpResponder::parseJsonBody(JsonDocument& doc, const char* route) {
  const DeserializationError error = deserializeJson(doc, m_server.arg("plain"));
  if (!error) return true;

  LOG_PRINTF("%s failed: invalid JSON: %s", route, error.c_str());
  sendJsonError(400, "Invalid JSON");
  return false;
}

void HttpResponder::sendGzipProgmem(int status, const char* contentType,
                                    const uint8_t* body, size_t length,
                                    bool cacheImmutable) {
  logRequest(status, length);
  m_server.sendHeader("Content-Encoding", "gzip");
  // Shared assets are hash-versioned in the URL, so they may be cached
  // forever. Pages allow only the back/forward cache; normal navigation
  // still revalidates so config edits show immediately.
  m_server.sendHeader("Cache-Control", cacheImmutable
                                          ? "public, max-age=31536000, immutable"
                                          : "private, no-cache");
  m_server.sendHeader("Vary", "Accept-Encoding");
  // Send headers only, then write the body directly so the socket reports how
  // many bytes actually went out. m_server.send_P() discards that count, which
  // hides mid-transfer truncation from the logs.
  m_server.setContentLength(length);
  m_server.send(status, contentType, "");
  m_lastActualTxBytes =
      m_server.client().write_P(reinterpret_cast<PGM_P>(body), length);
  m_actualTxKnown = true;
  captureClientState();
}

void HttpResponder::logRequest(int status, size_t txBytes) {
  m_lastStatus = status;
  m_lastTxBytes = txBytes;
  m_lastActualTxBytes = txBytes;
  m_actualTxKnown = false;
  m_clientGoneAfterSend = false;
  m_lastRxBytes = m_server.arg("plain").length();
  m_lastMethod = m_server.method();
  snprintf(m_lastUri, sizeof(m_lastUri), "%s", m_server.uri().c_str());
  snprintf(m_lastClientIp, sizeof(m_lastClientIp), "%s",
           m_server.client().remoteIP().toString().c_str());
  ++m_responseSequence;
}

void HttpResponder::captureClientState() {
  m_clientGoneAfterSend = !m_server.client().connected();
}

void HttpResponder::logCompletion(uint32_t elapsedUs) {
  const CommaNumber tx(static_cast<uint32_t>(m_lastTxBytes));
  const CommaNumber rx(static_cast<uint32_t>(m_lastRxBytes));
  const uint32_t elapsedMs = (elapsedUs + 500U) / 1000U;
  const bool truncated = m_actualTxKnown && m_lastActualTxBytes != m_lastTxBytes;
  const uint8_t apStations =
      (WiFi.getMode() & WIFI_AP) ? WiFi.softAPgetStationNum() : 0;
  // Peak cont-stack usage lands on this line too, via the LOG_PRINTF suffix.
  LOG_PRINTF("%s%s%s %s <- %s => %d tx=%s rx=%s time=%lu ms "
             "heap=%u maxblk=%u frag=%u%% sta=%u%s",
             truncated ? "TRUNCATED " : "",
             m_lastStatus >= 400 ? "ERROR " : "",
             methodName(m_lastMethod),
             abbreviatedRoute(m_lastUri),
             m_lastClientIp,
             m_lastStatus,
             tx.c_str(),
             rx.c_str(),
             static_cast<unsigned long>(elapsedMs),
             ESP.getFreeHeap(),
             ESP.getMaxFreeBlockSize(),
             ESP.getHeapFragmentation(),
             apStations,
             m_clientGoneAfterSend ? " client=gone" : "");
  if (truncated) {
    LOG_PRINTF("TRUNCATED detail: wrote %u of %u body bytes to %s",
               static_cast<unsigned>(m_lastActualTxBytes),
               static_cast<unsigned>(m_lastTxBytes),
               m_lastClientIp);
  }
}

const char* HttpResponder::abbreviatedRoute(const char* uri) {
  if ((uri == nullptr) || (uri[0] == '\0') || (strcmp(uri, "/") == 0)) return "home";
  if (strcmp(uri, "/settings") == 0) return "settings";
  if (strcmp(uri, "/format") == 0) return "formats";
  if (strcmp(uri, "/messages") == 0) return "messages";
  if (strcmp(uri, "/location") == 0) return "location";
  if (strcmp(uri, "/time") == 0) return "time";
  if (strcmp(uri, "/wifi") == 0) return "wifi";
  if (strcmp(uri, "/files") == 0) return "files";
  if (strcmp(uri, "/sunset") == 0) return "sunset";
  if (strcmp(uri, "/view") == 0) return "view";
  return uri[0] == '/' ? uri + 1 : uri;
}

const char* HttpResponder::methodName(HTTPMethod method) {
  switch (method) {
    case HTTP_GET:
      return "GET";
    case HTTP_POST:
      return "POST";
    case HTTP_PUT:
      return "PUT";
    case HTTP_PATCH:
      return "PATCH";
    case HTTP_DELETE:
      return "DELETE";
    default:
      return "OTHER";
  }
}

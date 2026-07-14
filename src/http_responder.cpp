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
  server_.sendHeader("Cache-Control", "no-store, max-age=0");
  server_.send(status, contentType, body == nullptr ? "" : body);
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

void HttpResponder::sendGzipProgmem(int status, const char* contentType,
                                    const uint8_t* body, size_t length,
                                    bool cacheImmutable) {
  logRequest(status, length);
  server_.sendHeader("Content-Encoding", "gzip");
  // Shared assets are hash-versioned in the URL, so they may be cached
  // forever. Pages allow only the back/forward cache; normal navigation
  // still revalidates so config edits show immediately.
  server_.sendHeader("Cache-Control", cacheImmutable
                                          ? "public, max-age=31536000, immutable"
                                          : "private, no-cache");
  server_.sendHeader("Vary", "Accept-Encoding");
  // Send headers only, then write the body directly so the socket reports how
  // many bytes actually went out. server_.send_P() discards that count, which
  // hides mid-transfer truncation from the logs.
  server_.setContentLength(length);
  server_.send(status, contentType, "");
  lastActualTxBytes_ =
      server_.client().write_P(reinterpret_cast<PGM_P>(body), length);
  actualTxKnown_ = true;
  captureClientState();
}

void HttpResponder::logRequest(int status, size_t txBytes) {
  lastStatus_ = status;
  lastTxBytes_ = txBytes;
  lastActualTxBytes_ = txBytes;
  actualTxKnown_ = false;
  clientGoneAfterSend_ = false;
  lastRxBytes_ = server_.arg("plain").length();
  lastMethod_ = server_.method();
  snprintf(lastUri_, sizeof(lastUri_), "%s", server_.uri().c_str());
  snprintf(lastClientIp_, sizeof(lastClientIp_), "%s",
           server_.client().remoteIP().toString().c_str());
  ++responseSequence_;
}

void HttpResponder::captureClientState() {
  clientGoneAfterSend_ = !server_.client().connected();
}

void HttpResponder::logCompletion(uint32_t elapsedUs) {
  const CommaNumber tx(static_cast<uint32_t>(lastTxBytes_));
  const CommaNumber rx(static_cast<uint32_t>(lastRxBytes_));
  const uint32_t elapsedMs = (elapsedUs + 500U) / 1000U;
  const bool truncated = actualTxKnown_ && lastActualTxBytes_ != lastTxBytes_;
  const uint8_t apStations =
      (WiFi.getMode() & WIFI_AP) ? WiFi.softAPgetStationNum() : 0;
  LOG_PRINTF("%s%s%s %s <- %s => %d tx=%s rx=%s time=%lu ms "
             "heap=%u maxblk=%u frag=%u%% sta=%u%s\n",
             truncated ? "TRUNCATED " : "",
             lastStatus_ >= 400 ? "ERROR " : "",
             methodName(lastMethod_),
             abbreviatedRoute(lastUri_),
             lastClientIp_,
             lastStatus_,
             tx.c_str(),
             rx.c_str(),
             static_cast<unsigned long>(elapsedMs),
             ESP.getFreeHeap(),
             ESP.getMaxFreeBlockSize(),
             ESP.getHeapFragmentation(),
             apStations,
             clientGoneAfterSend_ ? " client=gone" : "");
  if (truncated) {
    LOG_PRINTF("TRUNCATED detail: wrote %u of %u body bytes to %s\n",
               static_cast<unsigned>(lastActualTxBytes_),
               static_cast<unsigned>(lastTxBytes_),
               lastClientIp_);
  }
}

const char* HttpResponder::abbreviatedRoute(const char* uri) {
  if (uri == nullptr || uri[0] == '\0' || strcmp(uri, "/") == 0) return "home";
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

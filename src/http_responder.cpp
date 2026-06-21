#include "http_responder.h"

#include "log.h"
#include "number_format.h"

void HttpResponder::send(int status, const char* contentType, const char* body) {
  const size_t length = body == nullptr ? 0 : strlen(body);
  logRequest(status, length);
  server_.send(status, contentType, body == nullptr ? "" : body);
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

void HttpResponder::sendProgmem(int status, const char* contentType, PGM_P body) {
  const size_t length = body == nullptr ? 0 : strlen_P(body);
  logRequest(status, length);
  server_.send_P(status, contentType, body);
}

void HttpResponder::logRequest(int status, size_t txBytes) {
  const size_t rxBytes = server_.arg("plain").length();
  const CommaNumber tx(static_cast<uint32_t>(txBytes));
  const CommaNumber rx(static_cast<uint32_t>(rxBytes));
  LOG_PRINTF("%s %s <- %s  => %d  tx=%s rx=%s\n",
             methodName(server_.method()),
             server_.uri().c_str(),
             server_.client().remoteIP().toString().c_str(),
             status,
             tx.c_str(),
             rx.c_str());
}

const char* HttpResponder::methodName(HTTPMethod method) {
  switch (method) {
    case HTTP_GET:
      return "GET";
    case HTTP_POST:
      return "POST";
    case HTTP_PUT:
      return "PUT";
    case HTTP_DELETE:
      return "DELETE";
    case HTTP_PATCH:
      return "PATCH";
    default:
      return "OTHER";
  }
}

#pragma once

#include <ArduinoJson.h>
#include <ESP8266WebServer.h>

#include "http_responder.h"

// Handles ZIP lookup and sunset-calculation requests and serializes their results.
class LocationApi {
 public:
  LocationApi(ESP8266WebServer& server, HttpResponder& responder)
      : server_(server), responder_(responder) {}

  void handleZipcodeLookup();
  void handleSunset();

 private:
  bool parseJsonBody(JsonDocument& doc, const char* route);

  ESP8266WebServer& server_;  // Source of location request arguments and bodies.
  HttpResponder& responder_;  // Sends location API responses.
};

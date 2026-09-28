#pragma once

#include <ArduinoJson.h>
#include <ESP8266WebServer.h>

#include "http_responder.h"

// Handles ZIP lookup and sunset-calculation requests and serializes their results.
class LocationApi {
 public:
  LocationApi(ESP8266WebServer& server, HttpResponder& responder)
      : m_server(server), m_responder(responder) {}

  void handleZipcodeLookup();
  void handleSunset();

 private:
  ESP8266WebServer& m_server;  // Source of location request arguments and bodies.
  HttpResponder& m_responder;  // Sends location API responses.
};

#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESP8266WebServer.h>

#include "http_responder.h"

class WifiApi {
 public:
  WifiApi(ESP8266WebServer& server, HttpResponder& responder)
      : server_(server), responder_(responder) {}

  void handleStatus();
  void handleScan();
  void handleConnect();

 private:
  ESP8266WebServer& server_;       // Source of WiFi API request bodies.
  HttpResponder& responder_;       // Sends WiFi API responses.
};

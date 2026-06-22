#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESP8266WebServer.h>

#include "http_responder.h"
#include "reboot_scheduler.h"

class WifiApi {
 public:
  WifiApi(ESP8266WebServer& server,
          HttpResponder& responder,
          RebootScheduler& rebootScheduler)
      : server_(server), responder_(responder), rebootScheduler_(rebootScheduler) {}

  void handleStatus();
  void handleScan();
  void handleConnect();

 private:
  ESP8266WebServer& server_;       // Source of WiFi API request bodies.
  HttpResponder& responder_;       // Sends WiFi API responses.
  RebootScheduler& rebootScheduler_;  // Schedules reboot after AP config changes.
};

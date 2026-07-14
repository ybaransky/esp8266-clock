#pragma once

#include <ArduinoJson.h>
#include <ESP8266WebServer.h>

#include "http_responder.h"

class ClockController;
class RtcService;

// Handles RTC reads and browser-time synchronization through the clock controller.
class TimeApi {
 public:
  TimeApi(ESP8266WebServer& server, HttpResponder& responder,
          ClockController& clockController, RtcService& rtc)
      : server_(server),
        responder_(responder),
        clockController_(clockController),
        rtc_(rtc) {}

  void handleGetTime();
  void handleTimeSync();

 private:
  bool parseJsonBody(JsonDocument& doc);

  ESP8266WebServer& server_;  // Source of time-sync request bodies.
  HttpResponder& responder_;  // Sends time API responses.
  ClockController& clockController_;  // Applies synchronized time to the application.
  RtcService& rtc_;  // Supplies current RTC time for read responses.
};

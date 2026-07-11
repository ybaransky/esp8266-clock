#pragma once

#include <ArduinoJson.h>
#include <ESP8266WebServer.h>

#include "http_responder.h"

class ClockController;
class RtcService;

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

  ESP8266WebServer& server_;
  HttpResponder& responder_;
  ClockController& clockController_;
  RtcService& rtc_;
};

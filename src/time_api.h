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
      : m_server(server),
        m_responder(responder),
        m_clockController(clockController),
        m_rtc(rtc) {}

  void handleGetTime();
  void handleTimeSync();

 private:
  ESP8266WebServer& m_server;  // Source of time-sync request bodies.
  HttpResponder& m_responder;  // Sends time API responses.
  ClockController& m_clockController;  // Applies synchronized time to the application.
  RtcService& m_rtc;  // Supplies current RTC time for read responses.
};

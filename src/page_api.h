#pragma once

#include <Arduino.h>
#include <ESP8266WebServer.h>

#include "config.h"
#include "http_responder.h"
#include "wifi_connection_manager.h"

class PageApi {
 public:
  PageApi(ESP8266WebServer& server, HttpResponder& responder)
      : server_(server), responder_(responder) {}

  void handleRoot(const WifiRuntimeStatus& status);
  void sendHtml(PGM_P html);

 private:
  static const char* modeName(PersistentMode mode);
  static void networkInfoFromStatus(const WifiRuntimeStatus& status,
                                    String& ssid,
                                    String& ip);

  ESP8266WebServer& server_;
  HttpResponder& responder_;
};

#pragma once

#include <ESP8266WebServer.h>

#include "http_responder.h"

class LocationApi {
 public:
  LocationApi(ESP8266WebServer& server, HttpResponder& responder)
      : server_(server), responder_(responder) {}

  void handleZipcodeLookup();

 private:
  ESP8266WebServer& server_;
  HttpResponder& responder_;
};

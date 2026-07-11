#pragma once

#include <Arduino.h>

class PageManager {
 public:
  void showSsid(const String& ssid);
  void showIpAddress(const String& ip);
};

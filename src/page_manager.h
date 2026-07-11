#pragma once

#include <Arduino.h>

class DisplayManager;

class PageManager {
 public:
  explicit PageManager(DisplayManager& displayManager)
      : displayManager_(displayManager) {}

  void showSsid(const String& ssid);
  void showIpAddress(const String& ip);

 private:
  DisplayManager& displayManager_;
};

#pragma once

#include <Arduino.h>

class DisplayManager;

// Converts network details into paged display overlays for button actions.
class PageManager {
 public:
  explicit PageManager(DisplayManager& displayManager)
      : displayManager_(displayManager) {}

  void showSsid(const String& ssid);
  void showIpAddress(const String& ip);

 private:
  DisplayManager& displayManager_;  // Installs the generated page overlays.
};

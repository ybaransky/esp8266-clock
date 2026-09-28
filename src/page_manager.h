#pragma once

#include <Arduino.h>

class DisplayManager;

// Converts network details into paged display overlays for button actions.
class PageManager {
 public:
  explicit PageManager(DisplayManager& displayManager)
      : m_displayManager(displayManager) {}

  void showSsid(const String& ssid);
  void showIpAddress(const String& ip);

 private:
  DisplayManager& m_displayManager;  // Installs the generated page overlays.
};

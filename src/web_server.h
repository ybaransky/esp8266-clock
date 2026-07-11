#pragma once

#include <Arduino.h>

class ClockController;
class ConfigManager;
class WifiConnectionManager;

void webBegin(ClockController& clockController,
              ConfigManager& configManager,
              WifiConnectionManager& wifiConnectionManager);
void webHandleClients();
void networkGetInfo(String &ssid, String &ip);

// Reboots the device after delayMs, giving the HTTP response time to flush.
void webScheduleReboot(uint32_t delayMs);

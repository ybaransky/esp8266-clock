#pragma once

#include <Arduino.h>

void webBegin();
void webHandleClients();
void networkGetInfo(String &ssid, String &ip);

// Reboots the device after delayMs, giving the HTTP response time to flush.
void webScheduleReboot(uint32_t delayMs);

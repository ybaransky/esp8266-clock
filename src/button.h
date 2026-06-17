#pragma once

#include <Arduino.h>

enum class ButtonEvent {
  NONE,
  SHOW_SSID,
  SHOW_IP_ADDRESS,
  SHOW_RTC_STATUS,
};

void buttonBegin();
void buttonTick();
bool buttonHasEvent();
ButtonEvent buttonNextEvent();

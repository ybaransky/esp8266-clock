#pragma once

#include <Arduino.h>

enum class ButtonEvent {
  kNone,
  kShowSsid,
  kShowIpAddress,
  kShowRtcStatus,
};

void buttonBegin();
void buttonTick();
bool buttonHasEvent();
ButtonEvent buttonNextEvent();

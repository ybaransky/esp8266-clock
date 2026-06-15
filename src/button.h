#pragma once

#include <Arduino.h>

enum class ButtonEvent {
  NONE,
  SHOW_NETWORK_INFO,
  SHOW_I2C_SCAN,
  SHOW_RTC_STATUS,
};

void buttonBegin();
void buttonTick();
void buttonLedTick(unsigned long now);
bool buttonHasEvent();
ButtonEvent buttonNextEvent();

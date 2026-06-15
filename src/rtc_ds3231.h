#pragma once

#include <Arduino.h>

struct RtcStatus {
  bool present;
  bool powerLost;
  bool lowBattery;
  bool sqwConfigured;
  String error;
};

bool rtcBegin();
void rtcTick();
RtcStatus rtcGetStatus();
String rtcGetCurrentTimeString();

void IRAM_ATTR onSQWPulse();

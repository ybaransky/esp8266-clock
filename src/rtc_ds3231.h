#pragma once

#include <Arduino.h>
#include <RTClib.h>

struct RtcStatus {
  bool present;
  bool powerLost;
  bool lowBattery;
  bool sqwConfigured;
  String error;
};

bool rtcBegin();
RtcStatus rtcGetStatus();
String rtcGetCurrentTimeString();
DateTime rtcGetNow();

// SQW 1 Hz interrupt — call after rtcBegin() succeeds, then every loop iteration.
void rtcBeginSqwProcessing();
// Returns true once every kSqwLogIntervalSeconds pulses (caller should log).
bool rtcProcessSqwPulse();

#pragma once

#include <Arduino.h>
#include <RTClib.h>

struct RtcStatus {
  bool present;         // True when the DS3231 responds on I2C.
  bool powerLost;      // True when RTC reports oscillator stop/power loss.
  bool lowBattery;     // True when battery/oscillator status is suspect.
  bool sqwConfigured;  // True when SQW has been configured for 1 Hz.
  String error;        // Last RTC setup/probe error text.
};

bool rtcBegin();
RtcStatus rtcGetStatus();
String rtcGetCurrentTimeString();
DateTime rtcGetNow();
void rtcSetNow(const DateTime& timeValue);

// SQW 1 Hz interrupt - call after rtcBegin() succeeds, then every loop iteration.
void rtcBeginSqwProcessing();
// Returns true once every kSqwLogIntervalSeconds pulses (caller should log).
bool rtcProcessSqwPulse();

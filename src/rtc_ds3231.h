#pragma once

#include <Arduino.h>
#include <RTClib.h>

// Reports RTC presence, oscillator state, SQW setup, and the latest setup error.
struct RtcStatus {
  bool present;         // True when the DS3231 responds on I2C.
  bool powerLost;      // True when RTC reports oscillator stop/power loss.
  bool lowBattery;     // True when battery/oscillator status is suspect.
  bool sqwConfigured;  // True when SQW has been configured for 1 Hz.
  String error;        // Last RTC setup/probe error text.
};

// One coherent RTC sample delivered before scheduling and rendering.
struct RtcTick {
  DateTime now;  // Cached local wall-clock time after any required resync.
  uint32_t secondStartedAtMs = 0;  // ISR timestamp used for tenths and alerts.
  bool discontinuity = false;  // Boot, recovery, backlog, or a corrected time jump.
};

// Provides DS3231 access and a SQW-driven cache. Hardware/ISR state is file-static.
class RtcService {
 public:
  bool begin();
  RtcStatus getStatus() const;
  DateTime getNow();
  void setNow(const DateTime& timeValue);
  void beginSqwProcessing();

  // Call each loop. Consumes a coherent pulse snapshot, resyncs at :00/:30 or
  // after a backlog, and returns the latest sample once. Never replays a backlog.
  bool consumeSqwPulse(RtcTick& tick);
  bool isHealthy() const;

  // Phase-locked elapsed milliseconds, clamped to 999. Falls back to millis()
  // phase only when no recent SQW edge can be trusted.
  uint32_t msIntoSecond(uint32_t nowMs) const;

  // Zero-I2C-cost on the normal render path; live-read fallback if SQW is stale.
  DateTime getNowCached();
};

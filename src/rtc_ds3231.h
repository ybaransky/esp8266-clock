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

// Provides DS3231 access and a SQW-driven cached time source for hot render paths.
class RtcService {
 public:
  bool begin();
  RtcStatus getStatus() const;
  DateTime getNow();
  void setNow(const DateTime& timeValue);

  // Call after begin() succeeds, then every loop iteration via
  // consumeSqwPulse(). Attaches the SQW 1Hz interrupt.
  void beginSqwProcessing();

  // Call every loop iteration once beginSqwProcessing() has run. Consumes
  // any pending SQW pulse captured by the ISR and, on every real pulse,
  // advances the getNowCached() cache by one second. Returns true exactly
  // once per real RTC second - callers that need to react promptly to a
  // time change (e.g. Friday-mode phase transitions) should gate on this,
  // NOT on isLogIntervalDue(), which is throttled and would delay them by
  // up to kSqwLogIntervalSeconds.
  bool consumeSqwPulse();

  // Call once per loop, immediately after consumeSqwPulse() returns true.
  // Returns true only when the cached wall-clock time lands on a
  // kSqwLogIntervalSeconds boundary (e.g. :00 and :30 for the default 30s),
  // and that same call also triggers a live-read resync of the
  // getNowCached() cache to correct for any pulses that were missed.
  // Intended for periodic health/state logging - do not gate time-sensitive
  // logic on this.
  bool isLogIntervalDue();

  // True when the RTC is present and the SQW 1Hz pulse is arriving on schedule.
  bool isHealthy() const;

  // Milliseconds elapsed since the current RTC second began (the last
  // accepted SQW edge, timestamped in the ISR), clamped to 0-999. This is
  // what phase-locks the display's tenths digit to the real RTC second.
  // Falls back to nowMs % 1000 when SQW processing hasn't started or the
  // pulse has gone stale - same graceful degradation as getNowCached().
  uint32_t msIntoSecond(uint32_t nowMs) const;

  // Second-resolution RTC time maintained by consumeSqwPulse(), at
  // effectively zero I2C cost (advanced in software from the SQW pulse
  // rather than re-reading the chip). Automatically falls back to a live
  // getNow() read if the cache hasn't been seeded yet or the SQW pulse has
  // gone stale (see isHealthy()), so it degrades gracefully if SQW pulses
  // stop. This is what display rendering uses.
  DateTime getNowCached();
};

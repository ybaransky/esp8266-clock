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

// Live I2C read of the RTC. Use for infrequent/correctness-critical reads
// (e.g. a one-off API request). For a hot render/tick path, prefer
// rtcGetNowCached() instead - see below.
DateTime rtcGetNow();

// Writes the RTC and immediately resyncs the cache used by rtcGetNowCached(),
// so the cache doesn't keep ticking from the old time for up to a minute.
void rtcSetNow(const DateTime& timeValue);

// SQW 1 Hz interrupt - call after rtcBegin() succeeds, then every loop iteration.
void rtcBeginSqwProcessing();

// Call every loop iteration once rtcBeginSqwProcessing() has run. Consumes any
// pending SQW pulse captured by the ISR and, on every real pulse,
// advances the rtcGetNowCached() cache by one second. Returns true exactly
// once per real RTC second - callers that need to react promptly to a time
// change (e.g. Friday-mode phase transitions) should gate on this, NOT on
// rtcIsLogIntervalDue(), which is throttled and would delay them by up to
// kSqwLogIntervalSeconds.
bool rtcConsumeSqwPulse();

// Call once per loop, immediately after rtcConsumeSqwPulse() returns true.
// Returns true only when the cached wall-clock time lands on a
// kSqwLogIntervalSeconds boundary (e.g. :00 and :30 for the default 30s),
// and that same call also triggers a live-read resync of the
// rtcGetNowCached() cache to correct for any pulses that were missed.
// Intended for periodic health/state logging - do not gate time-sensitive
// logic on this.
bool rtcIsLogIntervalDue();

// Returns true when the RTC is present and the SQW 1Hz pulse is arriving on schedule.
bool rtcIsHealthy();

// Second-resolution RTC time maintained by rtcConsumeSqwPulse(), at
// effectively zero I2C cost (advanced in software from the SQW pulse rather
// than re-reading the chip). Automatically falls back to a live rtcGetNow()
// read if the cache hasn't been seeded yet or the SQW pulse has gone stale
// (see rtcIsHealthy()), so it degrades gracefully if SQW pulses stop.
// This is what display rendering uses.
DateTime rtcGetNowCached();

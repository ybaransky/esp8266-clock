#pragma once

#include <Arduino.h>
#include <RTClib.h>

#include "timezone_rule.h"

// Reports RTC presence, oscillator state, SQW setup, and the latest setup error.
//
// Deliberately a plain trivially-copyable struct with a fixed error buffer, not
// a String: getStatus() is called as a cheap predicate (isHealthy(), the 2s
// health poll), and a String member would allocate and free on the heap on
// every call.
struct RtcStatus {
  bool present = false;        // True when the DS3231 responds on I2C.
  bool powerLost = false;      // True when RTC reports oscillator stop/power loss.
  bool lowBattery = false;     // True when battery/oscillator status is suspect.
  bool sqwConfigured = false;  // True when SQW has been configured for 1 Hz.
  bool timeTrusted = false;    // True when the time came from a running clock or an explicit sync.
  char error[48] = "";         // Last RTC setup/probe error text; empty if none.
};

// One coherent RTC sample delivered before scheduling and rendering.
struct RtcTick {
  DateTime now;  // Local wall-clock time after any required resync.
  uint32_t utc = 0;  // The same instant as UTC seconds; durations are measured on this scale.
  uint32_t secondStartedAtMs = 0;  // ISR timestamp used for tenths and alerts.
  bool discontinuity = false;  // Boot, recovery, backlog, or a corrected time jump.
};

// Owns the DS3231 and the 1 Hz SQW-driven time cache that keeps I2C off the
// render path.
//
// The chip holds UTC. Local wall-clock time is derived through the configured
// timezone rule, once per accepted pulse, so daylight-saving changes need no
// write to the chip and the render path never converts.
//
// All state lives in members, with two file-static exceptions in the
// implementation: the volatile counters the ISR touches (there is exactly one
// SQW pin, and IRAM_ATTR code should not chase a pointer through DRAM to reach
// them), and the pointer the plain-function log time provider reads through.
class RtcService {
 public:
  bool begin();
  RtcStatus getStatus() const { return m_status; }
  // Live I2C reads of the chip: local wall-clock time, and raw UTC seconds.
  DateTime getNow();
  uint32_t getUtcNow();
  // Writes the chip and resyncs the cache. `reason` names the source in the log.
  void setUtc(uint32_t utc, const char* reason);
  void beginSqwProcessing();

  // Sets the rule local time is derived through and refreshes the local cache.
  void setTimeZone(const TimeZoneRule& zone);
  const TimeZoneRule& timeZone() const { return m_zone; }

  // Call each loop. Consumes a coherent pulse snapshot, resyncs at :00/:30 or
  // after a backlog, and returns the latest sample once. Never replays a backlog.
  bool consumeSqwPulse(RtcTick& tick);
  bool isHealthy() const;

  // True only when the chip's time is worth persisting. Distinct from
  // isHealthy(), which asks whether pulses are arriving: after lost-power
  // recovery the SQW train is perfectly healthy while the time itself is the
  // firmware build date, which passes every range check and is still wrong by
  // however long ago the image was built. Anything that writes a timestamp to
  // disk must consult this, not present or isHealthy().
  bool timeIsTrustworthy() const { return m_status.present && m_status.timeTrusted; }

  // Phase-locked elapsed milliseconds, clamped to 999. Falls back to millis()
  // phase only when no recent SQW edge can be trusted.
  uint32_t msIntoSecond(uint32_t nowMs) const;

  // Zero-I2C-cost local time and UTC on the normal render path; live-read
  // fallback if SQW is stale.
  DateTime getNowCached();
  uint32_t getUtcCached();

 private:
  bool probeAddress();
  void recoverIfPowerWasLost();
  void flagInvalidTimeIfNeeded();
  void configureSquareWaveOutput();
  void adjustWithLog(const DateTime& newTime, const char* reason);
  void setError(const char* text);
  // The chip's UTC time, or 2000-01-01 when no chip answered at boot.
  DateTime liveUtc();
  // Stores a UTC second in the cache and derives its local time.
  void setCachedUtc(const DateTime& utc);

  // True when a SQW pulse arrived recently enough to trust the cache.
  bool sqwPulseIsFresh() const;
  void logSqwHealthIfNeeded(uint32_t nowMs);

  // Installed as the log timestamp source. Reads the cache only - never I2C -
  // so a log line can never cost a bus transaction or reorder around one.
  // Static because logSetTimeProvider() takes a plain function pointer; it
  // reaches the single application-owned instance through loggingInstance.
  static bool logTimeProvider(char* buffer, size_t bufferSize);

  RTC_DS3231 m_rtc;  // RTClib DS3231 driver instance.
  RtcStatus m_status;  // Cached RTC health and last error text.
  DateTime m_cachedUtc;  // Second-resolution UTC, advanced by SQW pulses.
  DateTime m_cachedLocal;  // m_cachedUtc as local wall-clock time.
  TimeZoneRule m_zone;  // Derives local time from the chip's UTC; UTC until configured.
  uint32_t m_processingStartedAtMs = 0;  // millis() reference for startup health.
  uint32_t m_lastPulseAtMs = 0;  // ISR timestamp of the last accepted pulse.
  uint32_t m_lastAcceptedPulseAtMs = 0;  // Phase reference used for tenths.
  uint32_t m_lastHealthLogMs = 0;  // Last missing-pulse warning time.
  bool m_processingStarted = false;  // True after SQW interrupt setup begins.
  bool m_sawPulse = false;  // True after accepting at least one real SQW edge.
  bool m_cachedNowSynced = false;  // False until a live read seeds the cache.
  bool m_resyncOnNextPulse = true;  // Realign after boot, time sync, or a race.
};

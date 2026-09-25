#pragma once

#include <Arduino.h>
#include <RTClib.h>

// Reports RTC presence, oscillator state, SQW setup, and the latest setup error.
//
// Deliberately a plain trivially-copyable struct with a fixed error buffer, not
// a String: getStatus() is called as a cheap predicate (isHealthy(), the 2s
// health poll), and a String member made every one of those calls allocate and
// free on the heap.
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
  DateTime now;  // Cached local wall-clock time after any required resync.
  uint32_t secondStartedAtMs = 0;  // ISR timestamp used for tenths and alerts.
  bool discontinuity = false;  // Boot, recovery, backlog, or a corrected time jump.
};

// Owns the DS3231 and the 1 Hz SQW-driven time cache that keeps I2C off the
// render path.
//
// All state lives in members. The only file-static state in the
// implementation is the handful of volatile counters the ISR touches: there is
// exactly one SQW pin, and IRAM_ATTR code should not chase a pointer through
// DRAM to reach its counters. Everything else belongs to the instance, so this
// class is a real object rather than a façade over globals.
class RtcService {
 public:
  bool begin();
  RtcStatus getStatus() const { return status_; }
  DateTime getNow();
  void setNow(const DateTime& timeValue);
  void beginSqwProcessing();

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
  bool timeIsTrustworthy() const { return status_.present && status_.timeTrusted; }

  // Phase-locked elapsed milliseconds, clamped to 999. Falls back to millis()
  // phase only when no recent SQW edge can be trusted.
  uint32_t msIntoSecond(uint32_t nowMs) const;

  // Zero-I2C-cost on the normal render path; live-read fallback if SQW is stale.
  DateTime getNowCached();

 private:
  bool probeAddress();
  void recoverIfPowerWasLost();
  void flagInvalidTimeIfNeeded();
  void configureSquareWaveOutput();
  void adjustWithLog(const DateTime& newTime, const char* reason);
  void setError(const char* text);

  // True when a SQW pulse arrived recently enough to trust cachedNow_.
  bool sqwPulseIsFresh() const;
  void logSqwHealthIfNeeded(uint32_t nowMs);

  // Installed as the log timestamp source. Reads the cache only - never I2C -
  // so a log line can never cost a bus transaction or reorder around one.
  // Static because logSetTimeProvider() takes a plain function pointer; it
  // reaches the single application-owned instance through loggingInstance_.
  static bool logTimeProvider(char* buffer, size_t bufferSize);

  RTC_DS3231 rtc_;  // RTClib DS3231 driver instance.
  RtcStatus status_;  // Cached RTC health and last error text.
  DateTime cachedNow_;  // Second-resolution time, advanced by SQW pulses.
  uint32_t processingStartedAtMs_ = 0;  // millis() reference for startup health.
  uint32_t lastPulseAtMs_ = 0;  // ISR timestamp of the last accepted pulse.
  uint32_t lastAcceptedPulseAtMs_ = 0;  // Phase reference used for tenths.
  uint32_t lastHealthLogMs_ = 0;  // Last missing-pulse warning time.
  bool processingStarted_ = false;  // True after SQW interrupt setup begins.
  bool sawPulse_ = false;  // True after accepting at least one real SQW edge.
  bool cachedNowSynced_ = false;  // False until a live read seeds the cache.
  bool resyncOnNextPulse_ = true;  // Realign after boot, time sync, or a race.
};

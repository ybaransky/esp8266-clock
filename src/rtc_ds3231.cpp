#include "rtc_ds3231.h"

#include <Wire.h>

#include "hardware.h"
#include "log.h"

namespace {

constexpr uint8_t kRtcI2cAddress = Hardware::I2CAddress::DS3231;

// SQW edges advance the cache. RTC servicing owns the 30-second resync;
// diagnostic logging never changes time and never reads the chip.
constexpr uint8_t kSqwResyncSeconds = 30;
constexpr uint32_t kSqwStartupWarnMs = 3500;
constexpr uint32_t kSqwHealthLogIntervalMs = 10000;
constexpr uint32_t kSqwPulseStaleMs = 3000;

// The only state shared with the ISR, and the only file-static state left in
// this module. It is genuinely singleton - there is one SQW pin - and keeping
// it out of the object means the IRAM_ATTR handler writes three fixed
// addresses instead of dereferencing an instance pointer.
struct SqwIsrCounters {
  volatile uint32_t pendingPulseCount = 0;  // Incremented by the ISR, consumed in loop.
  volatile uint32_t lifetimePulseCount = 0;  // Lifetime ISR pulses (diagnostics).
  volatile uint32_t edgeAtMs = 0;  // millis() stamped in the ISR at the last rising edge.
};
SqwIsrCounters isrCounters;

// The instance the log timestamp provider reads from. There is exactly one
// RtcService, owned by ClockApplication; this is how a plain function pointer
// reaches it.
RtcService* loggingInstance = nullptr;

void IRAM_ATTR onRtcSqwPulse() {
  // The edge marks the instant the RTC second increments; capturing millis()
  // here (rather than when loop() consumes the pulse) keeps the phase
  // reference free of loop-servicing latency. millis() is ISR-safe.
  isrCounters.edgeAtMs = millis();
  isrCounters.pendingPulseCount++;
  isrCounters.lifetimePulseCount++;
}

uint32_t lifetimePulseCount() {
  noInterrupts();
  const uint32_t count = isrCounters.lifetimePulseCount;
  interrupts();
  return count;
}

bool isLikelyInvalidTime(const DateTime& now) {
  return (now.year() < 2020) || (now.year() > 2099);
}

void logRtcTime(const char* label, const DateTime& timeValue) {
  LOG_PRINTF("%s %04d-%02d-%02d %02d:%02d:%02d",
             label,
             timeValue.year(), timeValue.month(), timeValue.day(),
             timeValue.hour(), timeValue.minute(), timeValue.second());
}

void warnIfSqwSharesInternalLed() {
  if (Hardware::Pins::RTC_SQW != Hardware::Pins::INTERNAL_LED) return;
  LOG_PRINTF("WARNING: SQW shares GPIO%u with INTERNAL_LED; DS3231 SQW may blink the onboard LED",
             Hardware::Pins::RTC_SQW);
}

}  // namespace

// -----------------------------------------------------------------------------
// RtcService - hardware
// -----------------------------------------------------------------------------

void RtcService::setError(const char* text) {
  strlcpy(status_.error, text, sizeof(status_.error));
}

bool RtcService::probeAddress() {
  Wire.beginTransmission(kRtcI2cAddress);
  return Wire.endTransmission() == 0;
}

bool RtcService::begin() {
  status_ = RtcStatus{};
  logSetTimeProvider(nullptr);
  loggingInstance = this;

  if (!probeAddress()) {
    setError("DS3231 not found on I2C address 0x68");
    LOG_PRINTLN("ERROR: DS3231 not detected");
    return false;
  }

  if (!rtc_.begin()) {
    setError("rtc.begin() failed");
    LOG_PRINTLN("ERROR: rtc.begin() failed");
    return false;
  }

  status_.present = true;

  // Seed the cache from this read before installing the log provider, so boot
  // lines carry a real timestamp even though the provider itself never reads
  // the chip. beginSqwProcessing() re-seeds once the pulse train starts.
  const DateTime now = rtc_.now();
  cachedNow_ = now;
  cachedNowSynced_ = true;
  // Provisionally trusted: a chip answered and we have its time. The two checks
  // below are the ones that can withdraw that, so this must be set before them.
  status_.timeTrusted = true;
  logSetTimeProvider(&RtcService::logTimeProvider);
  logRtcTime("Current RTC time:", now);

  recoverIfPowerWasLost();
  flagInvalidTimeIfNeeded();
  configureSquareWaveOutput();

  LOG_PRINTLN("DS3231 initialized, SQW output set to 1Hz");
  return true;
}

void RtcService::adjustWithLog(const DateTime& newTime, const char* reason) {
  const DateTime oldTime = rtc_.now();
  LOG_PRINTF("Adjusting time (%s)", reason);
  logRtcTime("Old:", oldTime);

  rtc_.adjust(newTime);

  const DateTime updatedTime = rtc_.now();
  logRtcTime("New:", updatedTime);
  // Keep the cache coherent with the chip we just moved, so the next log line
  // does not report the pre-adjustment second.
  cachedNow_ = updatedTime;
  cachedNowSynced_ = true;
}

void RtcService::recoverIfPowerWasLost() {
  if (!rtc_.lostPower()) return;

  status_.powerLost = true;
  status_.lowBattery = true;
  LOG_PRINTLN("WARNING: RTC lost power (possible low/dead backup battery)");

  // Set a known-valid time once to clear the DS3231 OSF/lostPower condition.
  adjustWithLog(DateTime(F(__DATE__), F(__TIME__)), "lost power recovery");
  status_.powerLost = false;
  status_.lowBattery = false;
  // The chip now holds the firmware build date, which is in range and therefore
  // passes flagInvalidTimeIfNeeded(), but it is a placeholder rather than a
  // reading. Nothing may persist it as a timestamp until a real sync arrives.
  status_.timeTrusted = false;
  LOG_PRINTLN("INFO: RTC reset to build time to clear lost-power flag");
}

void RtcService::flagInvalidTimeIfNeeded() {
  const DateTime now = rtc_.now();
  if (!isLikelyInvalidTime(now)) return;

  status_.lowBattery = true;
  status_.timeTrusted = false;
  LOG_PRINTF("WARNING: RTC time looks invalid: %04d-%02d-%02d %02d:%02d:%02d",
             now.year(), now.month(), now.day(),
             now.hour(), now.minute(), now.second());
}

void RtcService::configureSquareWaveOutput() {
  rtc_.disable32K();
  rtc_.writeSqwPinMode(DS3231_SquareWave1Hz);
  status_.sqwConfigured = true;
}

DateTime RtcService::getNow() {
  return status_.present ? rtc_.now() : DateTime(2000, 1, 1, 0, 0, 0);
}

void RtcService::setNow(const DateTime& timeValue) {
  if (!status_.present) {
    LOG_PRINTLN("RTC time sync skipped: DS3231 not initialized");
    return;
  }

  adjustWithLog(timeValue, "browser time sync");
  status_.powerLost = false;
  status_.lowBattery = false;
  // An explicit sync is the authoritative source; it is what clears a build-date
  // placeholder installed by lost-power recovery.
  status_.timeTrusted = true;

  noInterrupts();
  isrCounters.pendingPulseCount = 0;
  interrupts();
  cachedNow_ = timeValue;
  cachedNowSynced_ = true;
  resyncOnNextPulse_ = true;
  sawPulse_ = false;
  processingStartedAtMs_ = millis();
}

// -----------------------------------------------------------------------------
// RtcService - logging
// -----------------------------------------------------------------------------

// Reads only the software cache. Before begin() seeds it (or if no instance is
// registered) this returns false and the logger prints its "--:--:--"
// placeholder, which is strictly better than a bus transaction per log line.
bool RtcService::logTimeProvider(char* buffer, size_t bufferSize) {
  if ((buffer == nullptr) || (bufferSize == 0)) return false;
  const RtcService* self = loggingInstance;
  if ((self == nullptr) || !self->status_.present || !self->cachedNowSynced_) {
    return false;
  }

  const DateTime now = self->cachedNow_;
  snprintf(buffer, bufferSize, "%02d:%02d:%02d",
           now.hour(), now.minute(), now.second());
  return true;
}

// -----------------------------------------------------------------------------
// RtcService - SQW processing
// -----------------------------------------------------------------------------

bool RtcService::sqwPulseIsFresh() const {
  if (!processingStarted_) return false;
  const uint32_t lastEventMs = sawPulse_ ? lastPulseAtMs_ : processingStartedAtMs_;
  return (millis() - lastEventMs) < kSqwPulseStaleMs;
}

void RtcService::logSqwHealthIfNeeded(uint32_t nowMs) {
  if (!processingStarted_) return;

  const uint32_t referenceMs = sawPulse_ ? lastPulseAtMs_ : processingStartedAtMs_;
  if ((nowMs - referenceMs) < kSqwStartupWarnMs) return;
  if ((nowMs - lastHealthLogMs_) < kSqwHealthLogIntervalMs) return;

  lastHealthLogMs_ = nowMs;
  LOG_PRINTF("SQW health: no pulse on GPIO%u for %lu ms, pin=%s, isrCount=%lu",
             Hardware::Pins::RTC_SQW,
             static_cast<unsigned long>(nowMs - referenceMs),
             digitalRead(Hardware::Pins::RTC_SQW) == HIGH ? "HIGH" : "LOW",
             static_cast<unsigned long>(lifetimePulseCount()));
}

void RtcService::beginSqwProcessing() {
  warnIfSqwSharesInternalLed();
  pinMode(Hardware::Pins::RTC_SQW, INPUT_PULLUP);
  const int initialLevel = digitalRead(Hardware::Pins::RTC_SQW);
  processingStartedAtMs_ = millis();
  lastPulseAtMs_ = processingStartedAtMs_;
  lastAcceptedPulseAtMs_ = 0;
  lastHealthLogMs_ = 0;
  sawPulse_ = false;
  processingStarted_ = true;
  resyncOnNextPulse_ = true;
  noInterrupts();
  isrCounters.pendingPulseCount = 0;
  isrCounters.lifetimePulseCount = 0;
  interrupts();

  cachedNow_ = rtc_.now();
  cachedNowSynced_ = true;

  const int interruptNumber = digitalPinToInterrupt(Hardware::Pins::RTC_SQW);
  if (interruptNumber == NOT_AN_INTERRUPT) {
    LOG_PRINTF("WARNING: GPIO%u does not support attachInterrupt; SQW pulse tracking disabled",
               Hardware::Pins::RTC_SQW);
    return;
  }

  attachInterrupt(interruptNumber, onRtcSqwPulse, RISING);
  LOG_PRINTF("SQW interrupt attached on GPIO%u interrupt=%d (RISING, INPUT_PULLUP, initial=%s)",
             Hardware::Pins::RTC_SQW,
             interruptNumber,
             initialLevel == HIGH ? "HIGH" : "LOW");
}

bool RtcService::consumeSqwPulse(RtcTick& tick) {
  noInterrupts();
  const uint32_t count = isrCounters.pendingPulseCount;
  const uint32_t edgeMs = isrCounters.edgeAtMs;
  isrCounters.pendingPulseCount = 0;
  interrupts();
  const uint32_t nowMs = millis();
  if (count == 0) {
    logSqwHealthIfNeeded(nowMs);
    return false;
  }
  if ((nowMs - edgeMs) >= kSqwPulseStaleMs) {
    // A queued but stale edge cannot supply the phase of the current second.
    resyncOnNextPulse_ = true;
    logSqwHealthIfNeeded(nowMs);
    return false;
  }
  // Reject impossible edges without treating them as elapsed RTC seconds.
  if (sawPulse_ && ((edgeMs - lastAcceptedPulseAtMs_) < 500UL)) return false;

  bool discontinuity = resyncOnNextPulse_ || !sawPulse_ || (count > 1) ||
      (sawPulse_ && ((edgeMs - lastAcceptedPulseAtMs_) > 1500UL));
  const DateTime expected(cachedNow_.unixtime() + 1);
  DateTime current = expected;
  if (discontinuity || ((expected.second() % kSqwResyncSeconds) == 0)) {
    current = rtc_.now();
    // If an edge arrived during I2C, the read may straddle two seconds. Leave
    // that new pulse queued and resync on the next loop instead of guessing.
    noInterrupts();
    const bool edgeChanged = isrCounters.edgeAtMs != edgeMs;
    interrupts();
    if (edgeChanged) {
      resyncOnNextPulse_ = true;
      return false;
    }
    discontinuity = discontinuity || (current.unixtime() != expected.unixtime());
  }
  cachedNow_ = current;
  cachedNowSynced_ = true;
  resyncOnNextPulse_ = false;
  sawPulse_ = true;
  lastPulseAtMs_ = edgeMs;
  lastAcceptedPulseAtMs_ = edgeMs;
  tick = {current, edgeMs, discontinuity};
  if (discontinuity) {
    LOG_PRINTF("RTC resynced: pulses=%lu; past announcements suppressed",
               static_cast<unsigned long>(count));
  }
  return true;
}

bool RtcService::isHealthy() const {
  if (!status_.present) return false;
  if (!processingStarted_) return true;
  return sqwPulseIsFresh();
}

// Second-resolution time backed by cachedNow_, avoiding an I2C transaction on
// the hot display-render path. Falls back to a live rtc_.now() read whenever
// the cache can't be trusted: before beginSqwProcessing() has run, or if the
// SQW pulse has gone stale.
DateTime RtcService::getNowCached() {
  // getNow() also handles failed initialization, when RTClib has no I2C device.
  if (!status_.present || !cachedNowSynced_ || !sqwPulseIsFresh()) return getNow();
  return cachedNow_;
}

// Phase-locked "how far into the current RTC second are we": elapsed millis
// since the last accepted SQW edge. Clamped to 999 because consecutive edges
// won't land exactly 1000 millis() ticks apart (timer jitter, crystal drift) -
// just before the next edge the raw value can read 1000+, and clamping parks
// the tenths digit at 9 instead of wrapping to 0 early. Falls back to the
// old millis()-phase behavior when the SQW pulse can't be trusted, matching
// getNowCached()'s degradation.
uint32_t RtcService::msIntoSecond(uint32_t nowMs) const {
  if (!sawPulse_ || !sqwPulseIsFresh()) return nowMs % 1000UL;
  const uint32_t elapsed = nowMs - lastAcceptedPulseAtMs_;
  return elapsed > 999UL ? 999UL : elapsed;
}

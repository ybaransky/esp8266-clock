#include "rtc_ds3231.h"

#include "hardware.h"
#include "log.h"
#include <Wire.h>

namespace {

bool rtcLogTimeProvider(char* buffer, size_t bufferSize);

}  // namespace

// Wraps RTClib initialization, validation, recovery, and direct DS3231 access.
class RtcDs3231 {
public:
  bool begin() {
    status_ = {false, false, false, false, ""};
    logSetTimeProvider(nullptr);

    if (!probeAddress()) {
      status_.error = "DS3231 not found on I2C address 0x68";
      LOG_PRINTLN("ERROR: DS3231 not detected");
      return false;
    }

    if (!rtc_.begin()) {
      status_.error = "rtc.begin() failed";
      LOG_PRINTLN("ERROR: rtc.begin() failed");
      return false;
    }

    status_.present = true;
    logSetTimeProvider(rtcLogTimeProvider);
    logRtcTime("Current RTC time:", rtc_.now());

    recoverIfPowerWasLost();
    flagInvalidTimeIfNeeded();
    configureSquareWaveOutput();

    LOG_PRINTLN("DS3231 initialized, SQW output set to 1Hz");
    return true;
  }

  RtcStatus getStatus() const {
    return status_;
  }

  DateTime now() {
    return status_.present ? rtc_.now() : DateTime(2000, 1, 1, 0, 0, 0);
  }

  void setNow(const DateTime &timeValue) {
    if (!status_.present) {
      LOG_PRINTLN("RTC time sync skipped: DS3231 not initialized");
      return;
    }

    adjustWithLog(timeValue, "browser time sync");
    status_.powerLost = false;
    status_.lowBattery = false;
  }

private:
  static constexpr uint8_t RTC_I2C_ADDRESS = Hardware::I2CAddress::DS3231;  // Fixed DS3231 bus address.

  bool probeAddress() {
    Wire.beginTransmission(RTC_I2C_ADDRESS);
    return Wire.endTransmission() == 0;
  }

  static bool isLikelyInvalidTime(const DateTime &now) {
    return now.year() < 2020 || now.year() > 2099;
  }

  static void logRtcTime(const char *label, const DateTime &timeValue) {
    LOG_PRINTF("%s %04d-%02d-%02d %02d:%02d:%02d",
               label,
               timeValue.year(), timeValue.month(), timeValue.day(),
               timeValue.hour(), timeValue.minute(), timeValue.second());
  }

  void adjustWithLog(const DateTime &newTime, const char *reason) {
    const DateTime oldTime = rtc_.now();
    LOG_PRINTF("Adjusting time (%s)", reason);
    logRtcTime("Old:", oldTime);

    rtc_.adjust(newTime);

    const DateTime updatedTime = rtc_.now();
    logRtcTime("New:", updatedTime);
  }

  void recoverIfPowerWasLost() {
    if (!rtc_.lostPower()) return;

    status_.powerLost = true;
    status_.lowBattery = true;
    LOG_PRINTLN("WARNING: RTC lost power (possible low/dead backup battery)");

    // Set a known-valid time once to clear the DS3231 OSF/lostPower condition.
    adjustWithLog(DateTime(F(__DATE__), F(__TIME__)), "lost power recovery");
    status_.powerLost = false;
    status_.lowBattery = false;
    LOG_PRINTLN("INFO: RTC reset to build time to clear lost-power flag");
  }

  void flagInvalidTimeIfNeeded() {
    const DateTime now = rtc_.now();
    if (!isLikelyInvalidTime(now)) return;

    status_.lowBattery = true;
    LOG_PRINTF("WARNING: RTC time looks invalid: %04d-%02d-%02d %02d:%02d:%02d",
               now.year(), now.month(), now.day(),
               now.hour(), now.minute(), now.second());
  }

  void configureSquareWaveOutput() {
    rtc_.disable32K();
    rtc_.writeSqwPinMode(DS3231_SquareWave1Hz);
    status_.sqwConfigured = true;
  }

  RTC_DS3231 rtc_;  // RTClib DS3231 driver instance.
  RtcStatus status_ = {false, false, false, false, "Not initialized"};  // Cached RTC health.
};

static RtcDs3231 rtc;

namespace {

bool rtcLogTimeProvider(char* buffer, size_t bufferSize) {
  if ((buffer == nullptr) || (bufferSize == 0)) {
    return false;
  }

  const RtcStatus status = rtc.getStatus();
  if (!status.present) {
    return false;
  }

  const DateTime now = rtc.now();
  snprintf(buffer, bufferSize, "%02d:%02d:%02d",
           now.hour(), now.minute(), now.second());
  return true;
}

}  // namespace

// -----------------------------------------------------------------------------
// RtcService
// -----------------------------------------------------------------------------

bool RtcService::begin()              { return rtc.begin(); }
RtcStatus RtcService::getStatus() const { return rtc.getStatus(); }
DateTime RtcService::getNow()         { return rtc.now(); }

// SQW edges advance the cache. RTC servicing owns the 30-second resync;
// diagnostic logging never changes time.
static constexpr uint8_t kSqwResyncSeconds = 30;
static constexpr uint32_t kSqwStartupWarnMs = 3500;
static constexpr uint32_t kSqwHealthLogIntervalMs = 10000;
static constexpr uint32_t kSqwPulseStaleMs = 3000;

// All mutable state for SQW pulse tracking and the software-advanced time
// cache: the RtcDs3231 class above talks to the chip; `sqw` tracks the 1 Hz
// pulse and the cached time it drives.
struct SqwState {
  volatile uint32_t pendingPulseCount = 0;  // Incremented by the ISR, consumed in loop.
  volatile uint32_t isrPulseCount = 0;      // Lifetime ISR pulses (diagnostics).
  volatile uint32_t edgeAtMs = 0;           // millis() stamped in the ISR at the last rising edge.
  bool processingStarted = false;  // True after SQW interrupt setup begins.
  bool sawPulse = false;  // True after accepting at least one real SQW edge.
  uint32_t processingStartedAtMs = 0;  // millis() reference for startup health checks.
  uint32_t lastPulseAtMs = 0;  // ISR timestamp of the last accepted pulse.
  uint32_t lastAcceptedPulseAtMs = 0;  // Phase reference used for tenths.
  uint32_t lastHealthLogMs = 0;  // Last missing-pulse warning time.
  DateTime cachedNow;            // Second-resolution time, advanced by SQW pulses.
  bool cachedNowSynced = false;  // False until a live read seeds the cache.
  bool resyncOnNextPulse = true;  // Realign after boot, time sync, or a read/edge race.
};
static SqwState sqw;

void RtcService::setNow(const DateTime& timeValue) {
  rtc.setNow(timeValue);
  if (!rtc.getStatus().present) return;
  noInterrupts();
  sqw.pendingPulseCount = 0;
  interrupts();
  sqw.cachedNow = timeValue;
  sqw.cachedNowSynced = true;
  sqw.resyncOnNextPulse = true;
  sqw.sawPulse = false;
  sqw.processingStartedAtMs = millis();
}

static void IRAM_ATTR onRtcSqwPulse() {
  // The edge marks the instant the RTC second increments; capturing millis()
  // here (rather than when loop() consumes the pulse) keeps the phase
  // reference free of loop-servicing latency. millis() is ISR-safe.
  sqw.edgeAtMs = millis();
  sqw.pendingPulseCount++;
  sqw.isrPulseCount++;
}

static void warnIfSqwSharesInternalLed() {
  if (Hardware::Pins::RTC_SQW != Hardware::Pins::INTERNAL_LED) return;
  LOG_PRINTF("WARNING: SQW shares GPIO%u with INTERNAL_LED; DS3231 SQW may blink the onboard LED",
             Hardware::Pins::RTC_SQW);
}

static uint32_t currentSqwIsrPulseCount() {
  noInterrupts();
  const uint32_t count = sqw.isrPulseCount;
  interrupts();
  return count;
}

static void logSqwHealthIfNeeded(uint32_t nowMs) {
  if (!sqw.processingStarted) return;

  const uint32_t referenceMs = sqw.sawPulse ? sqw.lastPulseAtMs : sqw.processingStartedAtMs;
  if (static_cast<long>(nowMs - referenceMs) < static_cast<long>(kSqwStartupWarnMs)) {
    return;
  }
  if (static_cast<long>(nowMs - sqw.lastHealthLogMs) <
      static_cast<long>(kSqwHealthLogIntervalMs)) {
    return;
  }

  sqw.lastHealthLogMs = nowMs;
  LOG_PRINTF("SQW health: no pulse on GPIO%u for %lu ms, pin=%s, isrCount=%lu",
             Hardware::Pins::RTC_SQW,
             static_cast<unsigned long>(nowMs - referenceMs),
             digitalRead(Hardware::Pins::RTC_SQW) == HIGH ? "HIGH" : "LOW",
             static_cast<unsigned long>(currentSqwIsrPulseCount()));
}

// True when a SQW pulse has been seen recently enough to trust sqw.cachedNow.
// Shared by RtcService::isHealthy() (user-facing "no rtc" banner) and
// RtcService::getNowCached() (falls back to a live I2C read when this is false).
static bool sqwPulseIsFresh() {
  if (!sqw.processingStarted) return false;
  const uint32_t lastEventMs = sqw.sawPulse ? sqw.lastPulseAtMs : sqw.processingStartedAtMs;
  return static_cast<long>(millis() - lastEventMs) < static_cast<long>(kSqwPulseStaleMs);
}

void RtcService::beginSqwProcessing() {
  warnIfSqwSharesInternalLed();
  pinMode(Hardware::Pins::RTC_SQW, INPUT_PULLUP);
  const int initialLevel = digitalRead(Hardware::Pins::RTC_SQW);
  sqw.processingStartedAtMs = millis();
  sqw.lastPulseAtMs = sqw.processingStartedAtMs;
  sqw.lastAcceptedPulseAtMs = 0;
  sqw.lastHealthLogMs = 0;
  sqw.sawPulse = false;
  sqw.processingStarted = true;
  sqw.resyncOnNextPulse = true;
  noInterrupts();
  sqw.pendingPulseCount = 0;
  sqw.isrPulseCount = 0;
  interrupts();

  sqw.cachedNow = rtc.now();
  sqw.cachedNowSynced = true;

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
  const uint32_t count = sqw.pendingPulseCount;
  const uint32_t edgeMs = sqw.edgeAtMs;
  sqw.pendingPulseCount = 0;
  interrupts();
  const uint32_t nowMs = millis();
  if (count == 0) {
    logSqwHealthIfNeeded(nowMs);
    return false;
  }
  if (nowMs - edgeMs >= kSqwPulseStaleMs) {
    // A queued but stale edge cannot supply the phase of the current second.
    sqw.resyncOnNextPulse = true;
    logSqwHealthIfNeeded(nowMs);
    return false;
  }
  // Reject impossible edges without treating them as elapsed RTC seconds.
  if (sqw.sawPulse && (edgeMs - sqw.lastAcceptedPulseAtMs < 500UL)) return false;

  bool discontinuity = sqw.resyncOnNextPulse || !sqw.sawPulse || (count > 1) ||
      (sqw.sawPulse && (edgeMs - sqw.lastAcceptedPulseAtMs > 1500UL));
  const DateTime expected(sqw.cachedNow.unixtime() + 1);
  DateTime current = expected;
  if (discontinuity || (expected.second() % kSqwResyncSeconds == 0)) {
    current = rtc.now();
    // If an edge arrived during I2C, the read may straddle two seconds. Leave
    // that new pulse queued and resync on the next loop instead of guessing.
    noInterrupts();
    const bool edgeChanged = sqw.edgeAtMs != edgeMs;
    interrupts();
    if (edgeChanged) {
      sqw.resyncOnNextPulse = true;
      return false;
    }
    discontinuity = discontinuity || (current.unixtime() != expected.unixtime());
  }
  sqw.cachedNow = current;
  sqw.cachedNowSynced = true;
  sqw.resyncOnNextPulse = false;
  sqw.sawPulse = true;
  sqw.lastPulseAtMs = edgeMs;
  sqw.lastAcceptedPulseAtMs = edgeMs;
  tick = {current, edgeMs, discontinuity};
  if (discontinuity) {
    LOG_PRINTF("RTC resynced: pulses=%lu; past announcements suppressed",
               static_cast<unsigned long>(count));
  }
  return true;
}

bool RtcService::isHealthy() const {
  if (!rtc.getStatus().present) return false;
  if (!sqw.processingStarted)   return true;
  return sqwPulseIsFresh();
}

// Second-resolution time backed by sqw.cachedNow, avoiding an I2C transaction
// on the hot display-render path (see the SQW section comment above). Falls
// back to a live rtc.now() read whenever the cache can't be trusted: before
// beginSqwProcessing() has run, or if the SQW pulse has gone stale.
DateTime RtcService::getNowCached() {
  if (!sqw.cachedNowSynced || !sqwPulseIsFresh()) return rtc.now();
  return sqw.cachedNow;
}

// Phase-locked "how far into the current RTC second are we": elapsed millis
// since the last accepted SQW edge. Clamped to 999 because consecutive edges
// won't land exactly 1000 millis() ticks apart (timer jitter, crystal drift) -
// just before the next edge the raw value can read 1000+, and clamping parks
// the tenths digit at 9 instead of wrapping to 0 early. Falls back to the
// old millis()-phase behavior when the SQW pulse can't be trusted, matching
// getNowCached()'s degradation.
uint32_t RtcService::msIntoSecond(uint32_t nowMs) const {
  if (!sqw.sawPulse || !sqwPulseIsFresh()) return nowMs % 1000UL;
  const uint32_t elapsed = nowMs - sqw.lastAcceptedPulseAtMs;
  return elapsed > 999UL ? 999UL : elapsed;
}

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
    LOG_PRINTF("%s %04d-%02d-%02d %02d:%02d:%02d\n",
               label,
               timeValue.year(), timeValue.month(), timeValue.day(),
               timeValue.hour(), timeValue.minute(), timeValue.second());
  }

  void adjustWithLog(const DateTime &newTime, const char *reason) {
    const DateTime oldTime = rtc_.now();
    LOG_PRINTF("Adjusting time (%s)\n", reason);
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
    LOG_PRINTF("WARNING: RTC time looks invalid: %04d-%02d-%02d %02d:%02d:%02d\n",
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

// -- SQW 1 Hz interrupt processing ---------------------------------------------
//
// The SQW pin ticks once per RTC second, driven by the same crystal as the
// DS3231's internal clock registers. rtcConsumeSqwPulse() uses that edge to
// advance cachedNow_ by one second in software instead of re-reading the
// clock over I2C every time — see rtcGetNowCached() below. This is what lets
// display rendering gets second-resolution time at
// effectively zero I2C cost instead of one I2C transaction per read.
//
// rtcConsumeSqwPulse() and rtcIsLogIntervalDue() are deliberately separate
// functions: the former fires every real second and is what time-sensitive
// logic (e.g. Friday-mode phase transitions) must gate on, while the latter
// is only true on the :00 and :30 wall-clock second of each minute and
// exists purely to pace the periodic health/state log line. Gating
// time-sensitive logic on rtcIsLogIntervalDue() by mistake delays it by up
// to kSqwLogIntervalSeconds.

static constexpr uint8_t kSqwLogIntervalSeconds = 30;  // Log on :00 and :30 boundaries.
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
  bool cachedNowSynced = false;  // False until the first real read has seeded the cache.
};
static SqwState sqw;

void RtcService::setNow(const DateTime& timeValue) {
  rtc.setNow(timeValue);
  if (rtc.getStatus().present) {
    // Keep the second-resolution cache in step immediately, rather than
    // leaving it to tick forward from the old time until the next periodic
    // resync (see rtcConsumeSqwPulse()).
    sqw.cachedNow = timeValue;
    sqw.cachedNowSynced = true;
  }
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
  LOG_PRINTF("WARNING: SQW shares GPIO%u with INTERNAL_LED; DS3231 SQW may blink the onboard LED\n",
             Hardware::Pins::RTC_SQW);
}

static bool consumeSqwInterruptPulse() {
  noInterrupts();
  const bool pending = sqw.pendingPulseCount > 0;
  if (pending) sqw.pendingPulseCount--;
  interrupts();
  return pending;
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
  LOG_PRINTF("SQW health: no pulse on GPIO%u for %lu ms, pin=%s, isrCount=%lu\n",
             Hardware::Pins::RTC_SQW,
             static_cast<unsigned long>(nowMs - referenceMs),
             digitalRead(Hardware::Pins::RTC_SQW) == HIGH ? "HIGH" : "LOW",
             static_cast<unsigned long>(currentSqwIsrPulseCount()));
}

static bool consumeAcceptedSqwPulse() {
  const bool interruptPulse = consumeSqwInterruptPulse();
  if (!interruptPulse) {
    logSqwHealthIfNeeded(millis());
    return false;
  }

  // Use the ISR-captured edge time, not millis() here: consumption can lag
  // the physical edge by however long loop() was busy (e.g. serving an HTTP
  // request), and this timestamp is the phase reference for rtcMsIntoSecond().
  // A 32-bit aligned volatile read is atomic on the ESP8266.
  const uint32_t edgeMs = sqw.edgeAtMs;
  // DS3231 SQW is 1 Hz; ignore impossible back-to-back pulses caused by
  // sampling races/noise so the cached time stays aligned to real seconds.
  if (sqw.sawPulse && (static_cast<long>(edgeMs - sqw.lastAcceptedPulseAtMs) < 500L)) {
    return false;
  }

  sqw.sawPulse = true;
  sqw.lastPulseAtMs = edgeMs;
  sqw.lastAcceptedPulseAtMs = edgeMs;
  return true;
}

// True when a SQW pulse has been seen recently enough to trust sqw.cachedNow.
// Shared by rtcIsHealthy() (user-facing "no rtc" banner) and
// rtcGetNowCached() (falls back to a live I2C read when this is false).
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
  noInterrupts();
  sqw.pendingPulseCount = 0;
  sqw.isrPulseCount = 0;
  interrupts();

  sqw.cachedNow = rtc.now();
  sqw.cachedNowSynced = true;

  const int interruptNumber = digitalPinToInterrupt(Hardware::Pins::RTC_SQW);
  if (interruptNumber == NOT_AN_INTERRUPT) {
    LOG_PRINTF("WARNING: GPIO%u does not support attachInterrupt; SQW pulse tracking disabled\n",
               Hardware::Pins::RTC_SQW);
    return;
  }

  attachInterrupt(interruptNumber, onRtcSqwPulse, RISING);
  LOG_PRINTF("SQW interrupt attached on GPIO%u interrupt=%d (RISING, INPUT_PULLUP, initial=%s)\n",
             Hardware::Pins::RTC_SQW,
             interruptNumber,
             initialLevel == HIGH ? "HIGH" : "LOW");
}

bool RtcService::consumeSqwPulse() {
  if (!consumeAcceptedSqwPulse()) return false;

  // The pulse itself IS the "one second has elapsed" signal, so advance the
  // cache in software rather than spending an I2C transaction to learn what
  // we already know.
  sqw.cachedNow = DateTime(sqw.cachedNow.unixtime() + 1);
  return true;
}

bool RtcService::isLogIntervalDue() {
  if (sqw.cachedNow.second() % kSqwLogIntervalSeconds != 0) return false;
  sqw.cachedNow = rtc.now();  // Also resyncs the cache, correcting drift from any pulses missed.
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
// rtcBeginSqwProcessing() has run, or if the SQW pulse has gone stale.
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
// rtcGetNowCached()'s degradation.
uint32_t RtcService::msIntoSecond(uint32_t nowMs) const {
  if (!sqw.sawPulse || !sqwPulseIsFresh()) return nowMs % 1000UL;
  const uint32_t elapsed = nowMs - sqw.lastAcceptedPulseAtMs;
  return elapsed > 999UL ? 999UL : elapsed;
}

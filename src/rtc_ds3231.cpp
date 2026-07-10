#include "rtc_ds3231.h"

#include "hardware.h"
#include "log.h"
#include <Wire.h>

namespace {

bool rtcLogTimeProvider(char* buffer, size_t bufferSize);

}  // namespace

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

  String currentTimeString() {
    if (!status_.present) {
      return "N/A";
    }

    DateTime now = rtc_.now();
    char timeBuf[32];
    snprintf(timeBuf, sizeof(timeBuf), "%04d-%02d-%02d %02d:%02d:%02d",
             now.year(), now.month(), now.day(),
             now.hour(), now.minute(), now.second());
    return String(timeBuf);
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
  static constexpr uint8_t RTC_I2C_ADDRESS = Hardware::I2CAddress::DS3231;

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
  if (buffer == nullptr || bufferSize == 0) {
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

bool rtcBegin()                  { return rtc.begin(); }
RtcStatus rtcGetStatus()         { return rtc.getStatus(); }
String rtcGetCurrentTimeString() { return rtc.currentTimeString(); }
DateTime rtcGetNow()             { return rtc.now(); }

// -- SQW 1 Hz interrupt processing ---------------------------------------------
//
// The SQW pin ticks once per RTC second, driven by the same crystal as the
// DS3231's internal clock registers. rtcConsumeSqwPulse() uses that edge to
// advance cachedNow_ by one second in software instead of re-reading the
// clock over I2C every time — see rtcGetNowCached() below. This is what lets
// ClockSource callers (display rendering) get second-resolution time at
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
static volatile uint32_t sqwPendingPulseCount    = 0;
static volatile uint32_t sqwIsrPulseCount        = 0;
static bool              sqwProcessingStarted    = false;
static bool              sqwSawPulse             = false;
static uint32_t          sqwProcessingStartedAtMs = 0;
static uint32_t          sqwLastPulseAtMs         = 0;
static uint32_t          sqwLastAcceptedPulseAtMs = 0;
static uint32_t          sqwLastHealthLogMs       = 0;
static DateTime          cachedNow_;               // Second-resolution time, advanced by SQW pulses.
static bool              cachedNowSynced_ = false;  // False until the first real read has seeded the cache.

void rtcSetNow(const DateTime& timeValue) {
  rtc.setNow(timeValue);
  if (rtc.getStatus().present) {
    // Keep the second-resolution cache in step immediately, rather than
    // leaving it to tick forward from the old time until the next periodic
    // resync (see rtcConsumeSqwPulse()).
    cachedNow_ = timeValue;
    cachedNowSynced_ = true;
  }
}

static void IRAM_ATTR onRtcSqwPulse() {
  sqwPendingPulseCount++;
  sqwIsrPulseCount++;
}

static void warnIfSqwSharesInternalLed() {
  if (Hardware::Pins::RTC_SQW != Hardware::Pins::INTERNAL_LED) return;
  LOG_PRINTF("WARNING: SQW shares GPIO%u with INTERNAL_LED; DS3231 SQW may blink the onboard LED\n",
             Hardware::Pins::RTC_SQW);
}

static bool consumeSqwInterruptPulse() {
  noInterrupts();
  const bool pending = sqwPendingPulseCount > 0;
  if (pending) sqwPendingPulseCount--;
  interrupts();
  return pending;
}

static uint32_t currentSqwIsrPulseCount() {
  noInterrupts();
  const uint32_t count = sqwIsrPulseCount;
  interrupts();
  return count;
}

static void logSqwHealthIfNeeded(uint32_t nowMs) {
  if (!sqwProcessingStarted) return;

  const uint32_t referenceMs = sqwSawPulse ? sqwLastPulseAtMs : sqwProcessingStartedAtMs;
  if (static_cast<long>(nowMs - referenceMs) < static_cast<long>(kSqwStartupWarnMs)) {
    return;
  }
  if (static_cast<long>(nowMs - sqwLastHealthLogMs) <
      static_cast<long>(kSqwHealthLogIntervalMs)) {
    return;
  }

  sqwLastHealthLogMs = nowMs;
  LOG_PRINTF("SQW health: no pulse on GPIO%u for %lu ms, pin=%s, isrCount=%lu\n",
             Hardware::Pins::RTC_SQW,
             static_cast<unsigned long>(nowMs - referenceMs),
             digitalRead(Hardware::Pins::RTC_SQW) == HIGH ? "HIGH" : "LOW",
             static_cast<unsigned long>(currentSqwIsrPulseCount()));
}

static bool consumeSqwPulse() {
  const bool interruptPulse = consumeSqwInterruptPulse();
  if (!interruptPulse) {
    logSqwHealthIfNeeded(millis());
    return false;
  }

  const uint32_t nowMs = millis();
  // DS3231 SQW is 1 Hz; ignore impossible back-to-back pulses caused by
  // sampling races/noise so cachedNow_ stays aligned to real seconds.
  if (sqwSawPulse && static_cast<long>(nowMs - sqwLastAcceptedPulseAtMs) < 500L) {
    return false;
  }

  sqwSawPulse = true;
  sqwLastPulseAtMs = nowMs;
  sqwLastAcceptedPulseAtMs = nowMs;
  return true;
}

// True when a SQW pulse has been seen recently enough to trust cachedNow_.
// Shared by rtcIsHealthy() (user-facing "no rtc" banner) and
// rtcGetNowCached() (falls back to a live I2C read when this is false).
static bool sqwPulseIsFresh() {
  if (!sqwProcessingStarted) return false;
  const uint32_t lastEventMs = sqwSawPulse ? sqwLastPulseAtMs : sqwProcessingStartedAtMs;
  return static_cast<long>(millis() - lastEventMs) < static_cast<long>(kSqwPulseStaleMs);
}

void rtcBeginSqwProcessing() {
  warnIfSqwSharesInternalLed();
  pinMode(Hardware::Pins::RTC_SQW, INPUT_PULLUP);
  const int initialLevel = digitalRead(Hardware::Pins::RTC_SQW);
  sqwProcessingStartedAtMs = millis();
  sqwLastPulseAtMs = sqwProcessingStartedAtMs;
  sqwLastAcceptedPulseAtMs = 0;
  sqwLastHealthLogMs = 0;
  sqwSawPulse = false;
  sqwProcessingStarted = true;
  noInterrupts();
  sqwPendingPulseCount = 0;
  sqwIsrPulseCount = 0;
  interrupts();

  cachedNow_ = rtc.now();
  cachedNowSynced_ = true;

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

bool rtcConsumeSqwPulse() {
  if (!consumeSqwPulse()) return false;

  // The pulse itself IS the "one second has elapsed" signal, so advance the
  // cache in software rather than spending an I2C transaction to learn what
  // we already know.
  cachedNow_ = DateTime(cachedNow_.unixtime() + 1);
  return true;
}

bool rtcIsLogIntervalDue() {
  if (cachedNow_.second() % kSqwLogIntervalSeconds != 0) return false;
  cachedNow_ = rtc.now();  // Also resyncs the cache, correcting drift from any pulses missed.
  return true;
}

bool rtcIsHealthy() {
  if (!rtc.getStatus().present) return false;
  if (!sqwProcessingStarted)    return true;
  return sqwPulseIsFresh();
}

// Second-resolution time backed by cachedNow_, avoiding an I2C transaction on
// the hot display-render path (see the SQW section comment above). Falls
// back to a live rtc.now() read whenever the cache can't be trusted: before
// rtcBeginSqwProcessing() has run, or if the SQW pulse has gone stale.
DateTime rtcGetNowCached() {
  if (!cachedNowSynced_ || !sqwPulseIsFresh()) return rtc.now();
  return cachedNow_;
}

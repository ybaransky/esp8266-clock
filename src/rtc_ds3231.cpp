#include "rtc_ds3231.h"

#include "hardware.h"
#include "log.h"
#include <RTClib.h>
#include <Wire.h>

class RtcDs3231 {
public:
  bool begin() {
    status_ = {false, false, false, false, ""};

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

  RTC_DS3231 rtc_;
  RtcStatus status_ = {false, false, false, false, "Not initialized"};
};

static RtcDs3231 rtc;

bool rtcBegin()                  { return rtc.begin(); }
RtcStatus rtcGetStatus()         { return rtc.getStatus(); }
String rtcGetCurrentTimeString() { return rtc.currentTimeString(); }
DateTime rtcGetNow()             { return rtc.now(); }
void rtcSetNow(const DateTime& timeValue) { rtc.setNow(timeValue); }

// ── SQW 1 Hz interrupt processing ─────────────────────────────────────────────

static constexpr uint8_t kSqwLogIntervalSeconds = 5;
static volatile uint32_t sqwPendingPulseCount    = 0;
static uint8_t           sqwLogPulseCount        = 0;

static void IRAM_ATTR onRtcSqwPulse() {
  sqwPendingPulseCount++;
}

static void warnIfSqwSharesInternalLed() {
  if (Hardware::Pins::RTC_SQW != Hardware::Pins::INTERNAL_LED) return;
  LOG_PRINTF("WARNING: SQW shares GPIO%u with INTERNAL_LED; DS3231 SQW may blink the onboard LED\n",
             Hardware::Pins::RTC_SQW);
}

static bool consumeSqwPulse() {
  noInterrupts();
  const bool pending = sqwPendingPulseCount > 0;
  if (pending) sqwPendingPulseCount--;
  interrupts();
  return pending;
}

void rtcBeginSqwProcessing() {
  warnIfSqwSharesInternalLed();
  pinMode(Hardware::Pins::RTC_SQW, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(Hardware::Pins::RTC_SQW), onRtcSqwPulse, RISING);
  LOG_PRINTF("SQW interrupt attached on GPIO%u (RISING, INPUT_PULLUP)\n",
             Hardware::Pins::RTC_SQW);
}

bool rtcProcessSqwPulse() {
  if (!consumeSqwPulse()) return false;
  if (++sqwLogPulseCount >= kSqwLogIntervalSeconds) {
    sqwLogPulseCount = 0;
    return true;
  }
  return false;
}

#include "rtc_ds3231.h"

#include "hardware.h"
#include <RTClib.h>
#include <Wire.h>

// Forward declaration for the SQW interrupt handler.
static void IRAM_ATTR onSqwPulseIsr();

class RtcDs3231 {
public:
  bool begin() {
    status_ = {false, false, false, false, ""};

    if (!probeAddress()) {
      status_.error = "DS3231 not found on I2C address 0x68";
      Serial.println("[RTC] ERROR: DS3231 not detected");
      return false;
    }

    if (!rtc_.begin()) {
      status_.error = "rtc.begin() failed";
      Serial.println("[RTC] ERROR: rtc.begin() failed");
      return false;
    }

    status_.present = true;
    logRtcTime("Current RTC time:", rtc_.now());
    recoverIfPowerWasLost();
    flagInvalidTimeIfNeeded();
    configureSquareWaveInterrupt();

    Serial.printf("[RTC] DS3231 initialized, SQW=1Hz on GPIO%u (RISING, INPUT_PULLUP)\n",
                  Hardware::Pins::RTC_SQW);
    return true;
  }

  void tick() {
    if (!sqwPulsePending_) return;

    sqwPulsePending_ = false;
    if (++sqwPulseCount_ >= LOG_INTERVAL_SECONDS) {
      sqwPulseCount_ = 0;
      logRtcTime("SQW trigger:", rtc_.now());
    }
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

  void IRAM_ATTR handleSqwPulse() {
    sqwPulsePending_ = true;
  }

private:
  static constexpr uint8_t RTC_I2C_ADDRESS = Hardware::I2CAddress::DS3231;
  static constexpr uint8_t LOG_INTERVAL_SECONDS = 5;

  bool probeAddress() {
    Wire.beginTransmission(RTC_I2C_ADDRESS);
    return Wire.endTransmission() == 0;
  }

  static bool isLikelyInvalidTime(const DateTime &now) {
    return now.year() < 2020 || now.year() > 2099;
  }

  static void logRtcTime(const char *label, const DateTime &timeValue) {
    Serial.printf("[RTC] %s %04d-%02d-%02d %02d:%02d:%02d\n",
                  label,
                  timeValue.year(), timeValue.month(), timeValue.day(),
                  timeValue.hour(), timeValue.minute(), timeValue.second());
  }

  void adjustWithLog(const DateTime &newTime, const char *reason) {
    const DateTime oldTime = rtc_.now();
    Serial.printf("[RTC] Adjusting time (%s)\n", reason);
    logRtcTime("Old:", oldTime);

    rtc_.adjust(newTime);

    const DateTime updatedTime = rtc_.now();
    logRtcTime("New:", updatedTime);
  }

  void recoverIfPowerWasLost() {
    if (!rtc_.lostPower()) return;

    status_.powerLost = true;
    status_.lowBattery = true;
    Serial.println("[RTC] WARNING: RTC lost power (possible low/dead backup battery)");

    // Set a known-valid time once to clear the DS3231 OSF/lostPower condition.
    adjustWithLog(DateTime(F(__DATE__), F(__TIME__)), "lost power recovery");
    status_.powerLost = false;
    status_.lowBattery = false;
    Serial.println("[RTC] INFO: RTC reset to build time to clear lost-power flag");
  }

  void flagInvalidTimeIfNeeded() {
    const DateTime now = rtc_.now();
    if (!isLikelyInvalidTime(now)) return;

    status_.lowBattery = true;
    Serial.printf("[RTC] WARNING: RTC time looks invalid: %04d-%02d-%02d %02d:%02d:%02d\n",
                  now.year(), now.month(), now.day(),
                  now.hour(), now.minute(), now.second());
  }

  void configureSquareWaveInterrupt() {
    rtc_.disable32K();
    rtc_.writeSqwPinMode(DS3231_SquareWave1Hz);
    status_.sqwConfigured = true;

    pinMode(Hardware::Pins::RTC_SQW, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(Hardware::Pins::RTC_SQW), onSqwPulseIsr, RISING);
  }

  RTC_DS3231 rtc_;
  RtcStatus status_ = {false, false, false, false, "Not initialized"};
  volatile bool sqwPulsePending_ = false;
  uint8_t sqwPulseCount_ = 0;
};

static RtcDs3231 rtc;

static void IRAM_ATTR onSqwPulseIsr() {
  rtc.handleSqwPulse();
}

bool rtcBegin()                  { return rtc.begin(); }
void rtcTick()                   { rtc.tick(); }
RtcStatus rtcGetStatus()         { return rtc.getStatus(); }
String rtcGetCurrentTimeString() { return rtc.currentTimeString(); }

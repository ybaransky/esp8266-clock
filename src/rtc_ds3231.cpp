#include "rtc_ds3231.h"

#include "hardware.h"
#include <RTClib.h>
#include <Wire.h>

class RtcDs3231 {
public:
  bool begin() {
    status = {false, false, false, false, ""};

    if (!probeAddress()) {
      status.error = "DS3231 not found on I2C address 0x68";
      Serial.println("[RTC] ERROR: DS3231 not detected");
      return false;
    }

    if (!rtc.begin()) {
      status.error = "rtc.begin() failed";
      Serial.println("[RTC] ERROR: rtc.begin() failed");
      return false;
    }

    status.present = true;
    logRtcTime("Current RTC time:", rtc.now());
    recoverIfPowerWasLost();
    flagInvalidTimeIfNeeded();
    configureSquareWaveInterrupt();

    Serial.printf("[RTC] DS3231 initialized, SQW=1Hz on GPIO%u (RISING, INPUT_PULLUP)\n",
                  Hardware::Pins::RTC_SQW);
    return true;
  }

  void tick() {
    if (!sqwPulsePending) return;

    sqwPulsePending = false;
    // Placeholder for pulse-driven tasks if needed later.
  }

  RtcStatus getStatus() const {
    return status;
  }

  String currentTimeString() {
    if (!status.present) {
      return "N/A";
    }

    DateTime now = rtc.now();
    char timeBuf[32];
    snprintf(timeBuf, sizeof(timeBuf), "%04d-%02d-%02d %02d:%02d:%02d",
             now.year(), now.month(), now.day(),
             now.hour(), now.minute(), now.second());
    return String(timeBuf);
  }

  void IRAM_ATTR handleSqwPulse() {
    sqwPulsePending = true;
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
    Serial.printf("[RTC] %s %04d-%02d-%02d %02d:%02d:%02d\n",
                  label,
                  timeValue.year(), timeValue.month(), timeValue.day(),
                  timeValue.hour(), timeValue.minute(), timeValue.second());
  }

  void adjustWithLog(const DateTime &newTime, const char *reason) {
    const DateTime oldTime = rtc.now();
    Serial.printf("[RTC] Adjusting time (%s)\n", reason);
    logRtcTime("Old:", oldTime);

    rtc.adjust(newTime);

    const DateTime updatedTime = rtc.now();
    logRtcTime("New:", updatedTime);
  }

  void recoverIfPowerWasLost() {
    if (!rtc.lostPower()) return;

    status.powerLost = true;
    status.lowBattery = true;
    Serial.println("[RTC] WARNING: RTC lost power (possible low/dead backup battery)");

    // Set a known-valid time once to clear the DS3231 OSF/lostPower condition.
    adjustWithLog(DateTime(F(__DATE__), F(__TIME__)), "lost power recovery");
    status.powerLost = false;
    status.lowBattery = false;
    Serial.println("[RTC] INFO: RTC reset to build time to clear lost-power flag");
  }

  void flagInvalidTimeIfNeeded() {
    const DateTime now = rtc.now();
    if (!isLikelyInvalidTime(now)) return;

    status.lowBattery = true;
    Serial.printf("[RTC] WARNING: RTC time looks invalid: %04d-%02d-%02d %02d:%02d:%02d\n",
                  now.year(), now.month(), now.day(),
                  now.hour(), now.minute(), now.second());
  }

  void configureSquareWaveInterrupt() {
    rtc.disable32K();
    rtc.writeSqwPinMode(DS3231_SquareWave1Hz);
    status.sqwConfigured = true;

    pinMode(Hardware::Pins::RTC_SQW, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(Hardware::Pins::RTC_SQW), onSQWPulse, RISING);
  }

  RTC_DS3231 rtc;
  RtcStatus status = {false, false, false, false, "Not initialized"};
  volatile bool sqwPulsePending = false;
};

static RtcDs3231 rtcDs3231;

void IRAM_ATTR onSQWPulse() {
  rtcDs3231.handleSqwPulse();
}

bool rtcBegin() {
  return rtcDs3231.begin();
}

void rtcTick() {
  rtcDs3231.tick();
}

RtcStatus rtcGetStatus() {
  return rtcDs3231.getStatus();
}

String rtcGetCurrentTimeString() {
  return rtcDs3231.currentTimeString();
}

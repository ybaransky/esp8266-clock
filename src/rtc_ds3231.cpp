#include "rtc_ds3231.h"

#include "hardware.h"
#include <ESP.h>
#include <RTClib.h>
#include <Wire.h>

// Forward declaration for SQW interrupt handler.
static void IRAM_ATTR onSqwPulseIsr();

class RtcCrashRecovery {
public:
  bool shouldForcePolling() {
    State state = loadState();
    const String resetReason = ESP.getResetReason();
    const bool exceptionReset = isExceptionLikeReset(resetReason);

    if (exceptionReset) {
      if (state.exceptionStreak < 255) {
        state.exceptionStreak++;
      }
      state.stableBoots = 0;
    } else {
      state.exceptionStreak = 0;
      if (state.pollingForced && state.stableBoots < 255) {
        state.stableBoots++;
      }
    }

    if (state.exceptionStreak >= EXCEPTION_STREAK_THRESHOLD) {
      state.pollingForced = true;
    }

    if (state.pollingForced && state.stableBoots >= STABLE_BOOTS_TO_CLEAR_FORCE) {
      state.pollingForced = false;
      state.stableBoots = 0;
      Serial.println("[RTC] INFO: Crash recovery returned to SQW mode after stable boots");
    }

    saveState(state);

    Serial.printf("[RTC] Reset reason: %s\n", resetReason.c_str());
    Serial.printf("[RTC] Crash guard: exceptionStreak=%u pollingForced=%s stableBoots=%u\n",
                  static_cast<unsigned>(state.exceptionStreak),
                  state.pollingForced ? "yes" : "no",
                  static_cast<unsigned>(state.stableBoots));

    return state.pollingForced;
  }

private:
  struct State {
    uint32_t magic;
    uint8_t exceptionStreak;
    uint8_t stableBoots;
    uint8_t pollingForced;
    uint8_t reserved;
  };

  static constexpr uint32_t STATE_MAGIC = 0x52544347;  // RTCG
  static constexpr uint32_t RTC_WORD_OFFSET = 64;
  static constexpr uint8_t EXCEPTION_STREAK_THRESHOLD = 2;
  static constexpr uint8_t STABLE_BOOTS_TO_CLEAR_FORCE = 3;

  static bool isExceptionLikeReset(const String &reason) {
    return reason.indexOf("Exception") >= 0 ||
           reason.indexOf("Fatal") >= 0 ||
           reason.indexOf("wdt") >= 0 ||
           reason.indexOf("WDT") >= 0;
  }

  static State defaultState() {
    return {STATE_MAGIC, 0, 0, 0, 0};
  }

  static State loadState() {
    State state = defaultState();
    if (!ESP.rtcUserMemoryRead(RTC_WORD_OFFSET, reinterpret_cast<uint32_t *>(&state), sizeof(state))) {
      return defaultState();
    }
    if (state.magic != STATE_MAGIC) {
      return defaultState();
    }
    return state;
  }

  static void saveState(const State &state) {
    State writableState = state;
    ESP.rtcUserMemoryWrite(RTC_WORD_OFFSET, reinterpret_cast<uint32_t *>(&writableState), sizeof(writableState));
  }
};

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

    forcePollingMode_ = crashRecovery_.shouldForcePolling();
    recoverIfPowerWasLost();
    flagInvalidTimeIfNeeded();

    if (forcePollingMode_) {
      configurePollingMode();
      Serial.printf("[RTC] DS3231 initialized in polling mode (GPIO%u SQW disabled by crash guard)\n",
                    Hardware::Pins::RTC_SQW);
    } else {
      configureSquareWaveInterrupt();
      Serial.printf("[RTC] DS3231 initialized, SQW=1Hz on GPIO%u (RISING, INPUT_PULLUP)\n",
                    Hardware::Pins::RTC_SQW);
    }
    return true;
  }

  void tick() {
    if (!status_.present) return;

    if (forcePollingMode_) {
      tickPollingMode();
      return;
    }

    if (!sqwPulsePending_) {
      logSqwFrequencyIfDue();
      return;
    }

    sqwPulsePending_ = false;
    if (++sqwPulseCount_ >= LOG_INTERVAL_SECONDS) {
      sqwPulseCount_ = 0;
      logRtcTime("SQW trigger:", rtc_.now());
    }
    logSqwFrequencyIfDue();
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
    sqwEdgeCount_++;
    sqwPulsePending_ = true;
  }

private:
  static constexpr uint8_t RTC_I2C_ADDRESS = Hardware::I2CAddress::DS3231;
  static constexpr uint8_t LOG_INTERVAL_SECONDS = 5;
  static constexpr unsigned long SQW_FREQ_LOG_INTERVAL_MS = 10000;

  void tickPollingMode() {
    const unsigned long nowMs = millis();
    if (static_cast<long>(nowMs - nextPollingLogAtMs_) < 0) return;

    nextPollingLogAtMs_ = nowMs + (LOG_INTERVAL_SECONDS * 1000UL);
    logRtcTime("RTC poll:", rtc_.now());
  }

  void logSqwFrequencyIfDue() {
    const unsigned long nowMs = millis();
    if (static_cast<long>(nowMs - nextSqwFrequencyLogAtMs_) < 0) return;

    const unsigned long elapsedMs = nowMs - lastSqwFrequencySampleAtMs_;
    const uint32_t pulseSnapshot = sqwEdgeCount_;
    const uint32_t pulseDelta = pulseSnapshot - lastSqwFrequencyPulseCount_;
    const float hz = elapsedMs > 0 ? (static_cast<float>(pulseDelta) * 1000.0f) / static_cast<float>(elapsedMs) : 0.0f;

    Serial.printf("[RTC] SQW frequency: %.3f Hz (%lu pulses/%lums)\n",
                  hz,
                  static_cast<unsigned long>(pulseDelta),
                  elapsedMs);

    lastSqwFrequencyPulseCount_ = pulseSnapshot;
    lastSqwFrequencySampleAtMs_ = nowMs;
    nextSqwFrequencyLogAtMs_ = nowMs + SQW_FREQ_LOG_INTERVAL_MS;
  }

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

    const unsigned long nowMs = millis();
    lastSqwFrequencySampleAtMs_ = nowMs;
    nextSqwFrequencyLogAtMs_ = nowMs + SQW_FREQ_LOG_INTERVAL_MS;
    lastSqwFrequencyPulseCount_ = sqwEdgeCount_;
  }

  void configurePollingMode() {
    rtc_.disable32K();
    rtc_.writeSqwPinMode(DS3231_OFF);
    status_.sqwConfigured = false;
    nextPollingLogAtMs_ = millis() + (LOG_INTERVAL_SECONDS * 1000UL);
  }

  RtcCrashRecovery crashRecovery_;
  RTC_DS3231 rtc_;
  RtcStatus status_ = {false, false, false, false, "Not initialized"};
  bool forcePollingMode_ = false;
  volatile bool sqwPulsePending_ = false;
  volatile uint32_t sqwEdgeCount_ = 0;
  uint8_t sqwPulseCount_ = 0;
  uint32_t lastSqwFrequencyPulseCount_ = 0;
  unsigned long lastSqwFrequencySampleAtMs_ = 0;
  unsigned long nextSqwFrequencyLogAtMs_ = 0;
  unsigned long nextPollingLogAtMs_ = 0;
};

static RtcDs3231 rtc;

static void IRAM_ATTR onSqwPulseIsr() {
  rtc.handleSqwPulse();
}

bool rtcBegin()                  { return rtc.begin(); }
void rtcTick()                   { rtc.tick(); }
RtcStatus rtcGetStatus()         { return rtc.getStatus(); }
String rtcGetCurrentTimeString() { return rtc.currentTimeString(); }

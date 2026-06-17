#include <Arduino.h>
#include <Wire.h>

#include "button.h"
#include "config.h"
#include "display.h"
#include "display_manager.h"
#include "hardware.h"
#include "log.h"
#include "rtc_ds3231.h"
#include "web_server.h"

// ─── Button handling ──────────────────────────────────────────────────────────

namespace {

void handleButtonEvent(ButtonEvent event) {
  switch (event) {
    case ButtonEvent::SHOW_NETWORK_INFO: {
      String ssid;
      String ip;
      networkGetInfo(ssid, ip);
      LOG_PRINTF("AP SSID: %s  IP: %s\n", ssid.c_str(), ip.c_str());
      break;
    }

    case ButtonEvent::SHOW_I2C_SCAN:
      i2cBusScanner.scan();
      break;

    case ButtonEvent::SHOW_RTC_STATUS: {
      const RtcStatus status = rtcGetStatus();
      LOG_PRINTF("present=%s powerLost=%s lowBattery=%s sqwConfigured=%s\n",
                 status.present ? "yes" : "no",
                 status.powerLost ? "yes" : "no",
                 status.lowBattery ? "yes" : "no",
                 status.sqwConfigured ? "yes" : "no");
      if (!status.error.isEmpty()) {
        LOG_PRINTF("error: %s\n", status.error.c_str());
      }
      break;
    }

    default:
      break;
  }
}

void processButtonEvents() {
  while (buttonHasEvent()) {
    handleButtonEvent(buttonNextEvent());
  }
}

}  // namespace

// ─── Arduino entry points ─────────────────────────────────────────────────────

void setup() {
  Serial.begin(74880);
  delay(500);
  LOG_PRINTLN("Starting up...");

  Wire.begin(Hardware::Pins::I2C_SDA, Hardware::Pins::I2C_SCL);
  Wire.setClock(100000);
  LOG_PRINTF("Initialized SDA=GPIO%u SCL=GPIO%u\n",
             Hardware::Pins::I2C_SDA,
             Hardware::Pins::I2C_SCL);

  if (rtcBegin()) {
    rtcBeginSqwProcessing();
  } else {
    LOG_PRINTF("Init failed: %s\n", rtcGetStatus().error.c_str());
  }
  i2cBusScanner.scan();

  ClockConfig cs = configManager.loadClockConfig();
  segmentDisplay.begin(cs.brightness);
  LOG_PRINTF("Mode %u, brightness %u\n", (unsigned)cs.activeMode, cs.brightness);

  displayManager.begin(cs);
  if (cs.splashMessage[0] != '\0') {
    displayManager.showSplash(cs.splashMessage);
  }

  const RtcStatus rtcStatus = rtcGetStatus();
  if (rtcStatus.lowBattery) {
    displayManager.showInfo("LOW BAT");
    LOG_PRINTLN("Low battery - showing info state");
  }

  WifiConfig cfg = configManager.loadWifiConfig();
  webBegin(cfg.ssid.c_str(), cfg.password.c_str());

  buttonBegin();
}

void loop() {
  uint32_t now = millis();
  buttonTick();
  processButtonEvents();
  if (rtcProcessSqwPulse()) {
    LOG_PRINTF("SQW trigger: state[%s]\n", displayManager.currentStateName());
  }

  displayManager.tick(now);
  webHandleClients();
}

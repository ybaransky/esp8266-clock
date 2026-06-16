#include <Arduino.h>
#include <Wire.h>

#include "button.h"
#include "config.h"
#include "hardware.h"
#include "rtc_ds3231.h"
#include "web.h"

namespace {

void handleButtonEvent(ButtonEvent event) {
  switch (event) {
    case ButtonEvent::SHOW_NETWORK_INFO: {
      String ssid;
      String ip;
      networkGetInfo(ssid, ip);
      Serial.printf("[NET] AP SSID: %s  IP: %s\n", ssid.c_str(), ip.c_str());
      break;
    }

    case ButtonEvent::SHOW_I2C_SCAN:
      i2cBusScanner.scan();
      break;

    case ButtonEvent::SHOW_RTC_STATUS: {
      const RtcStatus status = rtcGetStatus();
      Serial.printf("[RTC] present=%s powerLost=%s lowBattery=%s sqwConfigured=%s\n",
                    status.present ? "yes" : "no",
                    status.powerLost ? "yes" : "no",
                    status.lowBattery ? "yes" : "no",
                    status.sqwConfigured ? "yes" : "no");
      if (!status.error.isEmpty()) {
        Serial.printf("[RTC] error: %s\n", status.error.c_str());
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

void setup() {
  Serial.begin(74880);
  delay(500);
  Serial.println("\n[SETUP] Starting up...");

  Wire.begin(Hardware::Pins::I2C_SDA, Hardware::Pins::I2C_SCL);
  Wire.setClock(100000);
  Serial.printf("[I2C] Initialized SDA=GPIO%u SCL=GPIO%u\n",
                Hardware::Pins::I2C_SDA,
                Hardware::Pins::I2C_SCL);

  if (!rtcBegin()) {
    Serial.printf("[RTC] Init failed: %s\n", rtcGetStatus().error.c_str());
  }
  i2cBusScanner.scan();

  ApConfig cfg = configManager.loadApConfig();
  webBegin(cfg.ssid.c_str(), cfg.password.c_str());

  buttonBegin();
}


void loop() {
  buttonTick();
  buttonLedTick(millis());
  processButtonEvents();
  rtcTick();
  webHandleClients();
}

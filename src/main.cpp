#include <Arduino.h>
#include "button.h"
#include "config.h"
#include "hardware.h"
#include "rtc_ds3231.h"
#include "web.h"


void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n[SETUP] Starting up...");
  
  if (!rtcBegin()) {
    Serial.printf("[RTC] Init failed: %s\n", rtcGetStatus().error.c_str());
  }
  i2cScan();

  ApConfig cfg = loadApConfig();
  webBegin(cfg.ssid.c_str(), cfg.password.c_str());

  buttonBegin();
}


void loop() {
  // put your main code here, to run repeatedly:  static AppState state;
  unsigned long now = millis();

  buttonTick();
  buttonLedTick(now);
  rtcTick();
  webHandleClients();
}

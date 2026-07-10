#include <Arduino.h>
#include <Wire.h>

#include "button.h"
#include "clock_state.h"
#include "config.h"
#include "config_validation.h"
#include "display.h"
#include "display_manager.h"
#include "friday_mode.h"
#include "hardware.h"
#include "log.h"
#include "page_manager.h"
#include "rtc_ds3231.h"
#include "web_server.h"
#include "wifi_connection_manager.h"

//                                 123412341234
static const char kMsgNoRtc[]   = "  no rtc    ";
static const char kMsgLowBat[]  = "  LO BAT    ";

// --- Button handling ----------------------------------------------------------

namespace {

void printRtcErrorBanner(const char* detail) {
  Serial.println();
  Serial.println("############################");
  Serial.println("#          ERROR           #");
  Serial.println("#      RTC NOT FOUND       #");
  Serial.println("############################");
  if (detail != nullptr && detail[0] != '\0') {
    Serial.print("# ");
    Serial.println(detail);
  }
  Serial.println();
}

void checkRtcHealth(uint32_t nowMs) {
  static uint32_t lastCheckMs = 0;
  static bool wasHealthy = true;
  if (static_cast<long>(nowMs - lastCheckMs) < 2000L) return;
  lastCheckMs = nowMs;
  const bool healthy = rtcIsHealthy();
  if (!healthy) {
    if (wasHealthy) LOG_PRINTLN("RTC health lost");
    displayManager.showInfo(kMsgNoRtc);
  } else if (!wasHealthy) {
    // showInfo(kMsgNoRtc) has no expiration, so without this the "no rtc"
    // overlay would stay on screen forever after a transient health blip,
    // masking whatever the real base view (e.g. Friday's countdown)
    // resolves to underneath it.
    LOG_PRINTLN("RTC health restored");
    displayManager.clearOverlay();
  }
  wasHealthy = healthy;
}

// Logs whenever the mode or its view changes. For the three non-Friday
// modes the view never moves on its own - it's fixed by the mode. Friday
// mode is the one case where the view changes on its own, as
// FridayModeController cycles it between kClock and kCountdown; the mode
// itself stays "friday" throughout.
void logModeOrViewTransition() {
  static Mode lastMode = displayManager.activeMode();
  static View lastView = displayManager.activeView();

  const Mode mode = displayManager.activeMode();
  const View view = displayManager.activeView();
  if (mode == lastMode && view == lastView) return;

  LOG_PRINTF("mode/view: %s/%s -> %s/%s\n",
             modeName(lastMode), viewName(lastView),
             modeName(mode), viewName(view));
  lastMode = mode;
  lastView = view;
}

void handleButtonEvent(ButtonEvent event) {
  switch (event) {
    case ButtonEvent::SHOW_SSID: {
      String ssid;
      String ip;
      networkGetInfo(ssid, ip);
      LOG_PRINTF("Network SSID: %s\n", ssid.c_str());
      pageManager.showSsid(ssid);
      break;
    }

    case ButtonEvent::SHOW_IP_ADDRESS: {
      String ssid;
      String ip;
      networkGetInfo(ssid, ip);
      LOG_PRINTF("Network IP: %s\n", ip.c_str());
      pageManager.showIpAddress(ip);
      break;
    }

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

// --- Arduino entry points -----------------------------------------------------

void setup() {
  Serial.begin(74880);
  delay(500);
  LOG_PRINTF("Starting up...\n");
  printDeviceInfo();

  LOG_PRINTF("Built ========= %s %s ==========\n", __DATE__, __TIME__);
  Wire.begin(Hardware::Pins::I2C_SDA, Hardware::Pins::I2C_SCL);
  Wire.setClock(100000);
  LOG_PRINTF("Initialized SDA=GPIO%u SCL=GPIO%u\n",
             Hardware::Pins::I2C_SDA,
             Hardware::Pins::I2C_SCL);

  if (rtcBegin()) {
    rtcBeginSqwProcessing();
  } else {
    const RtcStatus status = rtcGetStatus();
    printRtcErrorBanner(status.error.c_str());
    LOG_PRINTF("Init failed: %s\n", status.error.c_str());
  }
  i2cBusScanner.scan();

  ClockConfig cs = configManager.loadClockConfig();
  segmentDisplay.begin(cs.brightness);
  LOG_PRINTF("Mode %u, brightness %u\n", (unsigned)cs.activeMode, cs.brightness);

  clockApplySettings(cs);
  if (cs.splashMessage[0] != '\0') {
    displayManager.showSplash(cs.splashMessage);
  }

  const RtcStatus rtcStatus = rtcGetStatus();
  if (!rtcStatus.present) {
    displayManager.showInfo(kMsgNoRtc);
    LOG_PRINTLN("RTC not found - showing no rtc");
  } else if (rtcStatus.lowBattery) {
    displayManager.showInfo(kMsgLowBat);
    LOG_PRINTLN("Low battery - showing info state");
  }

  WifiConfig cfg = configManager.loadWifiConfig();
  wifiConnectionManager.begin(cfg);
  webBegin();

  buttonBegin();
}

void loop() {
  uint32_t now = millis();
  buttonTick();
  processButtonEvents();
  if (rtcConsumeSqwPulse()) {
    // Re-phase rendering to the real second boundary: displayManager.tick()
    // below redraws immediately instead of waiting out its throttle window.
    displayManager.notifySecondBoundary();
    fridayModeTick(rtcGetNowCached());
    if (rtcIsLogIntervalDue()) {
      LOG_PRINTF("SQW: mode=%s view=%s\n",
                 modeName(displayManager.activeMode()),
                 viewName(displayManager.activeView()));
    }
  }

  logModeOrViewTransition();
  checkRtcHealth(now);
  displayManager.tick(now);
  wifiConnectionManager.tick();
  webHandleClients();
}

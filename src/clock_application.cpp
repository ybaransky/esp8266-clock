#include "clock_application.h"

#include <Wire.h>

#include "button.h"
#include "config.h"
#include "config_validation.h"
#include "display.h"
#include "display_manager.h"
#include "hardware.h"
#include "log.h"
#include "page_manager.h"
#include "rtc_ds3231.h"
#include "web_server.h"
#include "wifi_connection_manager.h"

namespace {

//                                 123412341234
const char kMsgNoRtc[]  = "  no rtc    ";
const char kMsgLowBat[] = "  LO BAT    ";

void printRtcErrorBanner(const char* detail) {
  Serial.println();
  Serial.println("############################");
  Serial.println("#          ERROR           #");
  Serial.println("#      RTC NOT FOUND       #");
  Serial.println("############################");
  if ((detail != nullptr) && (detail[0] != '\0')) {
    Serial.print("# ");
    Serial.println(detail);
  }
  Serial.println();
}

void handleButtonEvent(ButtonEvent event, PageManager& pageManager,
                       RtcService& rtc) {
  switch (event) {
    case ButtonEvent::kShowSsid: {
      String ssid;
      String ip;
      networkGetInfo(ssid, ip);
      LOG_PRINTF("Network SSID: %s", ssid.c_str());
      pageManager.showSsid(ssid);
      break;
    }

    case ButtonEvent::kShowIpAddress: {
      String ssid;
      String ip;
      networkGetInfo(ssid, ip);
      LOG_PRINTF("Network IP: %s", ip.c_str());
      pageManager.showIpAddress(ip);
      break;
    }

    case ButtonEvent::kShowRtcStatus: {
      const RtcStatus status = rtc.getStatus();
      LOG_PRINTF("present=%s powerLost=%s lowBattery=%s sqwConfigured=%s",
                 status.present ? "yes" : "no",
                 status.powerLost ? "yes" : "no",
                 status.lowBattery ? "yes" : "no",
                 status.sqwConfigured ? "yes" : "no");
      if (!status.error.isEmpty()) {
        LOG_PRINTF("error: %s", status.error.c_str());
      }
      break;
    }

    default:
      break;
  }
}

}  // namespace

// -----------------------------------------------------------------------------
// ClockApplication
// -----------------------------------------------------------------------------

ClockApplication::ClockApplication()
    : displayManager_(segmentDisplay_, rtc_),
      clockController_(displayManager_, rtc_),
      pageManager_(displayManager_) {}

void ClockApplication::begin() {
  Serial.begin(74880);
  delay(500);
  LOG_PRINTF("Starting up...");
  printDeviceInfo();

  LOG_PRINTF("Built ========= %s %s ==========", __DATE__, __TIME__);
  Wire.begin(Hardware::Pins::I2C_SDA, Hardware::Pins::I2C_SCL);
  Wire.setClock(100000);
  LOG_PRINTF("Initialized SDA=GPIO%u SCL=GPIO%u",
             Hardware::Pins::I2C_SDA,
             Hardware::Pins::I2C_SCL);

  if (rtc_.begin()) {
    rtc_.beginSqwProcessing();
  } else {
    const RtcStatus status = rtc_.getStatus();
    printRtcErrorBanner(status.error.c_str());
    LOG_PRINTF("Init failed: %s", status.error.c_str());
  }
  i2cBusScanner.scan();

  ClockConfig cs = configManager_.loadClockConfig();
  segmentDisplay_.begin(cs.display.brightness);
  LOG_PRINTF("Mode %u, brightness %u",
             (unsigned)cs.activeMode, cs.display.brightness);

  clockController_.applyConfig(cs);
  lastLoggedMode_ = displayManager_.activeMode();
  lastLoggedView_ = displayManager_.activeView();
  if (cs.messages.splash[0] != '\0') {
    displayManager_.showSplash(cs.messages.splash);
  }

  const RtcStatus rtcStatus = rtc_.getStatus();
  if (!rtcStatus.present) {
    displayManager_.showInfo(kMsgNoRtc);
    LOG_PRINTLN("RTC not found - showing no rtc");
  } else if (rtcStatus.lowBattery) {
    displayManager_.showInfo(kMsgLowBat);
    LOG_PRINTLN("Low battery - showing info state");
  }

  WifiConfig cfg = configManager_.loadWifiConfig();
  wifiConnectionManager_.begin(cfg);
  webBegin(clockController_, configManager_, wifiConnectionManager_, rtc_);

  buttonBegin();
}

void ClockApplication::tick(uint32_t nowMs) {
  const uint32_t tickStartUs = micros();
  buttonTick();
  processButtonEvents();
  if (rtc_.consumeSqwPulse()) {
    clockController_.onSecondBoundary(rtc_.getNowCached());
    if (rtc_.isLogIntervalDue()) {
      LOG_PRINTF("SQW: mode=%s view=%s",
                 modeName(displayManager_.activeMode()),
                 viewName(displayManager_.activeView()));
    }
  }

  logModeOrViewTransition();
  checkRtcHealth(nowMs);
  displayManager_.tick(nowMs);
  wifiConnectionManager_.tick();
  webHandleClients();

  const uint32_t tickUs = micros() - tickStartUs;
  if (tickUs > maxTickUs_) {
    maxTickUs_ = tickUs;
  }
  if (nowMs - lastTickReportMs_ >= 10000) {
    LOG_PRINTF("tick: max %lu.%lu ms in last 10s",
               static_cast<unsigned long>(maxTickUs_ / 1000),
               static_cast<unsigned long>((maxTickUs_ % 1000) / 100));
    lastTickReportMs_ = nowMs;
    maxTickUs_ = 0;
  }
}

void ClockApplication::processButtonEvents() {
  while (buttonHasEvent()) {
    handleButtonEvent(buttonNextEvent(), pageManager_, rtc_);
  }
}

void ClockApplication::checkRtcHealth(uint32_t nowMs) {
  if (static_cast<long>(nowMs - lastRtcHealthCheckMs_) < 2000L) return;
  lastRtcHealthCheckMs_ = nowMs;
  const bool healthy = rtc_.isHealthy();
  if (!healthy) {
    if (rtcWasHealthy_) LOG_PRINTLN("RTC health lost");
    displayManager_.showInfo(kMsgNoRtc);
  } else if (!rtcWasHealthy_) {
    // A no-RTC overlay has no expiration, so clear it after recovery to reveal
    // the current base view (including any Friday-mode phase correction).
    LOG_PRINTLN("RTC health restored");
    displayManager_.clearOverlay();
  }
  rtcWasHealthy_ = healthy;
}

void ClockApplication::logModeOrViewTransition() {
  const Mode mode = displayManager_.activeMode();
  const View view = displayManager_.activeView();
  if ((mode == lastLoggedMode_) && (view == lastLoggedView_)) return;

  LOG_PRINTF("mode/view: %s/%s -> %s/%s",
             modeName(lastLoggedMode_), viewName(lastLoggedView_),
             modeName(mode), viewName(view));
  lastLoggedMode_ = mode;
  lastLoggedView_ = view;
}

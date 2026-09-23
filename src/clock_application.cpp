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
#include "sound_player.h"
#include "web_server.h"
#include "wifi_connection_manager.h"

namespace {

constexpr uint32_t kRtcHealthPollIntervalMs = 2000;

//                                 123412341234
const char kMsgNoRtc[]  = "  no rtc    ";
const char kMsgLowBat[] = "  LO BAT    ";

// Deliberately raw Serial (not LOG_PRINTLN): this is a hand-aligned ASCII-art
// box meant to catch a human's eye during hardware bring-up, and the
// LOG_PRINTLN prefix (timestamp/stack/source) on every line would break the
// alignment. F() still keeps the literals in flash instead of RAM.
void printRtcErrorBanner(const char* detail) {
  Serial.println();
  Serial.println(F("############################"));
  Serial.println(F("#          ERROR           #"));
  Serial.println(F("#      RTC NOT FOUND       #"));
  Serial.println(F("############################"));
  if ((detail != nullptr) && (detail[0] != '\0')) {
    Serial.print(F("# "));
    Serial.println(detail);
  }
  Serial.println();
}

void handleButtonEvent(ButtonEvent event, PageManager& pageManager,
                       RtcService& rtc, const WebPortal& webPortal) {
  switch (event) {
    case ButtonEvent::kShowSsid: {
      String ssid;
      String ip;
      webPortal.getNetworkInfo(ssid, ip);
      LOG_PRINTF("Network SSID: %s", ssid.c_str());
      pageManager.showSsid(ssid);
      break;
    }

    case ButtonEvent::kShowIpAddress: {
      String ssid;
      String ip;
      webPortal.getNetworkInfo(ssid, ip);
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
      if (status.error[0] != 0) {
        LOG_PRINTF("error: %s", status.error);
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
      clockController_(displayManager_, rtc_, soundPlayer_),
      pageManager_(displayManager_),
      webPortal_(clockController_, configManager_, wifiConnectionManager_, rtc_) {}

void ClockApplication::begin() {
  Serial.begin(74880);
  delay(500);
  LOG_PRINTF("Starting up...");
  printDeviceInfo();
  LOG_PRINTF("Built ========= %s %s ==========", __DATE__, __TIME__);

  initializeRtc();
  initializeDisplayAndConfig();
  reportInitialRtcStatus(rtc_.getStatus());

  const WifiConfig& cfg = configManager_.wifiConfig();
  wifiConnectionManager_.begin(cfg);
  webPortal_.begin();

  buttonBegin();
}

void ClockApplication::initializeRtc() {
  Wire.begin(Hardware::Pins::I2C_SDA, Hardware::Pins::I2C_SCL);
  Wire.setClock(100000);
  LOG_PRINTF("Initialized SDA=GPIO%u SCL=GPIO%u",
             Hardware::Pins::I2C_SDA,
             Hardware::Pins::I2C_SCL);

  if (rtc_.begin()) {
    rtc_.beginSqwProcessing();
  } else {
    const RtcStatus status = rtc_.getStatus();
    printRtcErrorBanner(status.error);
    LOG_PRINTF("Init failed: %s", status.error);
  }
  i2cBusScanner.scan();
}

void ClockApplication::initializeDisplayAndConfig() {
  const ClockConfig& cs = configManager_.clockConfig();
  segmentDisplay_.begin(cs.display.brightness);
  soundPlayer_.begin();
  LOG_PRINTF("Mode %u, brightness %u",
             (unsigned)cs.activeMode, cs.display.brightness);

  clockController_.applyConfig(cs);
  lastLoggedMode_ = clockController_.activeMode();
  lastLoggedView_ = clockController_.activeView();
  if (cs.messages.splash[0] != '\0') {
    displayManager_.showSplash(cs.messages.splash);
  }
  // Plays under the splash rather than after it: the sound is non-blocking, so
  // the two overlap the way a startup chime is expected to.
  soundPlayer_.play(activeSoundName(cs.sound, cs.sound.startup), millis());
}

void ClockApplication::reportInitialRtcStatus(const RtcStatus& status) {
  if (!status.present) {
    displayManager_.showFault(kMsgNoRtc);
    LOG_PRINTLN("RTC not found - showing no rtc");
  } else if (status.lowBattery) {
    displayManager_.showFault(kMsgLowBat);
    LOG_PRINTLN("Low battery - showing info state");
  }
}

void ClockApplication::tick(uint32_t nowMs) {
  buttonTick();
  processButtonEvents();
  RtcTick rtcTick;
  if (rtc_.consumeSqwPulse(rtcTick)) {
    clockController_.onSecondBoundary(rtcTick);
    if (rtcTick.now.second() == 0) {
      LOG_PRINTF("SQW: mode=%s view=%s",
                 modeName(clockController_.activeMode()),
                 viewName(clockController_.activeView()));
    }
  }

  logModeOrViewTransition();
  checkRtcHealth(nowMs);
  nowMs = millis();
  displayManager_.tick(nowMs);
  soundPlayer_.tick(nowMs);
  wifiConnectionManager_.tick();
  webPortal_.handleClients();
}

void ClockApplication::processButtonEvents() {
  while (buttonHasEvent()) {
    handleButtonEvent(buttonNextEvent(), pageManager_, rtc_, webPortal_);
  }
}

void ClockApplication::checkRtcHealth(uint32_t nowMs) {
  if (static_cast<long>(nowMs - lastRtcHealthCheckMs_) <
      static_cast<long>(kRtcHealthPollIntervalMs)) return;
  lastRtcHealthCheckMs_ = nowMs;
  const bool healthy = rtc_.isHealthy();
  if (!healthy) {
    if (rtcWasHealthy_) LOG_PRINTLN("RTC health lost");
    displayManager_.showFault(kMsgNoRtc);
  } else {
    if (!rtcWasHealthy_) LOG_PRINTLN("RTC health restored");
    if (rtc_.getStatus().lowBattery) {
      displayManager_.showFault(kMsgLowBat);
    } else {
      displayManager_.clearFault();
    }
  }
  rtcWasHealthy_ = healthy;
}

void ClockApplication::logModeOrViewTransition() {
  const Mode mode = clockController_.activeMode();
  const View view = clockController_.activeView();
  if ((mode == lastLoggedMode_) && (view == lastLoggedView_)) return;

  LOG_PRINTF("mode/view: %s/%s -> %s/%s",
             modeName(lastLoggedMode_), viewName(lastLoggedView_),
             modeName(mode), viewName(view));
  lastLoggedMode_ = mode;
  lastLoggedView_ = view;
}

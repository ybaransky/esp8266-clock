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
#include "beep_player.h"
#include "web_server.h"
#include "wifi_connection_manager.h"

namespace {

constexpr uint32_t kRtcHealthPollIntervalMs = 2000;  // How often the "no rtc" overlay is re-evaluated.

//                                 123412341234
const char kNoRtcMessage[]      = "  no rtc    ";  // Fault overlay when the RTC is missing.
const char kLowBatteryMessage[] = "  LO BAT    ";  // Fault overlay for a low RTC backup battery.

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
    : m_displayManager(m_segmentDisplay, m_rtc),
      m_clockController(m_displayManager, m_rtc, m_beepPlayer),
      m_pageManager(m_displayManager),
      m_webPortal(m_clockController, m_configManager, m_wifiConnectionManager, m_rtc,
                 m_beepPlayer) {}

void ClockApplication::begin() {
  Serial.begin(74880);
  delay(500);
  LOG_PRINTF("Starting up...");
  printDeviceInfo();
  LOG_PRINTF("Built ========= %s %s ==========", __DATE__, __TIME__);

  initializeRtc();
  initializeDisplayAndConfig();
  reportInitialRtcStatus(m_rtc.getStatus());

  const WifiConfig& config = m_configManager.wifiConfig();
  m_wifiConnectionManager.begin(config);
  m_webPortal.begin();

  buttonBegin();
  // Start after blocking WiFi setup, so the loop can service the beep deadline.
  const SoundConfig& sound = m_configManager.clockConfig().sound;
  if (sound.enabled && sound.startupBeep) m_beepPlayer.beep(880, millis());
}

void ClockApplication::initializeRtc() {
  Wire.begin(Hardware::Pins::I2C_SDA, Hardware::Pins::I2C_SCL);
  Wire.setClock(100000);
  LOG_PRINTF("Initialized SDA=GPIO%u SCL=GPIO%u",
             Hardware::Pins::I2C_SDA,
             Hardware::Pins::I2C_SCL);

  if (m_rtc.begin()) {
    m_rtc.beginSqwProcessing();
  } else {
    const RtcStatus status = m_rtc.getStatus();
    printRtcErrorBanner(status.error);
    LOG_PRINTF("Init failed: %s", status.error);
  }
  i2cBusScanner.scan();
}

void ClockApplication::initializeDisplayAndConfig() {
  const ClockConfig& cs = m_configManager.clockConfig();
  m_segmentDisplay.begin(cs.display.brightness);
  m_beepPlayer.begin();
  LOG_PRINTF("Mode %u, brightness %u",
             (unsigned)cs.activeMode, cs.display.brightness);

  m_clockController.applyConfig(cs);
  m_lastLoggedMode = m_clockController.activeMode();
  m_lastLoggedView = m_clockController.activeView();
  if (cs.messages.splash[0] != '\0') {
    m_displayManager.showSplash(cs.messages.splash);
  }
}

void ClockApplication::reportInitialRtcStatus(const RtcStatus& status) {
  if (!status.present) {
    m_displayManager.showFault(kNoRtcMessage);
    LOG_PRINTLN("RTC not found - showing no rtc");
  } else if (status.lowBattery) {
    m_displayManager.showFault(kLowBatteryMessage);
    LOG_PRINTLN("Low battery - showing info state");
  }
}

void ClockApplication::tick(uint32_t nowMs) {
  buttonTick();
  processButtonEvents();
  RtcTick rtcTick;
  if (m_rtc.consumeSqwPulse(rtcTick)) {
    m_clockController.onSecondBoundary(rtcTick);
    if (rtcTick.now.second() == 0) {
      LOG_PRINTF("SQW: mode=%s view=%s",
                 modeName(m_clockController.activeMode()),
                 viewName(m_clockController.activeView()));
    }
  }

  logModeOrViewTransition();
  checkRtcHealth(nowMs);
  nowMs = millis();
  m_displayManager.tick(nowMs);
  m_beepPlayer.tick(nowMs);
  m_wifiConnectionManager.tick();
  m_webPortal.handleClients();
}

void ClockApplication::processButtonEvents() {
  while (buttonHasEvent()) {
    handleButtonEvent(buttonNextEvent(), m_pageManager, m_rtc, m_webPortal);
  }
}

void ClockApplication::checkRtcHealth(uint32_t nowMs) {
  if ((nowMs - m_lastRtcHealthCheckMs) < kRtcHealthPollIntervalMs) return;
  m_lastRtcHealthCheckMs = nowMs;
  const bool healthy = m_rtc.isHealthy();
  if (!healthy) {
    if (m_rtcWasHealthy) LOG_PRINTLN("RTC health lost");
    m_displayManager.showFault(kNoRtcMessage);
  } else {
    if (!m_rtcWasHealthy) LOG_PRINTLN("RTC health restored");
    if (m_rtc.getStatus().lowBattery) {
      m_displayManager.showFault(kLowBatteryMessage);
    } else {
      m_displayManager.clearFault();
    }
  }
  m_rtcWasHealthy = healthy;
}

void ClockApplication::logModeOrViewTransition() {
  const Mode mode = m_clockController.activeMode();
  const View view = m_clockController.activeView();
  if ((mode == m_lastLoggedMode) && (view == m_lastLoggedView)) return;

  LOG_PRINTF("mode/view: %s/%s -> %s/%s",
             modeName(m_lastLoggedMode), viewName(m_lastLoggedView),
             modeName(mode), viewName(view));
  m_lastLoggedMode = mode;
  m_lastLoggedView = view;
}

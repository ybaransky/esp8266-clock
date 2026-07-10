#include "config_update_service.h"

#include "clock_state.h"
#include "config.h"
#include "config_validation.h"
#include "format.h"
#include "zipcode.h"

namespace {

bool applyZipcode(const char* zipcode, char* destination, size_t destinationSize) {
  if (zipcode == nullptr || (zipcode[0] != '\0' && !isValidZipcode(zipcode))) {
    return false;
  }
  snprintf(destination, destinationSize, "%s", zipcode);
  return true;
}

bool applyModeAndFormats(JsonVariantConst display, JsonVariantConst modes, ClockConfig& cfg) {
  if (!display["activeMode"].isNull()) {
    Mode nextMode;
    const String mode = display["activeMode"] | "";
    if (!modeFromName(mode, &nextMode)) {
      return false;
    }
    cfg.activeMode = nextMode;
  }

  if (!modes["countdown"]["format"].isNull()) {
    cfg.countdownFmt = sanitizeFormatIndex(
        kFmtGroupCountdown, modes["countdown"]["format"].as<int>(), cfg.countdownFmt);
  }
  if (!modes["countup"]["format"].isNull()) {
    cfg.countupFmt = sanitizeFormatIndex(
        kFmtGroupCountUp, modes["countup"]["format"].as<int>(), cfg.countupFmt);
  }
  if (!modes["clock"]["format"].isNull()) {
    cfg.clockFmt = sanitizeFormatIndex(
        kFmtGroupClock, modes["clock"]["format"].as<int>(), cfg.clockFmt);
  }
  if (!modes["friday"]["clockFormat"].isNull()) {
    cfg.fridayClockFmt = sanitizeFormatIndex(
        kFmtGroupClock, modes["friday"]["clockFormat"].as<int>(), cfg.fridayClockFmt);
  }
  if (!modes["friday"]["toFridaySunsetFormat"].isNull()) {
    cfg.fridayToFridaySunsetFmt = sanitizeFormatIndex(
        kFmtGroupCountdown,
        modes["friday"]["toFridaySunsetFormat"].as<int>(),
        cfg.fridayToFridaySunsetFmt);
  }
  if (!modes["friday"]["toSaturdaySunsetFormat"].isNull()) {
    cfg.fridayToSatSunsetFmt = sanitizeFormatIndex(
        kFmtGroupCountdown,
        modes["friday"]["toSaturdaySunsetFormat"].as<int>(),
        cfg.fridayToSatSunsetFmt);
  }
  if (!display["brightness"].isNull()) {
    cfg.brightness = sanitizeBrightness(display["brightness"].as<int>());
  }
  if (!display["clock12Hour"].isNull()) {
    cfg.clockUse12Hour = display["clock12Hour"].as<bool>();
  }
  if (!modes["countdown"]["end"].isNull()) {
    snprintf(cfg.countdownDatetime,
             sizeof(cfg.countdownDatetime),
             "%s",
             modes["countdown"]["end"].as<const char*>());
  }
  if (!modes["countup"]["start"].isNull()) {
    snprintf(cfg.countupDatetime,
             sizeof(cfg.countupDatetime),
             "%s",
             modes["countup"]["start"].as<const char*>());
  }

  return true;
}

void applyMessageFields(JsonVariantConst messages, ClockConfig& cfg) {
  if (!messages["splash"].isNull()) {
    sanitizeDisplayMessage(messages["splash"].as<const char*>(),
                           cfg.splashMessage,
                           sizeof(cfg.splashMessage));
  }
  if (!messages["final"].isNull()) {
    sanitizeDisplayMessage(messages["final"].as<const char*>(),
                           cfg.finalMessage,
                           sizeof(cfg.finalMessage));
  }
}

bool applyLocationFields(JsonVariantConst location, ClockConfig& cfg) {
  if (!location["latitude"].isNull()) {
    cfg.location.latitude = location["latitude"].as<float>();
  }
  if (!location["longitude"].isNull()) {
    cfg.location.longitude = location["longitude"].as<float>();
  }
  if (!location["zipcode"].isNull()) {
    if (!applyZipcode(location["zipcode"].as<const char*>(),
                      cfg.location.zipcode,
                      sizeof(cfg.location.zipcode))) {
      return false;
    }
  }
  return true;
}

bool applySunsetFields(JsonVariantConst sunset, ClockConfig& cfg) {
  if (!sunset["latitude"].isNull()) {
    cfg.sunsetTest.latitude = sunset["latitude"].as<float>();
  }
  if (!sunset["longitude"].isNull()) {
    cfg.sunsetTest.longitude = sunset["longitude"].as<float>();
  }
  if (!sunset["zipcode"].isNull()) {
    if (!applyZipcode(sunset["zipcode"].as<const char*>(),
                      cfg.sunsetTest.zipcode,
                      sizeof(cfg.sunsetTest.zipcode))) {
      return false;
    }
  }
  return true;
}

void applyTimezoneFields(JsonVariantConst time, ClockConfig& cfg) {
  JsonVariantConst timezone = time["timezone"];
  if (!timezone["name"].isNull()) {
    sanitizePrintableText(timezone["name"].as<const char*>(),
                          cfg.timezone,
                          sizeof(cfg.timezone));
  }
  if (!timezone["utcOffsetMinutes"].isNull()) {
    cfg.utcOffsetMinutes = sanitizeUtcOffsetMinutes(timezone["utcOffsetMinutes"].as<int>());
  }
  if (!time["dst"].isNull()) {
    cfg.dst = time["dst"].as<bool>();
  }
}

}  // namespace

SaveConfigResult ConfigUpdateService::applySavePayload(const JsonDocument& doc) {
  ClockConfig clockConfig = configManager.loadClockConfig();
  JsonVariantConst root = doc.as<JsonVariantConst>();
  JsonVariantConst display = root["display"];
  JsonVariantConst modes = display["modes"];
  JsonVariantConst messages = display["messages"];
  JsonVariantConst time = root["time"];
  JsonVariantConst location = root["location"];
  JsonVariantConst sunset = root["sunset"];
  JsonVariantConst wifi = root["wifi"];
  JsonVariantConst station = wifi["station"];
  JsonVariantConst accessPoint = wifi["accessPoint"];

  if (!applyModeAndFormats(display, modes, clockConfig)) {
    return {false, false, "{\"error\":\"Invalid active mode\"}"};
  }
  applyMessageFields(messages, clockConfig);

  if (!applyLocationFields(location, clockConfig) ||
      !applySunsetFields(sunset, clockConfig)) {
    return {false, false, "{\"error\":\"ZIP code must be 5 digits\"}"};
  }

  applyTimezoneFields(time, clockConfig);

  configManager.saveClockConfig(clockConfig);
  clockApplySettings(configManager.sanitizeClockConfig(clockConfig));

  bool wifiChanged = false;
  if (!station["ssid"].isNull() || !station["password"].isNull() ||
      !accessPoint["ssid"].isNull() || !accessPoint["password"].isNull()) {
    WifiConfig wifiConfig = configManager.loadWifiConfig();
    if (!station["ssid"].isNull()) {
      wifiConfig.staSsid = station["ssid"].as<String>();
    }
    if (!station["password"].isNull()) {
      wifiConfig.staPassword = station["password"].as<String>();
    }
    if (!accessPoint["ssid"].isNull()) {
      wifiConfig.apSsid = accessPoint["ssid"].as<String>();
    }
    if (!accessPoint["password"].isNull()) {
      wifiConfig.apPassword = accessPoint["password"].as<String>();
    }
    configManager.saveWifiConfig(wifiConfig);
    wifiChanged = true;
  }

  return {true, wifiChanged, nullptr};
}

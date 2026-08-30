#include "config_validation.h"

#include "defaults.h"
#include "log.h"

namespace {

// 802.11 limits that softAP() enforces. A refused AP is unrecoverable when no
// station credentials are configured - nothing else serves the config pages.
constexpr size_t kMaxApSsidLength     = 32;
constexpr size_t kMinApPasswordLength = 8;
constexpr size_t kMaxApPasswordLength = 63;

}  // namespace

const char* modeName(Mode mode) {
  switch (mode) {
    case kModeCountdown: return "countdown";
    case kModeCountup:   return "countup";
    case kModeClock:     return "clock";
    case kModeFriday:    return "friday";
    case kModeTrading:   return "trading";
  }
  return "countdown";
}

Mode sanitizeMode(int rawMode, Mode fallback) {
  if ((rawMode < static_cast<int>(kModeCountdown)) ||
      (rawMode > static_cast<int>(kModeTrading))) {
    return fallback;
  }
  return static_cast<Mode>(rawMode);
}

bool modeFromName(const String& name, Mode* mode) {
  if (mode == nullptr) {
    return false;
  }
  if (name == "countdown") {
    *mode = kModeCountdown;
    return true;
  }
  if (name == "countup") {
    *mode = kModeCountup;
    return true;
  }
  if (name == "clock") {
    *mode = kModeClock;
    return true;
  }
  if (name == "friday") {
    *mode = kModeFriday;
    return true;
  }
  if (name == "trading") {
    *mode = kModeTrading;
    return true;
  }
  return false;
}

uint8_t sanitizeFormatIndex(FormatGroup group, int rawIndex, uint8_t fallback) {
  if ((group < 0) || (group >= kFmtGroupCount) || (rawIndex < 0)) {
    return fallback;
  }
  const uint8_t index = static_cast<uint8_t>(rawIndex);
  return index < displayFormatCount(group) ? index : fallback;
}

uint8_t sanitizeOptionalFormatIndex(FormatGroup group, int rawIndex,
                                    uint8_t fallback) {
  if ((rawIndex == kSameFormat) || (rawIndex == -1)) {
    return kSameFormat;
  }
  return sanitizeFormatIndex(group, rawIndex, fallback);
}

uint8_t sanitizeBrightness(int rawBrightness) {
  return static_cast<uint8_t>(constrain(rawBrightness, 0, 7));
}

uint8_t sanitizeBlinkMinutes(int rawMinutes) {
  static constexpr int kMaxBlinkMinutes = 240;
  return static_cast<uint8_t>(constrain(rawMinutes, 0, kMaxBlinkMinutes));
}

int16_t sanitizeUtcOffsetMinutes(int rawOffsetMinutes) {
  return static_cast<int16_t>(constrain(rawOffsetMinutes, -840, 840));
}

uint8_t sanitizeVolumePercent(int rawPercent) {
  return static_cast<uint8_t>(constrain(rawPercent, 0, 100));
}

uint16_t sanitizeBoundaryDurationSeconds(int rawSeconds) {
  return static_cast<uint16_t>(constrain(rawSeconds, 4, 1200));
}

uint16_t sanitizeBoundaryFrequencyHz(int rawFrequencyHz) {
  return static_cast<uint16_t>(constrain(rawFrequencyHz, 100, 5000));
}

uint8_t sanitizeBoundaryStartingBeatsHz(int rawBeatsHz) {
  return static_cast<uint8_t>(constrain(rawBeatsHz, 1, 10));
}

const char* activeSoundName(const SoundConfig& sound, const char* name) {
  if (!sound.enabled || (name == nullptr)) return "";
  return name;
}

void sanitizeLocationInfo(LocationInfo& info) {
  info.latitude = constrain(info.latitude, -90.0f, 90.0f);
  info.longitude = constrain(info.longitude, -180.0f, 180.0f);
}

void sanitizePrintableText(const char* input, char* output, size_t outputSize) {
  if ((output == nullptr) || (outputSize == 0)) {
    return;
  }

  if (input == nullptr) {
    output[0] = '\0';
    return;
  }

  size_t out = 0;
  for (size_t in = 0; input[in] != '\0' && out < outputSize - 1; ++in) {
    const uint8_t value = static_cast<uint8_t>(input[in]);
    if ((value >= 32) && (value <= 126)) {
      output[out++] = static_cast<char>(value);
    }
  }
  output[out] = '\0';
}

void sanitizeDisplayMessage(const char* input, char* output, size_t outputSize) {
  static constexpr size_t kDisplayMessageChars = 12;

  if ((output == nullptr) || (outputSize == 0)) {
    return;
  }

  if (input == nullptr) {
    output[0] = '\0';
    return;
  }

  char clean[kDisplayMessageChars + 1];
  sanitizePrintableText(input, clean, sizeof(clean));
  snprintf(output, outputSize, "%s", clean);
}

void sanitizeWifiConfig(WifiConfig& wifi) {
  wifi.apSsid.trim();
  if (wifi.apSsid.length() > kMaxApSsidLength) {
    LOG_PRINTF("AP SSID is %u characters (max %u) - reverting to the derived default",
               static_cast<unsigned>(wifi.apSsid.length()),
               static_cast<unsigned>(kMaxApSsidLength));
    wifi.apSsid = "";
  }

  const size_t passwordLength = wifi.apPassword.length();
  if ((passwordLength < kMinApPasswordLength) ||
      (passwordLength > kMaxApPasswordLength)) {
    LOG_PRINTF("AP password is %u characters (WPA2 needs %u-%u) - reverting to the default",
               static_cast<unsigned>(passwordLength),
               static_cast<unsigned>(kMinApPasswordLength),
               static_cast<unsigned>(kMaxApPasswordLength));
    wifi.apPassword = defaultWifiConfig().apPassword;
  }
}

#include "config_validation.h"

const char* modeName(Mode mode) {
  switch (mode) {
    case kModeCountdown: return "countdown";
    case kModeCountup:   return "countup";
    case kModeClock:     return "clock";
    case kModeFriday:    return "friday";
  }
  return "countdown";
}

Mode sanitizeMode(int rawMode, Mode fallback) {
  if (rawMode < static_cast<int>(kModeCountdown) ||
      rawMode > static_cast<int>(kModeFriday)) {
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
  return false;
}

uint8_t sanitizeFormatIndex(FormatGroup group, int rawIndex, uint8_t fallback) {
  if (group < 0 || group >= kFmtGroupCount || rawIndex < 0) {
    return fallback;
  }
  const uint8_t index = static_cast<uint8_t>(rawIndex);
  return index < formatCount(group) ? index : fallback;
}

uint8_t sanitizeBrightness(int rawBrightness) {
  return static_cast<uint8_t>(constrain(rawBrightness, 0, 7));
}

int16_t sanitizeUtcOffsetMinutes(int rawOffsetMinutes) {
  return static_cast<int16_t>(constrain(rawOffsetMinutes, -840, 840));
}

void sanitizePrintableText(const char* input, char* output, size_t outputSize) {
  if (output == nullptr || outputSize == 0) {
    return;
  }

  if (input == nullptr) {
    output[0] = '\0';
    return;
  }

  size_t out = 0;
  for (size_t in = 0; input[in] != '\0' && out < outputSize - 1; ++in) {
    const uint8_t value = static_cast<uint8_t>(input[in]);
    if (value >= 32 && value <= 126) {
      output[out++] = static_cast<char>(value);
    }
  }
  output[out] = '\0';
}

void sanitizeDisplayMessage(const char* input, char* output, size_t outputSize) {
  static constexpr size_t kDisplayMessageChars = 12;

  if (output == nullptr || outputSize == 0) {
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

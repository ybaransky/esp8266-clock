#pragma once

#include <Arduino.h>
#include <RTClib.h>

#include "display.h"

enum FormatGroup : uint8_t {
  kFmtGroupCountdown = 0,
  kFmtGroupCountUp = 1,
  kFmtGroupClock = 2,
  kFmtGroupCount = 3,
};

enum class RefreshRate : uint8_t {
  kOneSecond,
  kOneTenth,
};

enum class ColonAnimation : uint8_t {
  kNone,
  kBlinking,
};

// Describes a display format and the scheduling behavior derived from its panel shapes.
struct DisplayFormatInfo {
  const char* label;  // Human-readable token layout shown in the UI.
  RefreshRate refreshRate;  // Minimum cadence needed by the renderer.
  ColonAnimation colonAnimation;  // Colon cadence required by the renderer.
};

uint8_t displayFormatCount(FormatGroup group);
DisplayFormatInfo displayFormatInfo(FormatGroup group, uint8_t index);

DisplayFrame renderCountingFormat(uint8_t index,
                                  long totalSeconds,
                                  uint8_t tenths);
DisplayFrame renderClockFormat(uint8_t index,
                               const DateTime& now,
                               bool use12Hour,
                               uint8_t tenths,
                               bool colonVisible);

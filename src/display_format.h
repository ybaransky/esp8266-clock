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

struct DisplayFormatInfo {
  const char* label;
  bool hasTenths;
  bool blinksColon;
};

uint8_t displayFormatCount(FormatGroup group);
const DisplayFormatInfo& displayFormatInfo(FormatGroup group, uint8_t index);

DisplayFrame renderCountingFormat(uint8_t index,
                                  long totalSeconds,
                                  uint8_t tenths);
DisplayFrame renderClockFormat(uint8_t index,
                               const DateTime& now,
                               bool use12Hour,
                               uint8_t tenths,
                               bool colonVisible);

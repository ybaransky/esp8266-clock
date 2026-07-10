#pragma once

#include <Arduino.h>

// Format group indices.
enum FormatGroup : uint8_t {
  kFmtGroupCountdown     = 0,
  kFmtGroupCountUp       = 1,
  kFmtGroupClock         = 2,
  kFmtGroupCount         = 3
};

// Per-format rendering properties.
struct FormatMetadata {
  bool hasTenths;    // True when the format displays sub-second tenths (drives 100ms refresh).
  bool blinkColon;   // True when the hh:mm separator should blink once per second (clock only).
};

// A format string paired with its rendering properties. Keeping these together
// eliminates the index-mismatch bug class that arose from parallel arrays.
struct FormatEntry {
  const char* format;
  FormatMetadata meta;
};

// Public format accessors. Countdown and CountUp share the same underlying
// table, resolved internally by group.
uint8_t formatCount(FormatGroup group);
const char* getFormat(FormatGroup group, uint8_t index);
const FormatMetadata* getFormatMeta(FormatGroup group, uint8_t index);

// For counting formats, automatically resolve hhh:mm overflow to a compatible
// fallback format when totalHours exceeds 99. Resolution is semantic and does
// not depend on hardcoded indices.
uint8_t resolveCountingOverflowIndex(uint8_t index, int totalHours);

// Persistent mode stored in config.json and selected from the web UI - what
// the clock is fundamentally set to do. Distinct from View (what content is
// currently being rendered) and Overlay (a temporary layer on top of it) -
// see display_manager.h.
enum Mode : uint8_t {
  kModeCountdown = 0,
  kModeCountup   = 1,
  kModeClock     = 2,
  kModeFriday    = 3,
};

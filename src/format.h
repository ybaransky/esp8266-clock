#pragma once

#include <Arduino.h>

// Format group indices.
enum FormatGroup : uint8_t {
  kFmtGroupCountdown     = 0,
  kFmtGroupCountUp       = 1,
  kFmtGroupClock         = 2,
  kFmtGroupCount         = 3
};

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

// Format catalog accessors, implemented in clock_format.cpp next to the
// tables. Each table entry pairs the UI label with the three per-row render
// ops, so adding or reordering a format touches exactly one table row.
// Countdown and CountUp share the same underlying table.
uint8_t formatCount(FormatGroup group);

// UI label shown by the web configurator, e.g. "dd D |  hh:mm |  ss:u".
const char* getFormat(FormatGroup group, uint8_t index);

// True when the format renders sub-second tenths (drives the 100ms refresh).
// Derived from the format's render ops, never stored separately.
bool formatHasTenths(FormatGroup group, uint8_t index);

// True for clock formats whose hh:mm separator blinks once per second.
// Derived from the format's render ops.
bool clockFormatBlinksColon(uint8_t index);

// For counting formats, automatically resolve hhh:mm overflow to a compatible
// fallback format when totalHours exceeds 99. Resolution is semantic (based
// on render ops) and does not depend on hardcoded indices.
uint8_t resolveCountingOverflowIndex(uint8_t index, int totalHours);

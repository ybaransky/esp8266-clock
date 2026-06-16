#pragma once
#include <Arduino.h>

// ── Format group indices ──────────────────────────────────────────────────────
enum FormatGroup : uint8_t {
  kFmtGroupCountdown     = 0,
  kFmtGroupCountUp       = 1,
  kFmtGroupClock         = 2,
  kFmtGroupJustification = 3,
  kFmtGroupCount         = 4
};

// Number of entries in each group
extern const uint8_t kFormatGroupSizes[kFmtGroupCount];

// Master lookup: kFormatGroups[group][index] → format string
extern const char* const* const kFormatGroups[kFmtGroupCount];

// Convenience accessors
inline uint8_t     formatCount(FormatGroup g)          { return kFormatGroupSizes[g]; }
inline const char* getFormat(FormatGroup g, uint8_t i) {
  return (i < kFormatGroupSizes[g]) ? kFormatGroups[g][i] : nullptr;
}

// ── BaseMode ──────────────────────────────────────────────────────────────────
// Persistent display mode stored in config.json and selected from the web UI.
// Overlay modes (splash, info, demo) are transient and managed separately.
enum BaseMode : uint8_t {
  kBaseCountdown = 0,
  kBaseCountup   = 1,
  kBaseClock     = 2,
};


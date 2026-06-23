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

// Number of entries in each group.
extern const uint8_t kFormatGroupSizes[kFmtGroupCount];

// Master lookup: kFormatEntries[group][index] -> FormatEntry.
extern const FormatEntry* const kFormatEntries[kFmtGroupCount];

inline uint8_t formatCount(FormatGroup group) {
  return kFormatGroupSizes[group];
}

inline const char* getFormat(FormatGroup group, uint8_t index) {
  return (index < kFormatGroupSizes[group]) ? kFormatEntries[group][index].format : nullptr;
}

inline const FormatMetadata* getFormatMeta(FormatGroup group, uint8_t index) {
  return (index < kFormatGroupSizes[group]) ? &kFormatEntries[group][index].meta : nullptr;
}

// Persistent display mode stored in config.json and selected from the web UI.
// Temporary display behavior is represented by DisplayState transitions.
enum PersistentMode : uint8_t {
  kPersistentCountdown = 0,
  kPersistentCountup   = 1,
  kPersistentClock     = 2,
  kPersistentFriday    = 3,
};

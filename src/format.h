#pragma once

#include <Arduino.h>

// Format group indices.
enum FormatGroup : uint8_t {
  kFmtGroupCountdown     = 0,
  kFmtGroupCountUp       = 1,
  kFmtGroupClock         = 2,
  kFmtGroupCount         = 3
};

// Per-format rendering properties. Lives next to the format string tables it describes.
struct FormatMetadata {
  bool hasTenths;    // True when the format displays sub-second tenths (drives 100ms refresh).
  bool blinkColon;   // True when the hh:mm separator should blink once per second (clock only).
};

// Number of entries in each group.
extern const uint8_t kFormatGroupSizes[kFmtGroupCount];

// Master lookup: kFormatGroups[group][index] -> format string.
extern const char* const* const kFormatGroups[kFmtGroupCount];

// Metadata lookup: kFormatGroupMeta[group][index] -> FormatMetadata.
extern const FormatMetadata* const kFormatGroupMeta[kFmtGroupCount];

inline uint8_t formatCount(FormatGroup group) {
  return kFormatGroupSizes[group];
}

inline const char* getFormat(FormatGroup group, uint8_t index) {
  return (index < kFormatGroupSizes[group]) ? kFormatGroups[group][index] : nullptr;
}

inline const FormatMetadata* getFormatMeta(FormatGroup group, uint8_t index) {
  return (index < kFormatGroupSizes[group]) ? &kFormatGroupMeta[group][index] : nullptr;
}

// Persistent display mode stored in config.json and selected from the web UI.
// Temporary display behavior is represented by DisplayState transitions.
enum PersistentMode : uint8_t {
  kPersistentCountdown = 0,
  kPersistentCountup   = 1,
  kPersistentClock     = 2,
  kPersistentFriday    = 3,
};

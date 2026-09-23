#pragma once

#include <Arduino.h>
#include <RTClib.h>

#include "display_frame.h"

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

// Row widths for the flash-resident key and label tables. Both keys and labels
// live in PROGMEM, so these also size the scratch buffers callers copy into.
static constexpr size_t kFormatKeyLength = 24;
static constexpr size_t kFormatLabelLength = 24;

// Describes the scheduling behavior derived from a format's panel shapes.
// The label is not here: it lives in flash and is fetched with
// displayFormatLabel(), so this struct stays free of a pointer nobody can
// dereference directly.
struct DisplayFormatInfo {
  RefreshRate refreshRate;  // Minimum cadence needed by the renderer.
  ColonAnimation colonAnimation;  // Colon cadence required by the renderer.
};

uint8_t displayFormatCount(FormatGroup group);
DisplayFormatInfo displayFormatInfo(FormatGroup group, uint8_t index);

// A format's stable identity: what /config.json stores and what the web
// dropdowns post. Unlike an index it survives rows being added, removed, or
// reordered, so editing the catalog can never silently repoint a saved
// selection at a different format. Keys are unique within a group (the same
// key may appear in both the counting and clock tables; every lookup is
// group-scoped). Out-of-range indexes yield the group's first key, matching
// how the renderers fall back.
//
// Copies into `out` because the table is in flash; pass kFormatKeyLength.
void displayFormatKey(FormatGroup group, uint8_t index, char* out,
                      size_t outSize);

// The human-readable token layout shown in the UI. Also flash-resident; pass
// kFormatLabelLength.
void displayFormatLabel(FormatGroup group, uint8_t index, char* out,
                        size_t outSize);

// Resolves a stored key back to its index. False for a null, empty, or
// unknown key, which leaves *index untouched so the caller keeps its default.
bool displayFormatIndexForKey(FormatGroup group, const char* key,
                              uint8_t* index);

DisplayFrame renderCountingFormat(uint8_t index,
                                  long totalSeconds,
                                  uint8_t tenths);
DisplayFrame renderClockFormat(uint8_t index,
                               const DateTime& now,
                               bool use12Hour,
                               uint8_t tenths,
                               bool colonVisible);

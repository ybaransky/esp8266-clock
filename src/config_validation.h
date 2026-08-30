#pragma once

#include <Arduino.h>

#include "config.h"
#include "display_format.h"

const char* modeName(Mode mode);
Mode sanitizeMode(int rawMode, Mode fallback);
bool modeFromName(const String& name, Mode* mode);
uint8_t sanitizeFormatIndex(FormatGroup group, int rawIndex, uint8_t fallback);
// Like sanitizeFormatIndex, but also accepts kSameFormat (or -1) meaning
// "no secondary format selected".
uint8_t sanitizeOptionalFormatIndex(FormatGroup group, int rawIndex,
                                    uint8_t fallback);
uint8_t sanitizeBrightness(int rawBrightness);
// Clamps a blink-window length in minutes to 0 (disabled) through 240.
uint8_t sanitizeBlinkMinutes(int rawMinutes);
int16_t sanitizeUtcOffsetMinutes(int rawOffsetMinutes);
// Clamps a loudness percentage to 0 through 100.
uint8_t sanitizeVolumePercent(int rawPercent);
uint16_t sanitizeBoundaryDurationSeconds(int rawSeconds);
uint16_t sanitizeBoundaryFrequencyHz(int rawFrequencyHz);
uint8_t sanitizeBoundaryStartingBeatsHz(int rawBeatsHz);
// Resolves a configured cue name against the master sound switch: a disabled
// switch reads as "no sound" for every event, so no scheduling or rendering
// code has to test the flag - an empty name already means silence everywhere.
const char* activeSoundName(const SoundConfig& sound, const char* name);
// Clamps latitude/longitude to valid ranges in place. Zipcode is untouched -
// callers that accept a new zipcode validate it with isValidZipcode instead.
void sanitizeLocationInfo(LocationInfo& info);
// Both text sanitizers support input == output (in-place sanitization):
// sanitizePrintableText compacts forward, so its write index never passes its
// read index, and sanitizeDisplayMessage stages through a local buffer.
// ConfigManager::sanitizeClockConfig relies on this to avoid struct copies.
void sanitizePrintableText(const char* input, char* output, size_t outputSize);
void sanitizeDisplayMessage(const char* input, char* output, size_t outputSize);
// Keeps the fallback access point startable: clears an over-long AP SSID back
// to the empty "derive from the soft-AP MAC" sentinel, and replaces a password
// softAP() would reject with the default. An empty SSID is left alone - it is
// the sentinel, not an error. Station credentials are untouched; empty is
// meaningful there too (it selects AP-only mode).
void sanitizeWifiConfig(WifiConfig& wifi);

#pragma once

#include <Arduino.h>

#include "config.h"
#include "format.h"

PersistentMode sanitizePersistentMode(int rawMode, PersistentMode fallback);
bool persistentModeFromName(const String& name, PersistentMode* mode);
uint8_t sanitizeFormatIndex(FormatGroup group, int rawIndex, uint8_t fallback);
uint8_t sanitizeBrightness(int rawBrightness);
int16_t sanitizeUtcOffsetMinutes(int rawOffsetMinutes);
void sanitizePrintableText(const char* input, char* output, size_t outputSize);
void sanitizeDisplayMessage(const char* input, char* output, size_t outputSize);

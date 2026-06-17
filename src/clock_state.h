#pragma once
#include "config.h"

// Public clock state API defined by DisplayManager and called from web_server.cpp.
void clockApplySettings(const ClockConfig& cfg);
void clockSetBrightness(uint8_t brightness);
void clockTriggerDemo();
void clockShowMessagePreview(const char* msg);
void clockShowInfo(const char* msg, int32_t durationMs = FOREVER);
void clockClearInfo();

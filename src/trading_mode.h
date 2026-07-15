#pragma once

#include <RTClib.h>

#include "config.h"

class DisplayManager;

// Applies the latest Trading-mode format and announcement settings.
void tradingModeApplySettings(const ClockConfig& config);

// Clears the prior phase so a browser time jump cannot look like a live boundary.
void tradingModeResetSchedule();

// Recomputes the next weekday 09:30/16:00 Eastern-time boundary every RTC second.
void tradingModeTick(const DateTime& now, DisplayManager& displayManager);

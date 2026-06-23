#pragma once

#include <RTClib.h>
#include "config.h"

// Called from clockApplySettings() whenever config changes.
void fridayModeApplySettings(const ClockConfig& config);

// Forces sunset targets to be recomputed on the next tick.
// Call after rtcSetNow() so a time change is reflected immediately.
void fridayModeResetSunsetCache();

// Called every minute (on SQW log-interval pulse) from main loop.
// Self-gates: does nothing unless activeMode == kPersistentFriday.
void fridayModeTick(const DateTime& now);

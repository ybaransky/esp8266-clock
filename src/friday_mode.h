#pragma once

#include <RTClib.h>
#include "config.h"

// Called from clockApplySettings() whenever config changes.
void fridayModeApplySettings(const ClockConfig& config);

// Forces sunset targets to be recomputed on the next tick.
// Call after rtcSetNow() so a time change is reflected immediately.
void fridayModeResetSunsetCache();

// Called every real RTC second (on each SQW pulse, via rtcConsumeSqwPulse())
// from the main loop. Self-gates: does nothing unless
// activeMode == kModeFriday, and short-circuits when the phase hasn't
// changed since the last call.
void fridayModeTick(const DateTime& now);

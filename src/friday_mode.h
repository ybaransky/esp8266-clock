#pragma once

#include <RTClib.h>
#include "config.h"

// Called from clockApplySettings() whenever config changes.
void fridayModeApplySettings(const ClockConfig& config);

// Called every second (on SQW pulse) from main loop.
// Self-gates: does nothing unless activeMode == kPersistentFriday.
void fridayModeTick(const DateTime& now);

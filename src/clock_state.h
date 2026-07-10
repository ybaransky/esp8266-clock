#pragma once
#include "config.h"

// Applies a new ClockConfig everywhere it matters: DisplayManager (what is
// shown) and FridayModeController (phase schedule). Defined in
// display_manager.cpp. Callers needing a single display action (demo,
// brightness, splash) should call displayManager directly instead.
void clockApplySettings(const ClockConfig& cfg);

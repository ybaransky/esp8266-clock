#pragma once

#include <stdint.h>
#include <stddef.h>

// The data half of the display layer: what a rendered frame *is*, with no
// hardware, no driver, and no Arduino dependency. Split out from display.h so
// the pure format renderers (and their host tests) can produce frames without
// pulling in the TM1637 driver.

constexpr size_t kDisplayPanelCount = 3;
constexpr size_t kDisplayFramePanelSize = 8;

// Carries the complete text payload for one render across all three panels.
struct DisplayFrame {
  char panels[kDisplayPanelCount][kDisplayFramePanelSize] = {};  // Null-terminated panel strings.
};

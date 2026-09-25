#pragma once

#include <stdint.h>

// Describes four equal phases whose beep rate doubles at each phase boundary.
struct BeepPattern {
  uint16_t toneHz = 880;  // Constant pitch throughout the alert.
  uint16_t totalDurationSeconds = 40;  // Total length of all four phases.
  uint8_t startingBeatsHz = 2;  // First-phase rate; doubled three times.
};

// Pure envelope calculation; beeps end each slot and the last ends at durationMs.
bool boundaryBeepOn(uint32_t elapsedMs, uint32_t durationMs,
                    uint8_t startingBeatsHz);

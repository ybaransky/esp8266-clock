#pragma once

#include <stdint.h>

#include "display_frame.h"

// Treats three TM1637 4-digit displays as independent 4-character panels.
// Panel strings are rendered left-to-right. ':' or ';' between the second and
// third visible slots lights the center colon, and '.' lights the previous slot.
//
// showFrame() writes every requested panel update directly to the hardware.
class SegmentDisplay {
public:
  void begin(uint8_t brightness = 3);
  void setBrightness(uint8_t level);
  void showFrame(const DisplayFrame& frame);
  void blank();

private:
  uint8_t lastSegments_[3][4] = {};  // Last segment bytes written per panel.
  bool cacheValid_[3] = {};          // True once lastSegments_ is initialized.
};

extern SegmentDisplay segmentDisplay;

#pragma once

#include <stdint.h>

// Treats three TM1637 4-digit displays as independent 4-character panels.
// Panel strings are rendered left-to-right. ':' or ';' between the second and
// third visible slots lights the center colon, and '.' lights the previous slot.
//
// showPanels() writes every requested panel update directly to the hardware.
class SegmentDisplay {
public:
  void begin(uint8_t brightness = 3);
  void setBrightness(uint8_t level);
  void showPanels(const char *r1, const char *r2, const char *r3);
  void blank();

private:
  uint8_t lastSegments_[3][4] = {};
  bool cacheValid_[3] = {};
};

extern SegmentDisplay segmentDisplay;

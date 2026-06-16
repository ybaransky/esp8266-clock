#pragma once

#include <stdint.h>

// Treats three TM1637 4-digit displays as independent 4-character panels.
// Panel strings are rendered left-to-right. ':' or ';' between the second and
// third visible slots lights the center colon, and '.' lights the previous slot.
//
// showPanels() only writes to a display when that panel's text changes, which
// avoids unnecessary TM1637 traffic on shared/boot-sensitive pins.
class SegmentDisplay {
public:
  void begin(uint8_t brightness = 3);
  void setBrightness(uint8_t level);
  void showPanels(const char *r1, const char *r2, const char *r3);
  void blank();

private:
  char last_[3][8] = {};
};

extern SegmentDisplay segmentDisplay;

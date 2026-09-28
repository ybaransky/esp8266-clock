#pragma once

#include <TM1637Display.h>

#include "display_frame.h"

// Treats three TM1637 4-digit displays as independent 4-character panels.
// Panel strings are rendered left-to-right. ':' or ';' between the second and
// third visible slots lights the panel's center colon without consuming a slot.
// This hardware has no decimal points.
//
// showFrame() writes every requested panel update directly to the hardware.
//
// Owns its three TM1637 drivers as members rather than reaching for a
// file-static array, so the object really is the display it claims to be.
class SegmentDisplay {
public:
  SegmentDisplay();

  void begin(uint8_t brightness = 3);
  void setBrightness(uint8_t level);
  void showFrame(const DisplayFrame& frame);
  void blank();

  // Makes the next showFrame() rewrite all four digits on every panel even if
  // its requested segments match the software cache.
  void invalidateCache();

private:
  // TM1637Display has no default constructor, so the array is brace-initialized
  // in the constructor's member-initializer list with each panel's pin pair.
  TM1637Display m_panels[kDisplayPanelCount];  // One driver per physical panel.
  uint8_t m_lastSegments[kDisplayPanelCount][4] = {};  // Last segment bytes written per panel.
  bool m_cacheValid[kDisplayPanelCount] = {};  // True once m_lastSegments is initialized.
};

#include "display_renderer.h"

namespace {

void blankFrame(DisplayFrame& frame) {
  for (size_t panel = 0; panel < kDisplayPanelCount; ++panel) {
    snprintf(frame.panels[panel], kDisplayFramePanelSize, "    ");
  }
}

}  // namespace

DisplayFrame renderDemoDisplayFrame(uint8_t wholeSeconds, uint8_t tenths) {
  DisplayFrame frame;
  blankFrame(frame);
  snprintf(frame.panels[2], kDisplayFramePanelSize,
           "%2u:%u", wholeSeconds, tenths);
  return frame;
}

DisplayFrame renderMessageDisplayFrame(const char* message, bool visible) {
  DisplayFrame frame;
  if (!visible) {
    blankFrame(frame);
    return frame;
  }

  const int length = strlen(message);
  snprintf(frame.panels[0], kDisplayFramePanelSize,
           "%-4.4s", length > 0 ? message : "    ");
  snprintf(frame.panels[1], kDisplayFramePanelSize,
           "%-4.4s", length > 4 ? message + 4 : "    ");
  snprintf(frame.panels[2], kDisplayFramePanelSize,
           "%-4.4s", length > 8 ? message + 8 : "    ");
  return frame;
}

DisplayFrame renderPageDisplayFrame(const char* panel1,
                                    const char* panel2,
                                    const char* panel3,
                                    bool firstPanelVisible) {
  DisplayFrame frame;
  snprintf(frame.panels[0], kDisplayFramePanelSize, "%s",
           firstPanelVisible ? panel1 : "    ");
  snprintf(frame.panels[1], kDisplayFramePanelSize, "%s", panel2);
  snprintf(frame.panels[2], kDisplayFramePanelSize, "%s", panel3);
  return frame;
}

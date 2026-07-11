#include "display_renderer.h"

#include "clock_format.h"

namespace {

TimeFields deltaToFields(long totalSeconds) {
  if (totalSeconds < 0) totalSeconds = 0;
  TimeFields fields = {};
  fields.days = totalSeconds / 86400;
  totalSeconds %= 86400;
  fields.hours = totalSeconds / 3600;
  totalSeconds %= 3600;
  fields.minutes = totalSeconds / 60;
  fields.seconds = totalSeconds % 60;
  return fields;
}

TimeFields rtcToFields(const DateTime& now) {
  TimeFields fields = {};
  fields.year = now.year();
  fields.month = now.month();
  fields.dayOfMonth = now.day();
  fields.dayOfWeek = now.dayOfTheWeek();
  fields.hours = now.hour();
  fields.minutes = now.minute();
  fields.seconds = now.second();
  return fields;
}

void blankFrame(DisplayFrame& frame) {
  for (size_t row = 0; row < kDisplayPanelCount; ++row) {
    snprintf(frame.rows[row], kDisplayFrameRowSize, "    ");
  }
}

}  // namespace

DisplayFrame renderClockDisplayFrame(uint8_t formatIndex,
                                     const DateTime& now,
                                     bool use12Hour,
                                     uint8_t tenths,
                                     bool colonVisible) {
  TimeFields fields = rtcToFields(now);
  if (use12Hour) {
    fields.hours %= 12;
    if (fields.hours == 0) fields.hours = 12;
  }
  fields.tenths = tenths;

  DisplayFrame frame;
  renderClock(formatIndex, fields,
              frame.rows[0], frame.rows[1], frame.rows[2], colonVisible);
  return frame;
}

DisplayFrame renderCountdownDisplayFrame(uint8_t formatIndex,
                                         long totalSeconds,
                                         uint8_t tenths) {
  TimeFields fields = deltaToFields(totalSeconds);
  fields.tenths = tenths;
  DisplayFrame frame;
  renderCountdown(formatIndex, fields,
                  frame.rows[0], frame.rows[1], frame.rows[2]);
  return frame;
}

DisplayFrame renderCountupDisplayFrame(uint8_t formatIndex,
                                       long totalSeconds,
                                       uint8_t tenths) {
  TimeFields fields = deltaToFields(totalSeconds);
  fields.tenths = tenths;
  DisplayFrame frame;
  renderCountup(formatIndex, fields,
                frame.rows[0], frame.rows[1], frame.rows[2]);
  return frame;
}

DisplayFrame renderDemoDisplayFrame(uint8_t wholeSeconds, uint8_t tenths) {
  DisplayFrame frame;
  blankFrame(frame);
  snprintf(frame.rows[2], kDisplayFrameRowSize,
           "%2u.%u", wholeSeconds, tenths);
  return frame;
}

DisplayFrame renderMessageDisplayFrame(const char* message, bool visible) {
  DisplayFrame frame;
  if (!visible) {
    blankFrame(frame);
    return frame;
  }

  const int length = strlen(message);
  snprintf(frame.rows[0], kDisplayFrameRowSize,
           "%-4.4s", length > 0 ? message : "    ");
  snprintf(frame.rows[1], kDisplayFrameRowSize,
           "%-4.4s", length > 4 ? message + 4 : "    ");
  snprintf(frame.rows[2], kDisplayFrameRowSize,
           "%-4.4s", length > 8 ? message + 8 : "    ");
  return frame;
}

DisplayFrame renderPageDisplayFrame(const char* row1,
                                    const char* row2,
                                    const char* row3,
                                    bool firstRowVisible) {
  DisplayFrame frame;
  snprintf(frame.rows[0], kDisplayFrameRowSize, "%s",
           firstRowVisible ? row1 : "    ");
  snprintf(frame.rows[1], kDisplayFrameRowSize, "%s", row2);
  snprintf(frame.rows[2], kDisplayFrameRowSize, "%s", row3);
  return frame;
}

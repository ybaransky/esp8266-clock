#include "clock_source.h"

#include "rtc_ds3231.h"

class RtcClockSource final : public ClockSource {
 public:
  DateTime now() override {
    return rtcGetNow();
  }
};

ClockSource& systemClockSource() {
  static RtcClockSource source;
  return source;
}

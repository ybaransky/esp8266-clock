#include "clock_source.h"

#include "rtc_ds3231.h"

class RtcClockSource final : public ClockSource {
 public:
  // Cached read: display rendering calls this up to 10x/sec for tenths
  // formats, and the RTC's registers only change once a second anyway. See
  // rtcGetNowCached() for how the cache stays fresh without hitting I2C.
  DateTime now() override {
    return rtcGetNowCached();
  }
};

ClockSource& systemClockSource() {
  static RtcClockSource source;
  return source;
}

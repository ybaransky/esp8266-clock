#include "time_service.h"

#include "rtc_ds3231.h"

namespace {

class RtcTimeService final : public TimeService {
 public:
  DateTime nowLive() override {
    return rtcGetNow();
  }

  DateTime nowCached() override {
    return rtcGetNowCached();
  }

  bool consumeSecondTick() override {
    return rtcConsumeSqwPulse();
  }

  bool isLogIntervalDue() override {
    return rtcIsLogIntervalDue();
  }

  bool isHealthy() override {
    return rtcIsHealthy();
  }
};

}  // namespace

TimeService& systemTimeService() {
  static RtcTimeService service;
  return service;
}

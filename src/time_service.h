#pragma once

#include <RTClib.h>

class TimeService {
 public:
  virtual ~TimeService() = default;

  virtual DateTime nowLive() = 0;
  virtual DateTime nowCached() = 0;
  virtual bool consumeSecondTick() = 0;
  virtual bool isLogIntervalDue() = 0;
  virtual bool isHealthy() = 0;
};

TimeService& systemTimeService();

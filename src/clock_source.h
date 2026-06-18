#pragma once

#include <RTClib.h>

class ClockSource {
 public:
  virtual DateTime now() = 0;

 protected:
  ~ClockSource() = default;
};

ClockSource& systemClockSource();

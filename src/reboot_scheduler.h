#pragma once

#include <Arduino.h>

class RebootScheduler {
 public:
  virtual void scheduleReboot(uint32_t delayMs) = 0;

 protected:
  ~RebootScheduler() = default;
};

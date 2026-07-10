#pragma once

#include <Arduino.h>

class DisplayScheduler {
 public:
  void reset(uint32_t nowMs);
  void resetBlink(uint32_t nowMs);
  void invalidateRender();

  bool shouldRender(uint32_t nowMs, uint32_t intervalMs, bool force);
  bool toggleBlinkIfDue(uint32_t nowMs, uint32_t blinkIntervalMs);
  bool toggleColonIfDue(uint32_t nowMs, uint32_t colonIntervalMs);

  bool blinkOn() const { return blinkOn_; }
  bool colonVisible() const { return colonVisible_; }

 private:
  bool blinkOn_ = true;
  uint32_t blinkMs_ = 0;
  bool colonVisible_ = true;
  uint32_t colonMs_ = 0;
  uint32_t lastRenderMs_ = 0;
};

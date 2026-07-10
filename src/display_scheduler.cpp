#include "display_scheduler.h"

void DisplayScheduler::reset(uint32_t nowMs) {
  blinkOn_ = true;
  blinkMs_ = nowMs;
  colonVisible_ = true;
  colonMs_ = 0;
  lastRenderMs_ = 0;
}

void DisplayScheduler::resetBlink(uint32_t nowMs) {
  blinkOn_ = true;
  blinkMs_ = nowMs;
}

void DisplayScheduler::invalidateRender() {
  lastRenderMs_ = 0;
}

bool DisplayScheduler::shouldRender(uint32_t nowMs, uint32_t intervalMs, bool force) {
  if (force) {
    lastRenderMs_ = nowMs;
    return true;
  }

  if (static_cast<long>(nowMs - lastRenderMs_) < static_cast<long>(intervalMs)) {
    return false;
  }

  lastRenderMs_ = nowMs;
  return true;
}

bool DisplayScheduler::toggleBlinkIfDue(uint32_t nowMs, uint32_t blinkIntervalMs) {
  if (static_cast<long>(nowMs - blinkMs_) < static_cast<long>(blinkIntervalMs)) {
    return false;
  }

  blinkMs_ = nowMs;
  blinkOn_ = !blinkOn_;
  return true;
}

bool DisplayScheduler::toggleColonIfDue(uint32_t nowMs, uint32_t colonIntervalMs) {
  if (static_cast<long>(nowMs - colonMs_) < static_cast<long>(colonIntervalMs)) {
    return false;
  }

  colonMs_ = nowMs;
  colonVisible_ = !colonVisible_;
  return true;
}

#pragma once

#include <stdint.h>

#include "beep_pattern.h"

// Generates RTC-aligned alerts and short beeps without storage or blocking delays.
class BeepPlayer {
 public:
  void begin();
  void tick(uint32_t nowMs);
  void setVolume(uint8_t percent);
  void beep(uint16_t toneHz, uint32_t nowMs);
  void updateBoundaryAlert(uint32_t targetLocalSeconds, uint32_t nowLocalSeconds,
                           uint32_t secondStartedAtMs, const BeepPattern& pattern);
  void cancelBoundaryAlert();
  void previewBoundaryAlert(const BeepPattern& pattern, uint32_t nowMs);
  // Stops playback and suppresses the currently armed boundary until it changes.
  void stop();

 private:
  // An absolute monotonic start makes a delayed tick skip directly to its phase.
  struct Window {
    uint32_t startedAtMs = 0;  // millis() at the beginning of the envelope.
    uint32_t durationMs = 0;  // Zero disables the window.
    uint16_t toneHz = 0;  // Buzzer pitch when the envelope is on.
    uint8_t startingBeatsHz = 0;  // Zero selects a continuous short beep.
  };

  enum class Override : uint8_t { kNone, kBeep, kPreview };
  void output(uint16_t toneHz);
  static Window windowFor(const BeepPattern& pattern, uint32_t nowMs);

  Window scheduled_;  // Current scheduled approach window, if armed and near enough.
  Window temporary_;  // Preview or event beep, independent of schedule updates.
  Override override_ = Override::kNone;  // Preview takes priority over event beeps.
  uint32_t targetLocalSeconds_ = 0;  // Identifies the currently armed occurrence.
  bool suppressed_ = false;  // Stop silences this occurrence even on later RTC ticks.
  uint16_t soundingHz_ = 0;  // Last hardware pitch; zero means the pin is silent.
  uint8_t volumePercent_ = 40;  // PWM loudness as a percentage of half duty.
};

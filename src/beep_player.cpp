#include "beep_player.h"

#include <Arduino.h>

#include "hardware.h"

namespace {
constexpr int kPwmRange = 255;
constexpr int kMaxDuty = kPwmRange / 2;
constexpr uint32_t kEventBeepMs = 150;
}  // namespace

void BeepPlayer::begin() {
  pinMode(Hardware::Pins::BUZZER, OUTPUT);
  // GPIO15 must stay LOW at boot; the inverting buffer makes LOW silent.
  digitalWrite(Hardware::Pins::BUZZER, LOW);
  analogWriteRange(kPwmRange);
}

void BeepPlayer::setVolume(uint8_t percent) {
  volumePercent_ = (percent > 100) ? 100 : percent;
  if (soundingHz_ != 0) {
    analogWrite(Hardware::Pins::BUZZER, volumePercent_ * kMaxDuty / 100);
  }
}

void BeepPlayer::output(uint16_t toneHz) {
  if (toneHz == soundingHz_) return;
  soundingHz_ = toneHz;
  if (toneHz == 0) {
    analogWrite(Hardware::Pins::BUZZER, 0);
    digitalWrite(Hardware::Pins::BUZZER, LOW);
  } else {
    analogWriteFreq((toneHz < 100) ? 100 : toneHz);
    analogWrite(Hardware::Pins::BUZZER, volumePercent_ * kMaxDuty / 100);
  }
}

BeepPlayer::Window BeepPlayer::windowFor(const BeepPattern& pattern,
                                        uint32_t nowMs) {
  return {nowMs, static_cast<uint32_t>(pattern.totalDurationSeconds) * 1000U,
          pattern.toneHz, pattern.startingBeatsHz};
}

void BeepPlayer::beep(uint16_t toneHz, uint32_t nowMs) {
  tick(nowMs);  // Expire a finished preview before deciding whether it owns output.
  if (override_ == Override::kPreview) return;
  temporary_ = {nowMs, kEventBeepMs, toneHz, 0};
  override_ = Override::kBeep;
  tick(nowMs);
}

void BeepPlayer::updateBoundaryAlert(uint32_t targetLocalSeconds,
                                      uint32_t nowLocalSeconds,
                                      uint32_t secondStartedAtMs,
                                      const BeepPattern& pattern) {
  if (targetLocalSeconds != targetLocalSeconds_) suppressed_ = false;
  targetLocalSeconds_ = targetLocalSeconds;
  scheduled_.durationMs = 0;
  if (suppressed_ || (targetLocalSeconds <= nowLocalSeconds) ||
      (pattern.toneHz == 0) || (pattern.startingBeatsHz == 0)) return;
  const uint32_t remainingSeconds = targetLocalSeconds - nowLocalSeconds;
  if (remainingSeconds > pattern.totalDurationSeconds) return;
  scheduled_ = windowFor(pattern, secondStartedAtMs);
  scheduled_.startedAtMs -= scheduled_.durationMs - remainingSeconds * 1000U;
}

void BeepPlayer::cancelBoundaryAlert() {
  targetLocalSeconds_ = 0;
  suppressed_ = false;
  scheduled_.durationMs = 0;
  // A schedule cancellation cannot cancel a preview or event beep.
  if (override_ == Override::kNone) output(0);
}

void BeepPlayer::previewBoundaryAlert(const BeepPattern& pattern, uint32_t nowMs) {
  temporary_ = windowFor(pattern, nowMs);
  override_ = Override::kPreview;
  tick(nowMs);
}

void BeepPlayer::stop() {
  override_ = Override::kNone;
  suppressed_ = targetLocalSeconds_ != 0;
  scheduled_.durationMs = 0;
  output(0);
}

void BeepPlayer::tick(uint32_t nowMs) {
  if ((override_ != Override::kNone) &&
      ((nowMs - temporary_.startedAtMs) >= temporary_.durationMs)) {
    override_ = Override::kNone;
  }
  const Window& window = (override_ == Override::kNone) ? scheduled_ : temporary_;
  const uint32_t elapsedMs = nowMs - window.startedAtMs;
  const bool on = (elapsedMs < window.durationMs) &&
      ((window.startingBeatsHz == 0) ||
       boundaryBeepOn(elapsedMs, window.durationMs, window.startingBeatsHz));
  output(on ? window.toneHz : 0);
}

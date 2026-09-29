#include "beep_player.h"

#include <Arduino.h>

#include "hardware.h"

namespace {
constexpr int kPwmRange = 255;           // analogWrite() full-scale value.
constexpr int kMaxDuty = kPwmRange / 2;  // 50% duty, the loudest square wave; 100% volume.
constexpr uint32_t kEventBeepMs = 150;   // Length of a one-shot event beep.
}  // namespace

void BeepPlayer::begin() {
  pinMode(Hardware::Pins::BUZZER, OUTPUT);
  // GPIO15 must stay LOW at boot; the inverting buffer makes LOW silent.
  digitalWrite(Hardware::Pins::BUZZER, LOW);
  analogWriteRange(kPwmRange);
}

void BeepPlayer::setVolume(uint8_t percent) {
  m_volumePercent = (percent > 100) ? 100 : percent;
  if (m_soundingHz != 0) {
    analogWrite(Hardware::Pins::BUZZER, m_volumePercent * kMaxDuty / 100);
  }
}

void BeepPlayer::output(uint16_t toneHz) {
  if (toneHz == m_soundingHz) return;
  m_soundingHz = toneHz;
  if (toneHz == 0) {
    analogWrite(Hardware::Pins::BUZZER, 0);
    digitalWrite(Hardware::Pins::BUZZER, LOW);
  } else {
    analogWriteFreq((toneHz < 100) ? 100 : toneHz);
    analogWrite(Hardware::Pins::BUZZER, m_volumePercent * kMaxDuty / 100);
  }
}

BeepPlayer::Window BeepPlayer::windowFor(const BeepPattern& pattern,
                                        uint32_t nowMs) {
  return {nowMs, static_cast<uint32_t>(pattern.totalDurationSeconds) * 1000U,
          pattern.toneHz, pattern.startingBeatsHz};
}

void BeepPlayer::beep(uint16_t toneHz, uint32_t nowMs) {
  tick(nowMs);  // Expire a finished preview before deciding whether it owns output.
  if (m_override == Override::kPreview) return;
  m_temporary = {nowMs, kEventBeepMs, toneHz, 0};
  m_override = Override::kBeep;
  tick(nowMs);
}

void BeepPlayer::updateBoundaryAlert(uint32_t targetUtcSeconds,
                                      uint32_t nowUtcSeconds,
                                      uint32_t secondStartedAtMs,
                                      const BeepPattern& pattern) {
  if (targetUtcSeconds != m_targetUtcSeconds) m_suppressed = false;
  m_targetUtcSeconds = targetUtcSeconds;
  m_scheduled.durationMs = 0;
  if (m_suppressed || (targetUtcSeconds <= nowUtcSeconds) ||
      (pattern.toneHz == 0) || (pattern.startingBeatsHz == 0)) return;
  const uint32_t remainingSeconds = targetUtcSeconds - nowUtcSeconds;
  if (remainingSeconds > pattern.totalDurationSeconds) return;
  m_scheduled = windowFor(pattern, secondStartedAtMs);
  m_scheduled.startedAtMs -= m_scheduled.durationMs - remainingSeconds * 1000U;
}

void BeepPlayer::cancelBoundaryAlert() {
  m_targetUtcSeconds = 0;
  m_suppressed = false;
  m_scheduled.durationMs = 0;
  // A schedule cancellation cannot cancel a preview or event beep.
  if (m_override == Override::kNone) output(0);
}

void BeepPlayer::previewBoundaryAlert(const BeepPattern& pattern, uint32_t nowMs) {
  m_temporary = windowFor(pattern, nowMs);
  m_override = Override::kPreview;
  tick(nowMs);
}

void BeepPlayer::stop() {
  m_override = Override::kNone;
  m_suppressed = m_targetUtcSeconds != 0;
  m_scheduled.durationMs = 0;
  output(0);
}

void BeepPlayer::tick(uint32_t nowMs) {
  if ((m_override != Override::kNone) &&
      ((nowMs - m_temporary.startedAtMs) >= m_temporary.durationMs)) {
    m_override = Override::kNone;
  }
  const Window& window = (m_override == Override::kNone) ? m_scheduled : m_temporary;
  const uint32_t elapsedMs = nowMs - window.startedAtMs;
  const bool on = (elapsedMs < window.durationMs) &&
      ((window.startingBeatsHz == 0) ||
       boundaryBeepOn(elapsedMs, window.durationMs, window.startingBeatsHz));
  output(on ? window.toneHz : 0);
}

#include "beep_pattern.h"

bool boundaryBeepOn(uint32_t elapsedMs, uint32_t durationMs,
                    uint8_t startingBeatsHz) {
  if ((durationMs < 4) || (elapsedMs >= durationMs) ||
      (startingBeatsHz == 0)) return false;
  const uint32_t phaseMs = durationMs / 4;
  const uint32_t phase = elapsedMs / phaseMs;
  if (phase >= 4) return false;
  const uint32_t rateHz = static_cast<uint32_t>(startingBeatsHz) << phase;
  const uint32_t withinPhaseMs = elapsedMs % phaseMs;
  // Match the floor-rounded slot boundaries below (e.g. 16 Hz starts its
  // second slot at 62 ms, not 63 ms). This avoids extending the preceding beep.
  const uint32_t pulseIndex = ((withinPhaseMs + 1U) * rateHz - 1U) / 1000U;
  const uint32_t pulseStartsAtMs = pulseIndex * 1000U / rateHz;
  const uint32_t nextPulseStartsAtMs = (pulseIndex + 1U) * 1000U / rateHz;
  const uint32_t periodMs = nextPulseStartsAtMs - pulseStartsAtMs;
  const uint32_t halfPeriodMs = periodMs / 2;
  const uint32_t beepMs = (halfPeriodMs < 100) ? halfPeriodMs : 100;
  return (withinPhaseMs - pulseStartsAtMs) >= (periodMs - beepMs);
}

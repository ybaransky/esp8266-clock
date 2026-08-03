#pragma once

#include <stdint.h>

// Tracks a schedule phase plus the boundary timestamp currently reflected on
// the display, so a mode controller's tick() can detect "did anything change
// since the last accepted tick" with one comparison instead of hand-rolling
// phase-only equality (which misses a same-phase, different-target change,
// e.g. a time sync landing on a new week while the phase is unchanged).
// PhaseEnum must declare a kNone value used as the not-yet-ticked sentinel.
template <typename PhaseEnum>
class PhaseTracker {
 public:
  void reset() {
    phase_ = PhaseEnum::kNone;
    targetUnix_ = 0;
  }

  bool hasChanged(PhaseEnum phase, uint32_t targetUnix) const {
    return (phase != phase_) || (targetUnix != targetUnix_);
  }

  PhaseEnum phase() const { return phase_; }

  void accept(PhaseEnum phase, uint32_t targetUnix) {
    phase_ = phase;
    targetUnix_ = targetUnix;
  }

 private:
  PhaseEnum phase_ = PhaseEnum::kNone;  // Last phase applied to the display.
  uint32_t targetUnix_ = 0;  // Boundary timestamp currently on the display.
};

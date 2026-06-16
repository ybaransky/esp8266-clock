#pragma once
#include "config.h"
#include <RTClib.h>

// ── Display mode state machine ────────────────────────────────────────────────
//
// All six modes are peers dispatched from a single tick() switch.
//
//   tick()
//   ├─ kCountdown → tickCountdown()   persistent, config-driven
//   ├─ kCountup   → tickCountup()     persistent, config-driven
//   ├─ kClock     → tickClock()       persistent, config-driven
//   ├─ kSplash    → tickSplash()      transient: 5 s at boot, then restoreMode()
//   ├─ kDemo      → tickDemo()        transient: 5 s countdown + 5 s blink, then restoreMode()
//   └─ kInfo      → tickInfo()        transient: blink for durationMs (-1=indefinite), then restoreMode()
//
// Transient modes save the interrupted mode in previousMode_ and restore it on exit.
// Config persistence uses BaseMode (format.h) and stores only values 0–2.

class ClockModeEngine {
public:
  // Load settings and start the splash if splashMessage is non-empty.
  void begin(const ClockConfig& s);

  // Hot-reload settings. Updates previousMode_ so transient modes return to the new setting.
  void applySettings(const ClockConfig& s);

  // Drive the engine. Call every loop iteration — self-throttles internally.
  void tick(uint32_t nowMs);

  // ── Transient mode controls ───────────────────────────────────────────────
  void triggerDemo();

  // Show a blinking info message for durationMs (-1 = indefinite until clearInfo()).
  void showInfo(const char* message, int32_t durationMs = -1);

  // Dismiss an indefinite info message and restore the previous mode.
  void clearInfo();

private:
  enum class DisplayMode : uint8_t {
    kCountdown = 0, kCountup = 1, kClock = 2,  // values match BaseMode — saveable
    kSplash    = 3, kDemo    = 4, kInfo  = 5,  // transient — never saved
  };

  // ── Mode state ────────────────────────────────────────────────────────────
  DisplayMode currentMode_    = DisplayMode::kCountdown;
  DisplayMode previousMode_   = DisplayMode::kCountdown;
  uint32_t    modeStartMs_    = 0;
  int32_t     infoDurationMs_ = -1;
  char        transientMsg_[64] = {};

  // ── Blink state (shared by all transient modes) ───────────────────────────
  bool        blinkOn_  = true;
  uint32_t    blinkMs_  = 0;

  // ── Base-mode state ───────────────────────────────────────────────────────
  ClockConfig settings_       = defaultClockConfig();
  DateTime      countupOrigin_;
  uint32_t      lastBaseTickMs_ = 0;
  bool          colonVisible_   = true;
  uint32_t      colonMs_        = 0;

  // ── Transition helpers ────────────────────────────────────────────────────
  void enterTransientMode(DisplayMode m, const char* msg, int32_t durationMs = -1);
  void restoreMode();

  // ── Blink helpers ─────────────────────────────────────────────────────────
  void updateBlink(uint32_t nowMs);
  void showBlinkingMessage(const char* msg);

  // ── Tick handlers ─────────────────────────────────────────────────────────
  void tickCountdown(uint32_t nowMs);
  void tickCountup(uint32_t nowMs);
  void tickClock(uint32_t nowMs);
  void tickSplash(uint32_t nowMs);
  void tickDemo(uint32_t nowMs);
  void tickInfo(uint32_t nowMs);

  // ── Base-mode helpers ─────────────────────────────────────────────────────
  uint32_t baseRefreshInterval() const;
  bool     baseElapsed(uint32_t nowMs);

  static DisplayMode baseModeToDisplay(BaseMode m);
};

extern ClockModeEngine clockModeEngine;

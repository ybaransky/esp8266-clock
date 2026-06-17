#pragma once

#include <Arduino.h>
#include <RTClib.h>

#include "config.h"

enum class BaseDisplayMode : uint8_t {
  kClock,
  kCountdown,
  kCountup,
};

enum class OverlayDisplayMode : uint8_t {
  kNone,
  kSplash,
  kDemo,
  kInfo,
};

class DisplayManager {
 public:
  void begin(const ClockConfig& config);
  void applySettings(const ClockConfig& config);
  void tick(uint32_t nowMs);

  void showSplash(const char* message);
  void showDemo();
  void showInfo(const char* message, int32_t durationMs = FOREVER);
  void clearInfo();

  const char* baseModeName() const;
  const char* overlayModeName() const;

 private:
  BaseDisplayMode baseModeFor(BaseMode mode) const;
  const char* baseModeName(BaseDisplayMode mode) const;
  const char* overlayModeName(OverlayDisplayMode mode) const;

  void setBaseMode(BaseDisplayMode next);
  void startOverlay(OverlayDisplayMode overlay, const char* message,
                    int32_t durationMs, uint32_t nowMs);
  void finishOverlay(uint32_t nowMs);

  void updateCountupOrigin(const ClockConfig& config);
  uint32_t baseRefreshInterval() const;
  bool baseElapsed(uint32_t nowMs, bool force = false);

  void tickBase(uint32_t nowMs, bool force = false);
  void tickClock(uint32_t nowMs, bool force = false);
  void tickCountdown(uint32_t nowMs, bool force = false);
  void tickCountup(uint32_t nowMs, bool force = false);
  bool tickActiveOverlay(uint32_t nowMs);
  bool tickSplash(uint32_t nowMs);
  bool tickInfo(uint32_t nowMs);
  bool tickDemo(uint32_t nowMs);

  ClockConfig settings_ = defaultClockConfig();  // Current display configuration.
  BaseDisplayMode baseMode_ = BaseDisplayMode::kClock;  // Persistent mode to render.
  OverlayDisplayMode overlayMode_ = OverlayDisplayMode::kNone;  // Active transient overlay.

  DateTime countupOrigin_;  // Start time used by count-up mode.
  uint32_t modeStartMs_ = 0;  // millis() when the active overlay started.
  int32_t infoDurationMs_ = FOREVER;  // Optional info overlay lifetime; FOREVER means indefinite.
  char overlayMessage_[64] = {};  // Message text used by splash/info overlays.

  bool blinkOn_ = true;  // Current blink phase for blinking overlays.
  uint32_t blinkMs_ = 0;  // millis() timestamp for the last blink phase change.
  uint32_t lastBaseTickMs_ = 0;  // millis() timestamp for the last base render.
  uint32_t baseHandoffDueMs_ = 0;  // When to render base after overlay; 0 means not pending.
  bool colonVisible_ = true;  // Current colon phase for clock formats with blinking colon.
  uint32_t colonMs_ = 0;  // millis() timestamp for the last colon phase change.
};

extern DisplayManager displayManager;

#pragma once

#include <Arduino.h>
#include <RTClib.h>

#include "scheduled_mode.h"
#include "rtc_ds3231.h"

struct ClockConfig;
enum Mode : uint8_t;
enum class View : uint8_t;
class DisplayManager;
class RtcService;
class SoundPlayer;

// Coordinates application actions shared by the main loop and web APIs.
class ClockController {
 public:
  ClockController(DisplayManager& displayManager, RtcService& rtc,
                  SoundPlayer& soundPlayer)
      : displayManager_(displayManager), rtc_(rtc), sound_(soundPlayer) {}

  void applyConfig(const ClockConfig& config);

  void onSecondBoundary(const RtcTick& tick);
  void setTime(const DateTime& now);
  void setBrightness(uint8_t brightness);
  void showDemo();
  void showInfo(const char* message, int32_t durationMs);
  void showSplash(const char* message);

  // Sound previews and catalog queries are NOT routed through here. They have
  // no application logic to add, and tunnelling them turned this class into a
  // service locator for the web handlers. ConfigApi holds SoundPlayer directly.
  Mode activeMode() const { return mode_; }
  View activeView() const;
  bool demoActive() const;

 private:
  ViewState initialView(const ClockConfig& config, const DateTime& now);
  void updateCountdown(const DateTime& now, bool announce);
  void refreshSchedule(const DateTime& now, uint32_t secondStartedAtMs);

  DisplayManager& displayManager_;  // Applies view, overlay, and brightness actions.
  RtcService& rtc_;  // Reads and updates the hardware clock.
  SoundPlayer& sound_;  // Plays catalog sounds for previews and cues.
  ScheduledModeController scheduledMode_;  // Shared Friday/Trading boundary tracking.
  Mode mode_ = kModeClock;  // Persisted selection applied to the application.
  DateTime countdownEnd_;  // Application deadline for ordinary Countdown mode.
  bool countdownComplete_ = false;  // One-shot completion state, independent of rendering.
  char finalSound_[kSoundNameLength] = "";  // Master-resolved completion cue.
};

#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <RTClib.h>

#include "scheduled_mode.h"
#include "rtc_ds3231.h"

struct ClockConfig;
enum Mode : uint8_t;
enum class View : uint8_t;
enum class SoundKind : uint8_t;
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

  // Plays a catalog sound by name, ignoring the master sound switch: every
  // caller is an explicit user action (a preview button), where refusing to
  // make a noise would just look broken. Returns false for an unknown name.
  bool playSound(const char* name);
  void previewBoundaryAlert(uint16_t frequencyHz,
                            uint16_t totalDurationSeconds,
                            uint8_t startingBeatsHz);
  void stopSound();

  // Fills `array` with every catalog sound name of `kind`. False when no
  // catalog is readable, which the page shows as "no sounds installed" rather
  // than an empty dropdown that looks like a bug.
  bool soundNamesAsJson(JsonArray array, SoundKind kind);

  // Playing time of a catalog sound in milliseconds; 0 when it is unknown.
  uint32_t soundDurationMs(const char* name);
  static uint32_t boundaryAlertDurationMs(uint16_t totalDurationSeconds) {
    return static_cast<uint32_t>(totalDurationSeconds) * 1000UL;
  }
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

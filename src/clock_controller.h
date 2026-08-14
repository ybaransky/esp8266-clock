#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <RTClib.h>

#include "friday_mode.h"
#include "mode_outputs.h"
#include "trading_mode.h"

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
      : displayManager_(displayManager), rtc_(rtc), sound_(soundPlayer),
        outputs_{displayManager, soundPlayer} {}

  void applyConfig(const ClockConfig& config);

  // Per-loop work that is not tied to an RTC second: currently just the
  // countdown-completion announcement, which is detected on the display's own
  // render cadence and would otherwise wait up to a second to be noticed.
  void tick();

  void onSecondBoundary(const DateTime& now);
  void setTime(const DateTime& now);
  void setBrightness(uint8_t brightness);
  void showDemo();
  void showInfo(const char* message, int32_t durationMs);
  void showSplash(const char* message);

  // Plays a catalog sound by name, ignoring the master sound switch: every
  // caller is an explicit user action (a preview button), where refusing to
  // make a noise would just look broken. Returns false for an unknown name.
  bool playSound(const char* name);
  void stopSound();

  // Fills `array` with every catalog sound name. False when no catalog is
  // readable, which the page shows as "no sounds installed" rather than an
  // empty dropdown that looks like a bug.
  bool soundNamesAsJson(JsonArray array);
  Mode activeMode() const;
  View activeView() const;
  bool demoActive() const;

 private:
  DisplayManager& displayManager_;  // Applies view, overlay, and brightness actions.
  RtcService& rtc_;  // Reads and updates the hardware clock.
  SoundPlayer& sound_;  // Plays catalog sounds for previews and cues.
  ModeOutputs outputs_;  // The pair handed to each scheduled mode on tick.
  FridayModeController fridayMode_;  // Owns Friday schedule state and cache.
  TradingModeController tradingMode_;  // Owns Trading schedule state.
  // Cue for countdown completion, resolved against the master sound switch by
  // applyConfig(). Countdown completion is the one announced event with no
  // schedule controller to hold its settings snapshot, so it lives here.
  char finalSound_[kSoundNameLength] = "";
};

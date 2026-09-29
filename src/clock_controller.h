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
class BeepPlayer;

// Coordinates application actions shared by the main loop and web APIs.
class ClockController {
 public:
  ClockController(DisplayManager& displayManager, RtcService& rtc,
                  BeepPlayer& beepPlayer)
      : m_displayManager(displayManager), m_rtc(rtc), m_sound(beepPlayer) {}

  void applyConfig(const ClockConfig& config);

  void onSecondBoundary(const RtcTick& tick);
  // Writes the RTC. `reason` names the source (browser, NTP) in the log.
  void setTime(uint32_t utc, const char* reason);
  void setBrightness(uint8_t brightness);
  void showDemo();
  void showInfo(const char* message, int32_t durationMs);
  void showSplash(const char* message);

  // Beep previews are NOT routed through here. They have
  // no application logic to add, and tunnelling them turned this class into a
  // service locator for the web handlers. ConfigApi holds BeepPlayer directly.
  Mode activeMode() const { return m_mode; }
  View activeView() const;
  bool demoActive() const;

 private:
  uint32_t localTextToUtc(const char* text) const;
  ViewState initialView(const ClockConfig& config, const DateTime& now,
                        uint32_t nowUtc);
  void updateCountdown(uint32_t nowUtc, bool announce);
  void refreshSchedule(const DateTime& now, uint32_t nowUtc,
                       uint32_t secondStartedAtMs);

  DisplayManager& m_displayManager;  // Applies view, overlay, and brightness actions.
  RtcService& m_rtc;  // Reads and updates the hardware clock.
  BeepPlayer& m_sound;  // Generates approach alerts and event beeps.
  ScheduledModeController m_scheduledMode;  // Shared Friday/Trading boundary tracking.
  Mode m_mode = kModeClock;  // Persisted selection applied to the application.
  uint32_t m_countdownEndUtc = 0;  // Application deadline for ordinary Countdown mode.
  bool m_countdownComplete = false;  // One-shot completion state, independent of rendering.
  bool m_finalBeep = false;  // Master-resolved countdown-completion beep.
};

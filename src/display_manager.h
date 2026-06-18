#pragma once

#include <Arduino.h>
#include <RTClib.h>

#include "config.h"

enum class DisplayBehavior : uint8_t {
  kClock,
  kCountdown,
  kCountup,
  kMessage,
  kPagedMessage,
};

static constexpr uint8_t kDisplayRowsPerPage = 3;
static constexpr uint8_t kDisplayRowChars = 4;
static constexpr uint8_t kMaxDisplayPages = 8;
static constexpr uint16_t kDefaultPageDurationMs = 2000;

struct DisplayPage {
  char rows[kDisplayRowsPerPage][kDisplayRowChars + 1] = {};  // Three 4-character panel rows.
};

struct PagedDisplayPayload {
  DisplayPage pages[kMaxDisplayPages];       // Prebuilt pages rendered exactly as provided.
  uint8_t pageCount = 0;                      // Number of valid pages in pages[].
  uint8_t currentPage = 0;                    // Page currently visible.
  uint16_t pageDurationMs = kDefaultPageDurationMs;  // Time each page remains visible.
  uint32_t pageStartedAtMs = 0;               // millis() timestamp when currentPage began.
  bool repeat = false;                        // True when pages loop until externally cleared.
};

struct DisplayPayload {
  DateTime endTime = DateTime(2000, 1, 1, 0, 0, 0);     // Countdown end time.
  DateTime startTime = DateTime(2000, 1, 1, 0, 0, 0);   // Count-up origin time.
  uint8_t formatIndex = 0;                              // Formatter index for clock/count states.
  char message[64] = {};                                // Text rendered by message states.
  PagedDisplayPayload paged;                             // Structured pages for paged message states.
};

struct DisplayState {
  DisplayBehavior behavior = DisplayBehavior::kClock;  // Renderer used for this state.
  bool blink = false;                                  // True when message output alternates blank/on.
  DisplayPayload payload;                              // Data consumed by the selected renderer.
};

struct DisplayTransition {
  bool hasExpiration = false;  // True when the current state should expire.
  uint32_t expiresAtMs = 0;    // millis() deadline for temporary state expiration.
};

class DisplayManager {
 public:
  void begin(const ClockConfig& config);
  void applySettings(const ClockConfig& config);
  void setBrightness(uint8_t brightness);
  void tick(uint32_t nowMs);

  void showSplash(const char* message);
  void showDemo();
  void showInfo(const char* message, int32_t durationMs = FOREVER);
  void showPages(const DisplayPage* pages, uint8_t pageCount,
                 uint16_t pageDurationMs = kDefaultPageDurationMs,
                 bool repeat = false);
  void clearInfo();

  const char* currentStateName() const;

 private:
  DisplayState stateForConfiguredMode(PersistentMode mode) const;
  const char* behaviorName(DisplayBehavior behavior) const;
  void logStateTransition(const DisplayState& from, const DisplayState& to,
                          const char* reason) const;

  void installState(const DisplayState& state, const DisplayTransition& transition,
                    uint32_t nowMs, bool rememberPrevious);
  void installDefaultState(uint32_t nowMs, bool forceRender = true);
  void finishTemporaryState(uint32_t nowMs);
  void restorePreviousState(uint32_t nowMs);
  void startDemoMessageState(uint32_t nowMs);

  void updateCountupOrigin(const ClockConfig& config);
  uint32_t refreshInterval() const;
  bool renderElapsed(uint32_t nowMs, bool force = false);
  bool transitionExpired(uint32_t nowMs) const;

  void renderCurrentState(uint32_t nowMs, bool force = false);
  void renderClock(uint32_t nowMs, bool force);
  void renderCountdown(uint32_t nowMs, bool force);
  void renderCountup(uint32_t nowMs, bool force);
  void renderMessage(uint32_t nowMs, bool force);
  void renderPagedMessage(uint32_t nowMs, bool force);

  ClockConfig settings_ = defaultClockConfig();  // Persisted display settings currently applied.
  DisplayState defaultState_;                    // State restored after temporary states.
  DisplayState currentState_;                    // State rendered on each tick.
  DisplayTransition currentTransition_;          // Expiration metadata for the current state.
  DisplayState previousState_;                   // State restored after temporary splash/info/demo states.
  bool hasPreviousState_ = false;                // True while a temporary state can return to previousState_.
  bool demoCountdownActive_ = false;             // True during demo's first countdown phase.

  DateTime countupOrigin_;       // Captured start time for count-up states using "now".
  bool blinkOn_ = true;          // Current visible/blank phase for blinking message states.
  uint32_t blinkMs_ = 0;         // millis() timestamp of the last blink phase change.
  uint32_t lastRenderMs_ = 0;    // millis() timestamp of the last rendered frame.
  bool colonVisible_ = true;     // Current colon phase for clock formats with blinking colon.
  uint32_t colonMs_ = 0;         // millis() timestamp of the last colon phase change.
};

extern DisplayManager displayManager;

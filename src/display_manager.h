#pragma once

#include <Arduino.h>
#include <RTClib.h>

#include "config.h"
#include "defaults.h"
#include "display_scheduler.h"

// What content is currently the "normal" thing to render - i.e. what the
// active Mode resolves to right now. For Countdown/Countup/Clock modes this
// never changes on its own. Friday mode is the one case where it varies
// over time: FridayModeController recomputes it as its phase changes and
// pushes the update via DisplayManager::setView().
enum class View : uint8_t {
  kClock,
  kCountdown,
  kCountup,
};

struct ViewState {
  View view = View::kClock;
  DateTime anchor;          // Countdown: end time. Countup: start time. Clock: unused.
  uint8_t formatIndex = 0;  // Index into the view's format table.
};

// Short lowercase name for logging (e.g. "clock", "countdown").
const char* viewName(View view);

// A temporary layer shown on top of the current View. While one is active,
// the physical display shows it instead of the View underneath - but the
// View keeps updating live (e.g. Friday mode's phase can still change), so
// whatever View is current the instant the overlay clears is what appears
// next. There is no separate snapshot of "the view before the overlay" to
// keep in sync or let go stale.
enum class Overlay : uint8_t {
  kDemo,  // Countdown driven by a transition deadline rather than a DateTime end time.
  kMessage,
  kPagedMessage,
};

// Short lowercase name for logging (e.g. "message", "pages").
const char* overlayName(Overlay overlay);

static constexpr uint8_t kDisplayRowsPerPage = 3;
static constexpr uint8_t kDisplayRowChars = 4;
static constexpr uint8_t kMaxDisplayPages = 8;
static constexpr uint16_t kDefaultPageDurationMs = 2000;

struct DisplayPage {
  char rows[kDisplayRowsPerPage][kDisplayRowChars + 1];  // Three 4-character panel rows.
};

struct PagedDisplayPayload {
  DisplayPage pages[kMaxDisplayPages] = {};
  uint8_t  pageCount = 0;
  uint8_t  currentPage = 0;
  uint16_t pageDurationMs = kDefaultPageDurationMs;
  uint32_t pageStartedAtMs = 0;
  bool     repeat = false;
};

struct OverlayTransition {
  bool hasExpiration = false;  // True when the overlay should clear on its own.
  uint32_t expiresAtMs = 0;    // millis() deadline for automatic clearing.
};

struct OverlayState {
  Overlay overlay = Overlay::kMessage;
  bool blink = false;             // True when output alternates blank/on.
  bool chainFinalMessage = false; // On expiry, show the final message instead
                                  // of the base view (demo's second phase).
  char message[64] = "";          // kMessage text; unused otherwise.
  PagedDisplayPayload paged;      // kPagedMessage pages; unused otherwise.
  OverlayTransition transition;
};

class DisplayManager {
 public:
  void begin(const ClockConfig& config);
  void applySettings(const ClockConfig& config);
  void setBrightness(uint8_t brightness);
  void tick(uint32_t nowMs);

  // Called from loop() on each accepted SQW pulse (the real RTC second
  // boundary). Invalidates the render throttle so the next tick() redraws
  // immediately - the seconds digit flips within one loop pass of the
  // physical edge, and the 100ms tenths cadence re-phases to it each second.
  void notifySecondBoundary();

  void showSplash(const char* message);
  void showDemo();
  void showInfo(const char* message, int32_t durationMs = FOREVER);
  void showPages(const DisplayPage* pages, uint8_t pageCount,
                 uint16_t pageDurationMs = kDefaultPageDurationMs,
                 bool repeat = false);
  void clearOverlay();

  // Replaces the base view (what's shown whenever no overlay is active).
  // If no overlay is active, also updates the current display immediately.
  // If one is active, this view simply becomes visible once the overlay
  // clears - there's no separate snapshot that needs to be kept in sync.
  void setView(const ViewState& view);

  // Name of whatever is actually on the segments right now: the overlay's
  // name if one is active, otherwise the base view's name. For logging.
  const char* renderedName() const;

  // The persistent mode from config (kModeCountdown/Countup/Clock/Friday).
  Mode activeMode() const { return settings_.activeMode; }

  // The View backing the base view (i.e. baseView_, not whatever overlay -
  // if any - is currently covering it, so a splash/info/demo overlay never
  // counts as a view change). Fixed by activeMode() for the three
  // non-Friday modes; Friday mode is the one case where it varies over
  // time, as FridayModeController recomputes it and calls setView().
  View activeView() const { return baseView_.view; }

 private:
  ViewState viewForMode(Mode mode) const;
  void logTransition(const char* from, const char* to, const char* reason) const;

  void installOverlay(const OverlayState& state, uint32_t nowMs);
  void installView(uint32_t nowMs, bool forceRender = true);
  void finishOverlay(uint32_t nowMs);
  void clearOverlayAndRenderView(uint32_t nowMs);
  void startDemoMessageOverlay(uint32_t nowMs);

  void updateCountupOrigin(const ClockConfig& config);
  uint32_t refreshInterval() const;
  bool renderElapsed(uint32_t nowMs, bool force = false);
  bool overlayExpired(uint32_t nowMs) const;

  void render(uint32_t nowMs, bool force = false);
  void renderClock(uint32_t nowMs, bool force);
  void renderCountdown(uint32_t nowMs, bool force);
  void renderCountup(uint32_t nowMs, bool force);
  void renderDemo(uint32_t nowMs, bool force);
  void renderMessage(uint32_t nowMs, bool force);
  void renderPagedMessage(uint32_t nowMs, bool force);

  ClockConfig settings_ = defaultClockConfig();  // Persisted display settings currently applied.
  ViewState baseView_;                           // What to show when no overlay is active.
  bool hasOverlay_ = false;                      // True while overlay_ is what's actually on screen.
  OverlayState overlay_;                         // Only meaningful while hasOverlay_ is true.

  DateTime countupOrigin_;       // Captured start time for count-up views using "now".
  DisplayScheduler scheduler_;   // Blink/colon cadence + render throttling.
};

extern DisplayManager displayManager;

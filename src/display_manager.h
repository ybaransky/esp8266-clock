#pragma once

#include <Arduino.h>
#include <RTClib.h>

#include "config.h"
#include "defaults.h"

// Tracks render deadlines and independently advances message and colon blink phases.
class DisplayScheduler {
 public:
  void reset(uint32_t nowMs);
  void resetBlink(uint32_t nowMs);
  void invalidateRender();
  bool shouldRender(uint32_t nowMs, uint32_t intervalMs, bool force);
  bool toggleBlinkIfDue(uint32_t nowMs, uint32_t intervalMs);
  bool toggleColonIfDue(uint32_t nowMs, uint32_t intervalMs);
  bool blinkOn() const { return blinkOn_; }
  bool colonVisible() const { return colonVisible_; }

 private:
  bool blinkOn_ = true;  // Current visibility phase for blinking overlays.
  uint32_t blinkMs_ = 0;  // Last overlay-blink transition time.
  bool colonVisible_ = true;  // Current visibility phase for animated colons.
  uint32_t colonMs_ = 0;  // Last colon transition time.
  uint32_t lastRenderMs_ = 0;  // Last accepted render time.
};

class SegmentDisplay;
class RtcService;
struct DisplayFrame;

// What content is currently the "normal" thing to render - i.e. what the
// active Mode resolves to right now. For Countdown/Countup/Clock modes this
// never changes on its own. Friday and Trading modes vary over time: their
// schedule controllers recompute the view as each phase changes and push the
// update via DisplayManager::setView().
enum class View : uint8_t {
  kClock,
  kCountdown,
  kCountup,
};

// Captures the base content and format-specific anchor needed for rendering a view.
struct ViewState {
  View view = View::kClock;  // Kind of base content to render.
  DateTime anchor;          // Countdown: end time. Countup: start time. Clock: unused.
  uint8_t formatIndex = 0;  // Index into the view's format table.
  uint8_t longFormatIndex = kSameFormat;  // Counting format while the duration
                                          // is >= 24h; kSameFormat disables.
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
  kNone,
  kSplash,
  kBlinkingMessage,
  kCountdownComplete,
  kDemoCountdown,
  kDemoFinalMessage,
  kPagedMessage,
};

// Short lowercase name for logging (e.g. "message", "pages").
const char* overlayName(Overlay overlay);

static constexpr uint8_t kDisplayPanelsPerPage = 3;
static constexpr uint8_t kDisplayPanelChars = 4;
static constexpr uint8_t kMaxDisplayPages = 8;
static constexpr uint16_t kDefaultPageDurationMs = 2000;

// Holds the three panel strings that make up one page of an overlay.
struct DisplayPage {
  char panels[kDisplayPanelsPerPage][kDisplayPanelChars + 1];  // Null-terminated panel text.
};

// Owns copied pages and timing state while a paged-message overlay is active.
struct PagedDisplayPayload {
  DisplayPage pages[kMaxDisplayPages] = {};  // Pages copied from the overlay request.
  uint8_t pageCount = 0;  // Number of valid entries in pages.
  uint8_t currentPage = 0;  // Index of the page currently rendered.
  uint16_t pageDurationMs = kDefaultPageDurationMs;  // Display duration per page.
  uint32_t pageStartedAtMs = 0;  // millis() when the current page began.
  bool repeat = false;  // True to wrap after the final page.
};

// Describes when an overlay should transition away automatically.
struct OverlayTransition {
  bool hasExpiration = false;  // True when the overlay should clear on its own.
  uint32_t expiresAtMs = 0;    // millis() deadline for automatic clearing.
};

// Owns the payload and lifecycle flags for the currently installed overlay.
struct OverlayState {
  Overlay overlay = Overlay::kNone;  // Active overlay type.
  char message[64] = "";          // Text for message overlays; unused otherwise.
  PagedDisplayPayload paged;      // kPagedMessage pages; unused otherwise.
  OverlayTransition transition;  // Automatic expiration policy.
};

// Snapshot of only the configuration fields display rendering consumes.
// Copied from ClockConfig by applySettings() to avoid holding the full config.
struct DisplaySettings {
  Mode activeMode = kModeClock;  // Persisted mode driving the base view.
  DisplayConfig display{};  // Clock format, brightness, and 12-hour flag.
  CountdownConfig countdown{};  // Countdown target and format.
  uint8_t countupFormat = 0;  // Count-up counting-format index.
  uint8_t fridayClockFmt = 0;  // Initial clock format for Friday mode.
  TradingConfig trading{};  // Trading-mode placeholder countdown formats.
  char finalMessage[64] = "";  // Shown on countdown completion and demo end.

  static DisplaySettings fromConfig(const ClockConfig& config);
};

// Resolves configured modes into base views and renders temporary overlays above them.
class DisplayManager {
 public:
  DisplayManager(SegmentDisplay& display, RtcService& rtc)
      : display_(display), rtc_(rtc) {}
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
  void showInfo(const char* message, int32_t durationMs = kForever);
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
  bool demoActive() const;

  // The persistent mode from config.
  Mode activeMode() const { return settings_.activeMode; }

  // The View backing the base view (i.e. baseView_, not whatever overlay -
  // if any - is currently covering it, so a splash/info/demo overlay never
  // counts as a view change). Fixed by activeMode() for countdown, countup,
  // and clock modes; Friday and Trading schedule controllers vary it over
  // time by calling setView().
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
  uint8_t activeCountingFormatIndex() const;
  uint32_t refreshInterval() const;
  bool renderElapsed(uint32_t nowMs, bool force = false);
  bool overlayExpired(uint32_t nowMs) const;
  bool overlayBlinks() const;
  bool hasOverlay() const { return overlay_.overlay != Overlay::kNone; }

  void render(uint32_t nowMs, bool force = false);
  bool buildClockFrame(uint32_t nowMs, bool force, DisplayFrame& frame);
  bool buildCountdownFrame(uint32_t nowMs, bool force, DisplayFrame& frame);
  bool buildCountupFrame(uint32_t nowMs, bool force, DisplayFrame& frame);
  bool buildDemoFrame(uint32_t nowMs, bool force, DisplayFrame& frame);
  bool buildMessageFrame(uint32_t nowMs, bool force, DisplayFrame& frame);
  bool buildPagedMessageFrame(uint32_t nowMs, bool force,
                              DisplayFrame& frame);

  DisplaySettings settings_ =
      DisplaySettings::fromConfig(defaultClockConfig());  // Applied settings snapshot.
  ViewState baseView_;                           // What to show when no overlay is active.
  OverlayState overlay_;                         // kNone unless an overlay is active.

  DateTime countupOrigin_;       // Captured start time for count-up views using "now".
  DisplayScheduler scheduler_;   // Blink/colon cadence + render throttling.
  SegmentDisplay& display_;  // Hardware target for completed frames.
  RtcService& rtc_;  // Cached time and SQW phase source for renderers.
};

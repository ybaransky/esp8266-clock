#include "button.h"

#include "hardware.h"
#include "log.h"
#include <Arduino.h>
#include <OneButton.h>

// Forward declarations for OneButton callbacks.
static void onBtnClick();
static void onBtnDoubleClick();
static void onBtnLongPressStart();

// Debounces the hardware button and queues semantic button events for the application loop.
class ButtonController {
public:
  void begin() {
    pinMode(Hardware::Pins::BUTTON, INPUT_PULLUP);
    if (digitalRead(Hardware::Pins::BUTTON) == LOW) {
      LOG_PRINTLN("WARNING: D3/GPIO0 is LOW at startup (button may be pressed). Avoid holding this button during boot.");
    }
    m_startupRecheckAtMs = millis() + kStartupRecheckDelayMs;
    m_startupRecheckDone = false;

    // D3/GPIO0 uses pull-up logic; pressed state is LOW.
    m_driver.attachClick(onBtnClick);
    m_driver.attachDoubleClick(onBtnDoubleClick);
    m_driver.attachLongPressStart(onBtnLongPressStart);
  }

  void tick() {
    if (!m_startupRecheckDone && (static_cast<long>(millis() - m_startupRecheckAtMs) >= 0)) {
      if (digitalRead(Hardware::Pins::BUTTON) == LOW) {
        LOG_PRINTLN("WARNING: D3/GPIO0 still LOW 500ms after startup. Check wiring or release button during boot.");
      }
      m_startupRecheckDone = true;
    }
    m_driver.tick();
  }

  bool hasEvent() const {
    return m_eventHead != m_eventTail;
  }

  ButtonEvent nextEvent() {
    if (m_eventHead == m_eventTail) return ButtonEvent::kNone;
    const ButtonEvent event = m_eventQueue[m_eventHead];
    m_eventHead = (m_eventHead + 1) % kEventQueueCapacity;
    return event;
  }

  void handleAction(const char *message, ButtonEvent event = ButtonEvent::kNone) {
    LOG_PRINTF("%s", message);
    if (event != ButtonEvent::kNone) {
      enqueueEvent(event);
    }
  }

private:
  static constexpr int kEventQueueCapacity = 8;  // Maximum queued button events.
  static constexpr unsigned long kStartupRecheckDelayMs = 500;  // Boot-pin recheck delay.

  void enqueueEvent(ButtonEvent event) {
    const int nextTail = (m_eventTail + 1) % kEventQueueCapacity;
    if (nextTail == m_eventHead) return;
    m_eventQueue[m_eventTail] = event;
    m_eventTail = nextTail;
  }

  OneButton m_driver = OneButton(Hardware::Pins::BUTTON, true, true);  // Debounced button driver.
  volatile ButtonEvent m_eventQueue[kEventQueueCapacity] = {};     // Pending button events.
  volatile int m_eventHead = 0;                                     // Queue read index.
  volatile int m_eventTail = 0;                                     // Queue write index.
  bool m_startupRecheckDone = false;                                // True after boot-pin recheck.
  unsigned long m_startupRecheckAtMs = 0;                           // millis() deadline for recheck.
};

static ButtonController controller;  // The one physical button behind the free functions below.

static void onBtnClick() {
  controller.handleAction("Single press", ButtonEvent::kShowSsid);
}

static void onBtnDoubleClick() {
  controller.handleAction("Double click", ButtonEvent::kShowIpAddress);
}

static void onBtnLongPressStart() {
  controller.handleAction("Long press", ButtonEvent::kShowRtcStatus);
}

void buttonBegin()                    { controller.begin(); }
void buttonTick()                     { controller.tick(); }
bool buttonHasEvent()                 { return controller.hasEvent(); }
ButtonEvent buttonNextEvent()         { return controller.nextEvent(); }

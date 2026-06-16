#include "button.h"

#include "hardware.h"
#include <Arduino.h>
#include <OneButton.h>

// Forward declarations for OneButton callbacks.
static void onBtnClick();
static void onBtnDoubleClick();
static void onBtnLongPressStart();

class ButtonController {
public:
  void begin() {
    pinMode(Hardware::Pins::BUTTON, INPUT_PULLUP);
    if (digitalRead(Hardware::Pins::BUTTON) == LOW) {
      Serial.println("[BTN] WARNING: D3/GPIO0 is LOW at startup (button may be pressed). Avoid holding this button during boot.");
    }
    startupRecheckAtMs_ = millis() + STARTUP_RECHECK_DELAY_MS;
    startupRecheckDone_ = false;

    // D3/GPIO0 uses pull-up logic; pressed state is LOW.
    btn_.attachClick(onBtnClick);
    btn_.attachDoubleClick(onBtnDoubleClick);
    btn_.attachLongPressStart(onBtnLongPressStart);
  }

  void tick() {
    if (!startupRecheckDone_ && static_cast<long>(millis() - startupRecheckAtMs_) >= 0) {
      if (digitalRead(Hardware::Pins::BUTTON) == LOW) {
        Serial.println("[BTN] WARNING: D3/GPIO0 still LOW 500ms after startup. Check wiring or release button during boot.");
      }
      startupRecheckDone_ = true;
    }
    btn_.tick();
  }

  bool hasEvent() const {
    return eventHead_ != eventTail_;
  }

  ButtonEvent nextEvent() {
    if (eventHead_ == eventTail_) return ButtonEvent::NONE;
    const ButtonEvent event = eventQueue_[eventHead_];
    eventHead_ = (eventHead_ + 1) % EVENT_QUEUE_CAPACITY;
    return event;
  }

  void handleAction(const char *message, ButtonEvent event = ButtonEvent::NONE) {
    Serial.println(message);
    if (event != ButtonEvent::NONE) {
      enqueueEvent(event);
    }
  }

private:
  static constexpr int EVENT_QUEUE_CAPACITY = 8;
  static constexpr unsigned long STARTUP_RECHECK_DELAY_MS = 500;

  void enqueueEvent(ButtonEvent event) {
    const int nextTail = (eventTail_ + 1) % EVENT_QUEUE_CAPACITY;
    if (nextTail == eventHead_) return;
    eventQueue_[eventTail_] = event;
    eventTail_ = nextTail;
  }

  OneButton btn_ = OneButton(Hardware::Pins::BUTTON, true, true);
  volatile ButtonEvent eventQueue_[EVENT_QUEUE_CAPACITY] = {};
  volatile int eventHead_ = 0;
  volatile int eventTail_ = 0;
  bool startupRecheckDone_ = false;
  unsigned long startupRecheckAtMs_ = 0;
};

static ButtonController btn;

static void onBtnClick() {
  btn.handleAction("[BTN] Single press", ButtonEvent::SHOW_NETWORK_INFO);
  Serial.println("[BTN] INFO: explicit call");
}

static void onBtnDoubleClick() {
  btn.handleAction("[BTN] Double click", ButtonEvent::SHOW_I2C_SCAN);
}

static void onBtnLongPressStart() {
  btn.handleAction("[BTN] Long press", ButtonEvent::SHOW_RTC_STATUS);
}

void buttonBegin()                    { btn.begin(); }
void buttonTick()                     { btn.tick(); }
bool buttonHasEvent()                 { return btn.hasEvent(); }
ButtonEvent buttonNextEvent()         { return btn.nextEvent(); }

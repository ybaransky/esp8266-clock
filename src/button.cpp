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
    pinMode(Hardware::Pins::INTERNAL_LED, OUTPUT);
    digitalWrite(Hardware::Pins::INTERNAL_LED, HIGH);

    pinMode(Hardware::Pins::BUTTON_1, INPUT);
    if (digitalRead(Hardware::Pins::BUTTON_1) == HIGH) {
      Serial.println("[BTN] WARNING: D8/GPIO15 is HIGH at startup (button may be pressed). Avoid holding this button during boot.");
    }
    startupRecheckAtMs_ = millis() + STARTUP_RECHECK_DELAY_MS;
    startupRecheckDone_ = false;

    // D8/GPIO15 has a board pull-down on D1 mini, so treat pressed state as HIGH.
    btn_.attachClick(onBtnClick);
    btn_.attachDoubleClick(onBtnDoubleClick);
    btn_.attachLongPressStart(onBtnLongPressStart);
  }

  void tick() {
    if (!startupRecheckDone_ && static_cast<long>(millis() - startupRecheckAtMs_) >= 0) {
      if (digitalRead(Hardware::Pins::BUTTON_1) == HIGH) {
        Serial.println("[BTN] WARNING: D8/GPIO15 still HIGH 500ms after startup. Check wiring or release button during boot.");
      }
      startupRecheckDone_ = true;
    }
    btn_.tick();
  }

  void ledTick(unsigned long now) {
    if (!ledPulseActive_ || static_cast<long>(now - ledOffAtMs_) < 0) return;
    digitalWrite(Hardware::Pins::INTERNAL_LED, HIGH);
    ledPulseActive_ = false;
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
    startLedPulse(LED_PULSE_MS);
    if (event != ButtonEvent::NONE) {
      enqueueEvent(event);
    }
  }

private:
  static constexpr int EVENT_QUEUE_CAPACITY = 8;
  static constexpr unsigned long LED_PULSE_MS = 200;
  static constexpr unsigned long STARTUP_RECHECK_DELAY_MS = 500;

  void enqueueEvent(ButtonEvent event) {
    const int nextTail = (eventTail_ + 1) % EVENT_QUEUE_CAPACITY;
    if (nextTail == eventHead_) return;
    eventQueue_[eventTail_] = event;
    eventTail_ = nextTail;
  }

  void startLedPulse(unsigned long durationMs) {
    digitalWrite(Hardware::Pins::INTERNAL_LED, LOW);
    ledPulseActive_ = true;
    ledOffAtMs_ = millis() + durationMs;
  }

  OneButton btn_ = OneButton(Hardware::Pins::BUTTON_1, false, false);
  volatile ButtonEvent eventQueue_[EVENT_QUEUE_CAPACITY] = {};
  volatile int eventHead_ = 0;
  volatile int eventTail_ = 0;
  bool ledPulseActive_ = false;
  unsigned long ledOffAtMs_ = 0;
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
void buttonLedTick(unsigned long now) { btn.ledTick(now); }
bool buttonHasEvent()                 { return btn.hasEvent(); }
ButtonEvent buttonNextEvent()         { return btn.nextEvent(); }

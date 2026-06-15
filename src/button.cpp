#include "button.h"

#include "hardware.h"
#include <Arduino.h>
#include <OneButton.h>

static void onBtnClick();
static void onBtnDoubleClick();
static void onBtnMultiClick();
static void onBtnLongPressStart();

class ButtonController {
public:
  void begin() {
    pinMode(Hardware::Pins::INTERNAL_LED, OUTPUT);
    digitalWrite(Hardware::Pins::INTERNAL_LED, LOW);

    btn.attachClick(onBtnClick);
    btn.attachDoubleClick(onBtnDoubleClick);
    btn.attachLongPressStart(onBtnLongPressStart);
    btn.attachMultiClick(onBtnMultiClick);

  }

  void tick() {
    btn.tick();
  }

  void ledTick(unsigned long now) {
    if (!ledPulseActive || static_cast<long>(now - ledOffAtMs) < 0) return;

    digitalWrite(Hardware::Pins::INTERNAL_LED, LOW);
    ledPulseActive = false;
  }

  bool hasEvent() const {
    return eventHead != eventTail;
  }

  ButtonEvent nextEvent() {
    if (eventHead == eventTail) return ButtonEvent::NONE;

    const ButtonEvent event = eventQueue[eventHead];
    eventHead = (eventHead + 1) % EVENT_QUEUE_CAPACITY;
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

  void enqueueEvent(ButtonEvent event) {
    const int nextTail = (eventTail + 1) % EVENT_QUEUE_CAPACITY;
    if (nextTail == eventHead) return;

    eventQueue[eventTail] = event;
    eventTail = nextTail;
  }

  void startLedPulse(unsigned long durationMs) {
    digitalWrite(Hardware::Pins::INTERNAL_LED, HIGH);
    ledPulseActive = true;
    ledOffAtMs = millis() + durationMs;
  }

  OneButton btn = OneButton(Hardware::Pins::BUTTON_1, true, true);
  volatile ButtonEvent eventQueue[EVENT_QUEUE_CAPACITY] = {};
  volatile int eventHead = 0;
  volatile int eventTail = 0;
  bool ledPulseActive = false;
  unsigned long ledOffAtMs = 0;
};

static ButtonController buttonController;

static void onBtnClick() {
  buttonController.handleAction("[BTN] Single press", ButtonEvent::TOGGLE_PRIMARY_PANEL);
}

static void onBtnDoubleClick() {
  buttonController.handleAction("[BTN] Double click", ButtonEvent::RESET_CURRENT_PANEL_DATA);
}

static void onBtnMultiClick() {
  buttonController.handleAction("[BTN] Triple click");
}

static void onBtnLongPressStart() {
  buttonController.handleAction("[BTN] Long press", ButtonEvent::SHOW_MENU_OR_RECENTER_HISTOGRAM);
}

void buttonBegin() {
  buttonController.begin();
}

void buttonTick() {
  buttonController.tick();
}

void buttonLedTick(unsigned long now) {
  buttonController.ledTick(now);
}

bool buttonHasEvent() {
  return buttonController.hasEvent();
}

ButtonEvent buttonNextEvent() {
  return buttonController.nextEvent();
}

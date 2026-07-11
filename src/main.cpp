#include <Arduino.h>

#include "clock_application.h"

ClockApplication application;

void setup() {
  application.begin();
}

void loop() {
  application.tick(millis());
}

#include <Arduino.h>

#include "clock_application.h"

ClockApplication application;  // Owns every service; setup() and loop() delegate to it.

void setup() {
  application.begin();
}

void loop() {
  application.tick(millis());
}

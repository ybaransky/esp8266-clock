#pragma once

#include <Arduino.h>
#include "display.h"

// Pure rendering functions. Each converts explicit application data into a
// hardware-independent frame and performs no I/O or scheduling.
DisplayFrame renderDemoDisplayFrame(uint8_t wholeSeconds, uint8_t tenths);
DisplayFrame renderMessageDisplayFrame(const char* message, bool visible);
DisplayFrame renderPageDisplayFrame(const char* panel1,
                                    const char* panel2,
                                    const char* panel3,
                                    bool firstPanelVisible);

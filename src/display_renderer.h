#pragma once

#include <Arduino.h>
#include <RTClib.h>

#include "display_frame.h"

// Pure rendering functions. Each converts explicit application data into a
// hardware-independent frame and performs no I/O or scheduling.
DisplayFrame renderClockDisplayFrame(uint8_t formatIndex,
                                     const DateTime& now,
                                     bool use12Hour,
                                     uint8_t tenths,
                                     bool colonVisible);
DisplayFrame renderCountdownDisplayFrame(uint8_t formatIndex,
                                         long totalSeconds,
                                         uint8_t tenths);
DisplayFrame renderCountupDisplayFrame(uint8_t formatIndex,
                                       long totalSeconds,
                                       uint8_t tenths);
DisplayFrame renderDemoDisplayFrame(uint8_t wholeSeconds, uint8_t tenths);
DisplayFrame renderMessageDisplayFrame(const char* message, bool visible);
DisplayFrame renderPageDisplayFrame(const char* row1,
                                    const char* row2,
                                    const char* row3,
                                    bool firstRowVisible);

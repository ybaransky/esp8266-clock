#pragma once
// Minimal host stand-in for the Arduino core, for the native test env only.
// Provides just the macros and types the pure modules under test reach for.
// Deliberately tiny: anything that needs more of the core than this is not a
// pure module and does not belong in a host test.

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#ifndef constrain
#define constrain(amt, low, high) \
  ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))
#endif

#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif

#ifndef max
#define max(a, b) ((a) > (b) ? (a) : (b))
#endif

// Flash-residency macros. On the host everything is already addressable, so
// these collapse to their plain-memory equivalents; the firmware build gets the
// real ones from the ESP8266 core.
#ifndef PROGMEM
#define PROGMEM
#endif
#ifndef PSTR
#define PSTR(s) (s)
#endif
#ifndef strncpy_P
#define strncpy_P strncpy
#endif
#ifndef strncmp_P
#define strncmp_P strncmp
#endif
#ifndef strcmp_P
#define strcmp_P strcmp
#endif
#ifndef memcpy_P
#define memcpy_P memcpy
#endif
#ifndef pgm_read_byte
#define pgm_read_byte(address) (*(const uint8_t*)(address))
#endif

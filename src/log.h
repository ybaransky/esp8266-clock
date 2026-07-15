#pragma once

#include <Arduino.h>

using LogTimeProvider = bool (*)(char* buffer, size_t bufferSize);

const char* logSourceName(const char* path);
const char* logCurrentTime();
void logSetTimeProvider(LogTimeProvider provider);

// Peak cont-stack usage in bytes (high-water mark since boot, of 4096);
// printed on every log line by LOG_PRINTF, right after the time.
unsigned logStackUsed();

// Both macros keep their format strings in flash (PSTR + printf_P) - on the
// ESP8266, a plain string literal would otherwise occupy RAM for the life of
// the program. This requires `message`/`format` to be a string literal; for
// a runtime string use LOG_PRINTF("%s", value).
//
// Each call emits one complete line: `<time> <stack> <file>:<line> <msg>`.
// The macro appends the terminating newline itself, so `message`/`format`
// must NOT end in \n.
#define LOG_PRINTLN(message) LOG_PRINTF(message)

#define LOG_PRINTF(format, ...) \
  do { Serial.printf_P(PSTR("%s %u %s:%d " format "\n"), logCurrentTime(), logStackUsed(), logSourceName(__FILE__), __LINE__, ##__VA_ARGS__); } while (0)

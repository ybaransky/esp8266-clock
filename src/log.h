#pragma once

#include <Arduino.h>

using LogTimeProvider = bool (*)(char* buffer, size_t bufferSize);

const char* logSourceName(const char* path);
const char* logCurrentTime();
void logSetTimeProvider(LogTimeProvider provider);

// Both macros keep their format strings in flash (PSTR + printf_P) - on the
// ESP8266, a plain string literal would otherwise occupy RAM for the life of
// the program. This requires `message`/`format` to be a string literal; for
// a runtime string use LOG_PRINTF("%s\n", value).
#define LOG_PRINTLN(message) LOG_PRINTF(message "\n")

#define LOG_PRINTF(format, ...) \
  do { Serial.printf_P(PSTR("%s %s:%d " format), logCurrentTime(), logSourceName(__FILE__), __LINE__, ##__VA_ARGS__); } while (0)

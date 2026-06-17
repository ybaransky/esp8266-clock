#pragma once

#include <Arduino.h>

const char* logSourceName(const char* path);
const char* logCurrentTime();

#define LOG_PRINTLN(message) \
  do { Serial.printf("%s %s:%d %s\n", logCurrentTime(), logSourceName(__FILE__), __LINE__, message); } while (0)

#define LOG_PRINTF(format, ...) \
  do { Serial.printf("%s %s:%d " format, logCurrentTime(), logSourceName(__FILE__), __LINE__, ##__VA_ARGS__); } while (0)

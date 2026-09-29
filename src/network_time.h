#pragma once

#include <Arduino.h>

class ClockController;
class RtcService;
class WifiConnectionManager;

// Keeps the DS3231 on network time while the clock is on the home WiFi.
//
// The ESP8266 SDK's SNTP client polls pool.ntp.org (hourly by default) and
// sets the system clock; its callback only raises a flag. The main loop then
// waits for the system clock's next whole-second rollover, compares with the
// RTC, and corrects the chip through ClockController when they disagree by
// 2 s or more (or the RTC's time is not trusted). The system clock is never
// used for display; the DS3231 remains the only time source the clock reads.
//
// Inactive in access-point mode, where there is no route to a time server and
// the browser remains the source.
class NetworkTimeSync {
 public:
  NetworkTimeSync(ClockController& clockController, RtcService& rtc,
                  const WifiConnectionManager& wifi)
      : m_clockController(clockController), m_rtc(rtc), m_wifi(wifi) {}

  // Starts SNTP if the station connection is up. Call after WiFi setup.
  void begin();
  void tick();

  bool active() const { return m_active; }
  // UTC second of the last successful sync, or 0 before the first.
  uint32_t lastSyncUtc() const { return m_lastSyncUtc; }

 private:
  void applySync(uint32_t utc);

  ClockController& m_clockController;  // Writes corrected time through the application.
  RtcService& m_rtc;                   // Source of the RTC's current UTC for comparison.
  const WifiConnectionManager& m_wifi; // Decides whether SNTP can run (station mode).
  bool m_active = false;               // True once SNTP has been started.
  volatile bool m_syncPending = false; // Set by the SNTP callback, consumed by tick().
  bool m_awaitingRollover = false;     // Waiting for the system clock's next second.
  uint32_t m_syncSecond = 0;           // System-clock second when the sync was noticed.
  uint32_t m_lastSyncUtc = 0;          // UTC second of the last applied sync.
};

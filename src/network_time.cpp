#include "network_time.h"

#include <coredecls.h>
#include <lwip/apps/sntp.h>
#include <time.h>

#include "clock_controller.h"
#include "log.h"
#include "rtc_ds3231.h"
#include "wifi_connection_manager.h"

namespace {

constexpr char kNtpServer[] = "pool.ntp.org";     // Public NTP pool.
constexpr uint32_t kEarliestPlausibleUtc = 1704067200;  // 2024-01-01; the system clock before a sync is ~0.
constexpr int32_t kCorrectionThresholdSeconds = 2;  // Smaller drift is second-phase noise.

}  // namespace

void NetworkTimeSync::begin() {
  if (m_wifi.status().mode != WifiMode::kStation) {
    LOG_PRINTLN("NTP inactive: access-point mode has no route to a time server");
    return;
  }
  // Runs in lwIP context: only raise the flag; tick() does the work.
  settimeofday_cb([this](bool fromSntp) {
    if (fromSntp) m_syncPending = true;
  });
  sntp_stop();
  sntp_setservername(0, kNtpServer);
  sntp_init();
  m_active = true;
  LOG_PRINTF("NTP started: %s", kNtpServer);
}

void NetworkTimeSync::tick() {
  if (m_syncPending) {
    m_syncPending = false;
    const uint32_t now = static_cast<uint32_t>(time(nullptr));
    if (now < kEarliestPlausibleUtc) {
      LOG_PRINTF("NTP sync ignored: implausible time %lu", static_cast<unsigned long>(now));
      return;
    }
    m_syncSecond = now;
    m_awaitingRollover = true;
  }
  if (!m_awaitingRollover) return;

  // Writing the DS3231 seconds register restarts its 1 Hz chain, so writing
  // right at a system-clock rollover lines the chip's second up with NTP's.
  const uint32_t now = static_cast<uint32_t>(time(nullptr));
  if (now == m_syncSecond) return;
  m_awaitingRollover = false;
  applySync(now);
}

void NetworkTimeSync::applySync(uint32_t utc) {
  m_lastSyncUtc = utc;
  const int32_t drift = static_cast<int32_t>(utc - m_rtc.getUtcCached());
  const bool untrusted = !m_rtc.timeIsTrustworthy();
  if (!untrusted && (abs(drift) < kCorrectionThresholdSeconds)) {
    LOG_PRINTF("NTP sync: RTC within %ld s, no correction", static_cast<long>(drift));
    return;
  }
  LOG_PRINTF("NTP sync: correcting RTC by %ld s%s", static_cast<long>(drift),
             untrusted ? " (RTC time was untrusted)" : "");
  m_clockController.setTime(utc, "NTP sync");
}

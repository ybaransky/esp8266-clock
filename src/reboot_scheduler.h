#pragma once

#include <stdint.h>

// The one thing the API handlers need from the web portal that owns them:
// "restart shortly, after this response has gone out."
//
// Exists to break an ownership cycle. WebPortal constructs ConfigApi and
// WifiApi and previously passed them `*this`, so the objects it owned depended
// on the class that owned them - and config_api.cpp had to include
// web_server.h to say so. A one-method interface costs one vtable and lets the
// dependency point the right way.
class RebootScheduler {
 public:
  virtual ~RebootScheduler() = default;

  // Requests a restart `delayMs` from now. Returns immediately; the reboot
  // happens from the main loop once the pending response has been delivered.
  virtual void scheduleReboot(uint32_t delayMs) = 0;
};

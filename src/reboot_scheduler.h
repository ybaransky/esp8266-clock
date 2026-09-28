#pragma once

#include <stdint.h>

// The one thing the API handlers need from the web portal that owns them:
// "restart shortly, after this response has gone out."
//
// Exists to break an ownership cycle. WebPortal constructs ConfigApi and
// WifiApi; handing them the WebPortal itself would make the objects it owns
// depend on their owner, and config_api.cpp include web_server.h. A one-method
// interface costs one vtable and lets the dependency point the right way.
class RebootScheduler {
 public:
  virtual ~RebootScheduler() = default;

  // Requests a restart `delayMs` from now. Returns immediately; the reboot
  // happens from the main loop once the pending response has been delivered.
  virtual void scheduleReboot(uint32_t delayMs) = 0;
};

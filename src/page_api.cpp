#include "page_api.h"

#include "config_validation.h"
#include "html.h"

void PageApi::handleRoot(const WifiRuntimeStatus& status) {
  const ClockConfig config = configManager_.loadClockConfig();
  String ssid;
  String ip;
  networkInfoFromStatus(status, ssid, ip);

  String page(FPSTR(INDEX_HTML));
  page.replace("__DEVICE_NAME__", ssid.isEmpty() ? "Clock" : ssid);
  page.replace("__INITIAL_MODE__", modeName(config.activeMode));
  responder_.logRequest(200, page.length());
  server_.send(200, "text/html", page);
}

void PageApi::sendHtml(PGM_P html) {
  responder_.sendProgmem(200, "text/html", html);
}

void PageApi::networkInfoFromStatus(const WifiRuntimeStatus& status,
                                    String& ssid,
                                    String& ip) {
  if (status.mode == WifiMode::kStation && status.connected) {
    ssid = status.ssid;
    ip = status.ip;
    return;
  }
  ssid = status.apSsid;
  ip = status.apIp;
}

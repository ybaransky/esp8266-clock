#include "page_api.h"

#include "html.h"

void PageApi::handleRoot(const WifiRuntimeStatus& status) {
  const ClockConfig config = configManager.loadClockConfig();
  String ssid;
  String ip;
  networkInfoFromStatus(status, ssid, ip);

  String page(FPSTR(INDEX_HTML));
  page.replace("__DEVICE_NAME__", ssid.isEmpty() ? "Clock" : ssid);
  page.replace("__INITIAL_MODE__", modeName(config.activeMode));
  responder_.logRequest(200, page.length());
  server_.send(200, "text/html", page);
}

void PageApi::handleSettings() {
  responder_.sendProgmem(200, "text/html", SETTINGS_HTML);
}

void PageApi::handleConfigDirectory() {
  responder_.sendProgmem(200, "text/html", CONFIG_JSON_HTML);
}

void PageApi::handleFormat() {
  responder_.sendProgmem(200, "text/html", CONFIG_HTML);
}

void PageApi::handleTimeSync() {
  responder_.sendProgmem(200, "text/html", TIME_SYNC_HTML);
}

void PageApi::handleMessage() {
  responder_.sendProgmem(200, "text/html", MESSAGE_HTML);
}

void PageApi::handleLocation() {
  responder_.sendProgmem(200, "text/html", LOCATION_HTML);
}

void PageApi::handleWifi() {
  responder_.sendProgmem(200, "text/html", WIFI_HTML);
}

void PageApi::handleViewFile() {
  responder_.sendProgmem(200, "text/html", VIEW_FILE_HTML);
}

const char* PageApi::modeName(PersistentMode mode) {
  switch (mode) {
    case kPersistentCountdown:
      return "countdown";
    case kPersistentCountup:
      return "countup";
    case kPersistentClock:
      return "clock";
    default:
      return "clock";
  }
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

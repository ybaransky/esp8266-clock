#include "web_server.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include "clock_state.h"
#include "config.h"
#include "format.h"
#include "hardware.h"
#include "html.h"
#include "log.h"
#include "rtc_ds3231.h"
#include "wifi_connection_manager.h"

// Forward declarations for server route callbacks.
static void handleRootRoute();
static void handleConfigGetRoute();
static void handleFormatRoute();
static void handleTimeSyncRoute();
static void handleMessageRoute();
static void handleWifiRoute();
static void handleApiDemoTestRoute();
static void handleApiMessageTestRoute();
static void handleApiSetModeRoute();
static void handleApiBrightnessRoute();
static void handleApiTimeRoute();
static void handleApiTimeSyncRoute();
static void handleApiFormatsRoute();
static void handleApiGetConfigRoute();
static void handleApiSaveConfigRoute();
static void handleApiWifiStatusRoute();
static void handleApiWifiScanRoute();
static void handleApiWifiConnectRoute();
static void handleCaptiveRedirectRoute();

// ── WebPortal ─────────────────────────────────────────────────────────────────

class WebPortal {
public:
  WebPortal() : server_(80) {}

  void begin() {
    if (wifiConnectionManager.status().mode == WifiMode::kAccessPoint) {
      dnsRunning_ = dnsServer_.start(53, "*", WiFi.softAPIP());
      if (!dnsRunning_) {
        LOG_PRINTLN("Failed to start captive DNS server (no socket available)");
      }
    }

    server_.on("/",                    HTTP_GET,  handleRootRoute);
    server_.on("/config",              HTTP_GET,  handleConfigGetRoute);
    server_.on("/format",              HTTP_GET,  handleFormatRoute);
    server_.on("/time-sync",           HTTP_GET,  handleTimeSyncRoute);
    server_.on("/messages",            HTTP_GET,  handleMessageRoute);
    server_.on("/wifi",                HTTP_GET,  handleWifiRoute);
    server_.on("/api/demo/test",       HTTP_POST, handleApiDemoTestRoute);
    server_.on("/api/message/test",    HTTP_POST, handleApiMessageTestRoute);
    server_.on("/api/mode",            HTTP_POST, handleApiSetModeRoute);
    server_.on("/api/brightness",      HTTP_POST, handleApiBrightnessRoute);
    server_.on("/api/time",            HTTP_GET,  handleApiTimeRoute);
    server_.on("/api/time/sync",       HTTP_POST, handleApiTimeSyncRoute);
    server_.on("/api/formats",         HTTP_GET,  handleApiFormatsRoute);
    server_.on("/api/config",          HTTP_GET,  handleApiGetConfigRoute);
    server_.on("/api/config",          HTTP_POST, handleApiSaveConfigRoute);
    server_.on("/api/wifi/status",     HTTP_GET,  handleApiWifiStatusRoute);
    server_.on("/api/wifi/scan",       HTTP_GET,  handleApiWifiScanRoute);
    server_.on("/api/wifi/connect",    HTTP_POST, handleApiWifiConnectRoute);
    server_.onNotFound(handleCaptiveRedirectRoute);
    server_.begin();
    LOG_PRINTLN("HTTP server started");
  }

  void handleClients() {
    if (dnsRunning_) dnsServer_.processNextRequest();
    server_.handleClient();
    if (pendingRebootMs_ && millis() >= pendingRebootMs_) {
      LOG_PRINTLN("Rebooting...");
      ESP.restart();
    }
  }

  void getNetworkInfo(String &ssid, String &ip) {
    const WifiRuntimeStatus status = wifiConnectionManager.status();
    if (status.mode == WifiMode::kStation && status.connected) {
      ssid = status.ssid;
      ip = status.ip;
      return;
    }
    ssid = status.apSsid;
    ip = status.apIp;
  }

  // ── GET / ────────────────────────────────────────────────────────────────────
  void handleRoot() {
    logRequest(200);
    server_.send_P(200, "text/html", INDEX_HTML);
  }

  // ── GET /config ───────────────────────────────────────────────────────────────
  void handleConfigGet() {
    logRequest(200);
    server_.send_P(200, "text/html", CONFIG_JSON_HTML);
  }

  void handleFormat() {
    logRequest(200);
    server_.send_P(200, "text/html", CONFIG_HTML);
  }

  void handleTimeSync() {
    logRequest(200);
    server_.send_P(200, "text/html", TIME_SYNC_HTML);
  }

  void handleMessage() {
    logRequest(200);
    server_.send_P(200, "text/html", MESSAGE_HTML);
  }

  // ── GET /wifi ─────────────────────────────────────────────────────────────────
  void handleWifi() {
    logRequest(200);
    server_.send_P(200, "text/html", WIFI_HTML);
  }

  // ── POST /api/demo/test ──────────────────────────────────────────────────────────
  // Applies demo mode in memory. If finalMessage is supplied, previews it without
  // persisting. Returns preview_ms so the page knows when the demo should finish.
  void handleApiDemoTest() {
    logRequest(200);
    if (server_.hasArg("plain") && server_.arg("plain").length() > 0) {
      JsonDocument doc;
      DeserializationError err = deserializeJson(doc, server_.arg("plain"));
      if (err) {
        server_.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
      }

      if (!doc["finalMessage"].isNull()) {
        ClockConfig cfg = configManager.loadClockConfig();
        snprintf(cfg.finalMessage, sizeof(cfg.finalMessage), "%s", doc["finalMessage"].as<const char*>());
        clockApplySettings(cfg);
      }
    }

    clockTriggerDemo();
    // 5-second countdown + 5 seconds of blinking = 10 s preview
    server_.send(200, "application/json", "{\"preview_ms\":10000}");
  }

  void handleApiMessageTest() {
    logRequest(200);
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, server_.arg("plain"));
    if (err) {
      server_.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
      return;
    }

    const char* message = doc["message"] | "";
    clockShowMessagePreview(message);
    server_.send(200, "application/json", "{\"message\":\"Previewing message\",\"preview_ms\":5000}");
  }

  void handleApiSetMode() {
    logRequest(200);
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, server_.arg("plain"));
    if (err) {
      server_.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
      return;
    }

    const String mode = doc["mode"] | "";
    ClockConfig cfg = configManager.loadClockConfig();
    if (mode == "countdown") {
      cfg.activeMode = kPersistentCountdown;
    } else if (mode == "countup") {
      cfg.activeMode = kPersistentCountup;
    } else if (mode == "clock") {
      cfg.activeMode = kPersistentClock;
    } else {
      server_.send(400, "application/json", "{\"error\":\"Invalid mode\"}");
      return;
    }

    configManager.saveClockConfig(cfg);
    clockApplySettings(cfg);
    server_.send(200, "application/json", "{\"message\":\"Mode changed\"}");
  }

  void handleApiBrightness() {
    logRequest(200);
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, server_.arg("plain"));
    if (err) {
      server_.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
      return;
    }

    if (doc["brightness"].isNull()) {
      server_.send(400, "application/json", "{\"error\":\"Brightness required\"}");
      return;
    }

    clockSetBrightness(constrain(doc["brightness"].as<int>(), 0, 7));
    server_.send(200, "application/json", "{\"message\":\"Brightness previewed\"}");
  }

  // ── GET /api/time ─────────────────────────────────────────────────────────────
  void handleApiTime() {
    logRequest(200);
    const DateTime dt = rtcGetNow();
    char buf[96];
    snprintf(buf, sizeof(buf),
             "{\"date\":\"%04d-%02d-%02d\",\"time\":\"%02d:%02d:%02d\",\"dateTime\":\"%04d-%02d-%02d %02d:%02d:%02d\"}",
             dt.year(), dt.month(), dt.day(), dt.hour(), dt.minute(), dt.second(),
             dt.year(), dt.month(), dt.day(), dt.hour(), dt.minute(), dt.second());
    server_.send(200, "application/json", buf);
  }

  void handleApiTimeSync() {
    logRequest(200);
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, server_.arg("plain"));
    if (err) {
      server_.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
      return;
    }

    const int year = doc["year"] | 0;
    const int month = doc["month"] | 0;
    const int day = doc["day"] | 0;
    const int hour = doc["hour"] | 0;
    const int minute = doc["minute"] | 0;
    const int second = doc["second"] | 0;
    if (year < 2020 || year > 2099 || month < 1 || month > 12 ||
        day < 1 || day > 31 || hour < 0 || hour > 23 ||
        minute < 0 || minute > 59 || second < 0 || second > 59) {
      server_.send(400, "application/json", "{\"error\":\"Invalid time\"}");
      return;
    }

    rtcSetNow(DateTime(year, month, day, hour, minute, second));
    server_.send(200, "application/json", "{\"message\":\"RTC synced\"}");
  }

  // ── GET /api/formats ──────────────────────────────────────────────────────────
  void handleApiFormats() {
    logRequest(200);
    JsonDocument doc;
    JsonArray cd = doc["countdown"].to<JsonArray>();
    for (uint8_t i = 0; i < formatCount(kFmtGroupCountdown); ++i)
      cd.add(getFormat(kFmtGroupCountdown, i));
    JsonArray cu = doc["countup"].to<JsonArray>();
    for (uint8_t i = 0; i < formatCount(kFmtGroupCountUp); ++i)
      cu.add(getFormat(kFmtGroupCountUp, i));
    JsonArray ck = doc["clock"].to<JsonArray>();
    for (uint8_t i = 0; i < formatCount(kFmtGroupClock); ++i)
      ck.add(getFormat(kFmtGroupClock, i));
    String json;
    serializeJson(doc, json);
    server_.send(200, "application/json", json);
  }

  // ── GET /api/config ───────────────────────────────────────────────────────────
  void handleApiGetConfig() {
    logRequest(200);
    const ClockConfig s   = configManager.loadClockConfig();
    const WifiConfig      cfg = configManager.loadWifiConfig();
    JsonDocument doc;
    doc["mode"]            = static_cast<int>(s.activeMode);
    doc["countdownFmt"]    = s.countdownFmt;
    doc["countupFmt"]      = s.countupFmt;
    doc["clockFmt"]        = s.clockFmt;
    doc["brightness"]      = s.brightness;
    doc["countdownDatetime"] = s.countdownDatetime;
    doc["countupDatetime"]    = s.countupDatetime;
    doc["splashMessage"]   = s.splashMessage;
    doc["finalMessage"]    = s.finalMessage;
    doc["staSsid"]         = cfg.staSsid;
    doc["apSsid"]          = cfg.apSsid;
    // password is never returned — client sends a new one only if changing it
    String json;
    serializeJson(doc, json);
    server_.send(200, "application/json", json);
  }

  // ── POST /api/config ──────────────────────────────────────────────────────────
  void handleApiSaveConfig() {
    logRequest(200);
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, server_.arg("plain"));
    if (err) {
      server_.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
      return;
    }

    // ── Clock settings ────────────────────────────────────────────────────────
    ClockConfig s = configManager.loadClockConfig();
    if (!doc["mode"].isNull())            s.activeMode    = static_cast<PersistentMode>(doc["mode"].as<int>());
    if (!doc["countdownFmt"].isNull())    s.countdownFmt  = doc["countdownFmt"].as<uint8_t>();
    if (!doc["countupFmt"].isNull())      s.countupFmt    = doc["countupFmt"].as<uint8_t>();
    if (!doc["clockFmt"].isNull())        s.clockFmt      = doc["clockFmt"].as<uint8_t>();
    if (!doc["brightness"].isNull())      s.brightness    = constrain(doc["brightness"].as<int>(), 0, 7);
    if (!doc["countdownDatetime"].isNull()) snprintf(s.countdownDatetime, sizeof(s.countdownDatetime), "%s", doc["countdownDatetime"].as<const char*>());
    if (!doc["countupDatetime"].isNull())    snprintf(s.countupDatetime,    sizeof(s.countupDatetime),    "%s", doc["countupDatetime"].as<const char*>());
    if (!doc["splashMessage"].isNull())   snprintf(s.splashMessage,   sizeof(s.splashMessage),   "%s", doc["splashMessage"].as<const char*>());
    if (!doc["finalMessage"].isNull())    snprintf(s.finalMessage,    sizeof(s.finalMessage),    "%s", doc["finalMessage"].as<const char*>());
    configManager.saveClockConfig(s);
    clockApplySettings(s);

    // ── WiFi settings (optional — reboot required to apply) ───────────────────
    bool wifiChanged = false;
    if (!doc["staSsid"].isNull() || !doc["staPassword"].isNull() ||
        !doc["apSsid"].isNull() || !doc["apPassword"].isNull()) {
      WifiConfig cfg = configManager.loadWifiConfig();
      if (!doc["staSsid"].isNull())         cfg.staSsid         = doc["staSsid"].as<String>();
      if (!doc["staPassword"].isNull())     cfg.staPassword     = doc["staPassword"].as<String>();
      if (!doc["apSsid"].isNull())          cfg.apSsid          = doc["apSsid"].as<String>();
      if (!doc["apPassword"].isNull())      cfg.apPassword      = doc["apPassword"].as<String>();
      configManager.saveWifiConfig(cfg);
      wifiChanged = true;
    }

    if (wifiChanged) {
      server_.send(200, "application/json", "{\"message\":\"Saved \xe2\x80\x94 rebooting\xe2\x80\xa6\",\"reboot\":true}");
      pendingRebootMs_ = millis() + 1500;
    } else {
      server_.send(200, "application/json", "{\"message\":\"Saved\"}");
    }
  }

  // ── GET /api/wifi/status ──────────────────────────────────────────────────────
  void handleApiWifiStatus() {
    logRequest(200);
    const WifiRuntimeStatus status = wifiConnectionManager.status();
    JsonDocument doc;
    doc["mode"] = status.mode == WifiMode::kStation ? "station" : "access_point";
    doc["connected"] = status.connected;
    doc["ssid"] = status.ssid;
    doc["ip"] = status.ip;
    doc["apSsid"] = status.apSsid;
    doc["apIp"] = status.apIp;
    String json;
    serializeJson(doc, json);
    server_.send(200, "application/json", json);
  }

  void handleApiWifiScan() {
    logRequest(200);
    JsonDocument doc;
    wifiConnectionManager.scanNetworks(doc);
    String json;
    serializeJson(doc, json);
    server_.send(200, "application/json", json);
  }

  // POST /api/wifi/connect
  void handleApiWifiConnect() {
    logRequest(200);
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, server_.arg("plain"));
    if (err) {
      server_.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
      return;
    }
    const String ssid = doc["ssid"] | "";
    const String password = doc["password"] | "";
    if (!wifiConnectionManager.connectAndSave(ssid, password)) {
      server_.send(400, "application/json", "{\"error\":\"SSID required\"}");
      return;
    }

    server_.send(200, "application/json", "{\"message\":\"Saved - rebooting...\",\"reboot\":true}");
    pendingRebootMs_ = millis() + 1500;
  }

  void handleCaptiveRedirect() {
    if (wifiConnectionManager.status().mode != WifiMode::kAccessPoint) {
      logRequest(404);
      server_.send(404, "text/plain", "Not found");
      return;
    }

    logRequest(302);
    server_.sendHeader("Location", "http://192.168.4.1/", true);
    server_.send(302, "text/plain", "");
  }

private:
  static const char *methodName(HTTPMethod method) {
    switch (method) {
      case HTTP_GET:    return "GET";
      case HTTP_POST:   return "POST";
      case HTTP_PUT:    return "PUT";
      case HTTP_DELETE: return "DELETE";
      case HTTP_PATCH:  return "PATCH";
      default:          return "OTHER";
    }
  }

  void logRequest(int status) {
    LOG_PRINTF("%s %s <- %s  => %d\n",
               methodName(server_.method()),
               server_.uri().c_str(),
               server_.client().remoteIP().toString().c_str(),
               status);
  }

  ESP8266WebServer server_;
  DNSServer        dnsServer_;
  bool             dnsRunning_      = false;
  uint32_t         pendingRebootMs_ = 0;
};

static WebPortal portal;

static void handleRootRoute()            { portal.handleRoot(); }
static void handleConfigGetRoute()       { portal.handleConfigGet(); }
static void handleFormatRoute()          { portal.handleFormat(); }
static void handleTimeSyncRoute()        { portal.handleTimeSync(); }
static void handleMessageRoute()         { portal.handleMessage(); }
static void handleWifiRoute()            { portal.handleWifi(); }
static void handleApiDemoTestRoute()     { portal.handleApiDemoTest(); }
static void handleApiMessageTestRoute()  { portal.handleApiMessageTest(); }
static void handleApiSetModeRoute()      { portal.handleApiSetMode(); }
static void handleApiBrightnessRoute()   { portal.handleApiBrightness(); }
static void handleApiTimeRoute()         { portal.handleApiTime(); }
static void handleApiTimeSyncRoute()     { portal.handleApiTimeSync(); }
static void handleApiFormatsRoute()      { portal.handleApiFormats(); }
static void handleApiGetConfigRoute()    { portal.handleApiGetConfig(); }
static void handleApiSaveConfigRoute()   { portal.handleApiSaveConfig(); }
static void handleApiWifiStatusRoute()   { portal.handleApiWifiStatus(); }
static void handleApiWifiScanRoute()     { portal.handleApiWifiScan(); }
static void handleApiWifiConnectRoute()  { portal.handleApiWifiConnect(); }
static void handleCaptiveRedirectRoute() { portal.handleCaptiveRedirect(); }

void webBegin()                                        { portal.begin(); }
void webHandleClients()                               { portal.handleClients(); }
void networkGetInfo(String &ssid, String &ip)          { portal.getNetworkInfo(ssid, ip); }

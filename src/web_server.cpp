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
#include "rtc_ds3231.h"

// Forward declarations for server route callbacks.
static void handleRootRoute();
static void handleConfigGetRoute();
static void handleWifiRoute();
static void handleUtilityRoute();
static void handleDemoRoute();
static void handleApiDemoTestRoute();
static void handleApiTimeRoute();
static void handleApiFormatsRoute();
static void handleApiGetConfigRoute();
static void handleApiSaveConfigRoute();
static void handleApiConfigRawRoute();
static void handleApiConfigDeleteRoute();
static void handleApiWifiGetRoute();
static void handleApiWifiSaveRoute();
static void handleCaptiveRedirectRoute();

// ── WebPortal ─────────────────────────────────────────────────────────────────

class WebPortal {
public:
  WebPortal() : server_(80) {}

  void begin(const char *ssid, const char *password) {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(ssid, password);
    waitForSoftApIp();

    Serial.printf("[WIFI] AP \"%s\" started  IP: %s\n", ssid, WiFi.softAPIP().toString().c_str());

    dnsRunning_ = dnsServer_.start(53, "*", WiFi.softAPIP());
    if (!dnsRunning_) {
      Serial.println("[DNS] Failed to start captive DNS server (no socket available)");
    }

    server_.on("/",                    HTTP_GET,  handleRootRoute);
    server_.on("/config",              HTTP_GET,  handleConfigGetRoute);
    server_.on("/wifi",                HTTP_GET,  handleWifiRoute);
    server_.on("/utility",             HTTP_GET,  handleUtilityRoute);
    server_.on("/demo",                HTTP_GET,  handleDemoRoute);
    server_.on("/api/demo/test",       HTTP_POST, handleApiDemoTestRoute);
    server_.on("/api/time",            HTTP_GET,  handleApiTimeRoute);
    server_.on("/api/formats",         HTTP_GET,  handleApiFormatsRoute);
    server_.on("/api/config",          HTTP_GET,  handleApiGetConfigRoute);
    server_.on("/api/config",          HTTP_POST, handleApiSaveConfigRoute);
    server_.on("/api/config/raw",      HTTP_GET,  handleApiConfigRawRoute);
    server_.on("/api/config/delete",   HTTP_POST, handleApiConfigDeleteRoute);
    server_.on("/api/wifi",            HTTP_GET,  handleApiWifiGetRoute);
    server_.on("/api/wifi",            HTTP_POST, handleApiWifiSaveRoute);
    server_.onNotFound(handleCaptiveRedirectRoute);
    server_.begin();
    Serial.println("[WIFI] HTTP server started");
  }

  void handleClients() {
    if (dnsRunning_) dnsServer_.processNextRequest();
    server_.handleClient();
    if (pendingRebootMs_ && millis() >= pendingRebootMs_) {
      Serial.println("[WEB] Rebooting...");
      ESP.restart();
    }
  }

  void getNetworkInfo(String &ssid, String &ip) {
    ssid = WiFi.softAPSSID();
    ip   = WiFi.softAPIP().toString();
  }

  // ── GET / ────────────────────────────────────────────────────────────────────
  void handleRoot() {
    logRequest(200);
    server_.send_P(200, "text/html", INDEX_HTML);
  }

  // ── GET /config ───────────────────────────────────────────────────────────────
  void handleConfigGet() {
    logRequest(200);
    server_.send_P(200, "text/html", CONFIG_HTML);
  }

  // ── GET /wifi ─────────────────────────────────────────────────────────────────
  void handleWifi() {
    logRequest(200);
    server_.send_P(200, "text/html", WIFI_HTML);
  }

  // ── GET /utility ──────────────────────────────────────────────────────────────
  void handleUtility() {
    logRequest(200);
    server_.send_P(200, "text/html", UTILITY_HTML);
  }

  // ── GET /demo ───────────────────────────────────────────────────────────────────
  void handleDemo() {
    logRequest(200);
    server_.send_P(200, "text/html", DEMO_HTML);
  }

  // ── POST /api/demo/test ──────────────────────────────────────────────────────────
  // Applies demo mode in memory (not persisted). Returns preview_ms so the
  // page knows when to show the Save button.
  void handleApiDemoTest() {
    logRequest(200);
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, server_.arg("plain"));
    if (err) {
      server_.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
      return;
    }
    // Patch finalMessage into the live settings without persisting, then fire the overlay.
    if (!doc["finalMessage"].isNull()) {
      ClockConfig s = configManager.loadClockConfig();
      snprintf(s.finalMessage, sizeof(s.finalMessage), "%s", doc["finalMessage"].as<const char*>());
      clockApplySettings(s);
    }
    clockTriggerDemo();
    // 5-second countdown + 5 seconds of blinking = 10 s preview
    server_.send(200, "application/json", "{\"preview_ms\":10000}");
  }

  // ── GET /api/time ─────────────────────────────────────────────────────────────
  void handleApiTime() {
    logRequest(200);
    const DateTime dt = rtcGetNow();
    char buf[32];
    snprintf(buf, sizeof(buf), "{\"time\":\"%02d:%02d:%02d\"}",
             dt.hour(), dt.minute(), dt.second());
    server_.send(200, "application/json", buf);
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
    JsonArray ju = doc["justification"].to<JsonArray>();
    for (uint8_t i = 0; i < formatCount(kFmtGroupJustification); ++i)
      ju.add(getFormat(kFmtGroupJustification, i));
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
    doc["justification"]   = s.justification;
    doc["brightness"]      = s.brightness;
    doc["countdownDatetime"] = s.countdownDatetime;
    doc["countupDatetime"]    = s.countupDatetime;
    doc["splashMessage"]   = s.splashMessage;
    doc["finalMessage"]    = s.finalMessage;
    doc["ssid"]            = cfg.ssid;
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
    if (!doc["mode"].isNull())            s.activeMode    = static_cast<BaseMode>(doc["mode"].as<int>());
    if (!doc["countdownFmt"].isNull())    s.countdownFmt  = doc["countdownFmt"].as<uint8_t>();
    if (!doc["countupFmt"].isNull())      s.countupFmt    = doc["countupFmt"].as<uint8_t>();
    if (!doc["clockFmt"].isNull())        s.clockFmt      = doc["clockFmt"].as<uint8_t>();
    if (!doc["justification"].isNull())   s.justification = doc["justification"].as<uint8_t>();
    if (!doc["brightness"].isNull())      s.brightness    = constrain(doc["brightness"].as<int>(), 0, 7);
    if (!doc["countdownDatetime"].isNull()) snprintf(s.countdownDatetime, sizeof(s.countdownDatetime), "%s", doc["countdownDatetime"].as<const char*>());
    if (!doc["countupDatetime"].isNull())    snprintf(s.countupDatetime,    sizeof(s.countupDatetime),    "%s", doc["countupDatetime"].as<const char*>());
    if (!doc["splashMessage"].isNull())   snprintf(s.splashMessage,   sizeof(s.splashMessage),   "%s", doc["splashMessage"].as<const char*>());
    if (!doc["finalMessage"].isNull())    snprintf(s.finalMessage,    sizeof(s.finalMessage),    "%s", doc["finalMessage"].as<const char*>());
    configManager.saveClockConfig(s);
    clockApplySettings(s);

    // ── WiFi settings (optional — reboot required to apply) ───────────────────
    bool wifiChanged = false;
    if (!doc["ssid"].isNull() || !doc["password"].isNull()) {
      WifiConfig cfg = configManager.loadWifiConfig();
      if (!doc["ssid"].isNull())     cfg.ssid     = doc["ssid"].as<String>();
      if (!doc["password"].isNull()) cfg.password = doc["password"].as<String>();
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

  // ── GET /api/config/raw ───────────────────────────────────────────────────────
  void handleApiConfigRaw() {
    logRequest(200);
    String raw = configManager.loadRaw();
    if (raw.isEmpty()) {
      server_.send(404, "text/plain", "config.json not found");
      return;
    }
    server_.send(200, "application/json", raw);
  }

  // ── POST /api/config/delete ───────────────────────────────────────────────────
  void handleApiConfigDelete() {
    logRequest(200);
    if (configManager.deleteConfig()) {
      server_.send(200, "application/json", "{\"message\":\"Deleted\"}");
    } else {
      server_.send(500, "application/json", "{\"error\":\"Delete failed\"}");
    }
  }

  // ── GET /api/wifi ─────────────────────────────────────────────────────────────
  void handleApiWifiGet() {
    logRequest(200);
    const WifiConfig cfg = configManager.loadWifiConfig();
    JsonDocument doc;
    doc["ssid"] = cfg.ssid;
    String json;
    serializeJson(doc, json);
    server_.send(200, "application/json", json);
  }

  // ── POST /api/wifi ────────────────────────────────────────────────────────────
  void handleApiWifiSave() {
    logRequest(200);
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, server_.arg("plain"));
    if (err) {
      server_.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
      return;
    }
    WifiConfig cfg = configManager.loadWifiConfig();
    if (!doc["ssid"].isNull())     cfg.ssid     = doc["ssid"].as<String>();
    if (!doc["password"].isNull()) cfg.password = doc["password"].as<String>();
    configManager.saveWifiConfig(cfg);
    server_.send(200, "application/json", "{\"message\":\"Saved \xe2\x80\x94 rebooting\xe2\x80\xa6\"}");
    pendingRebootMs_ = millis() + 1500;
  }

  void handleCaptiveRedirect() {
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

  void waitForSoftApIp() {
    while (WiFi.softAPIP() == IPAddress(0, 0, 0, 0)) delay(10);
  }

  void logRequest(int status) {
    Serial.printf("[HTTP] %s %s <- %s  => %d\n",
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
static void handleWifiRoute()            { portal.handleWifi(); }
static void handleUtilityRoute()         { portal.handleUtility(); }
static void handleDemoRoute()            { portal.handleDemo(); }
static void handleApiDemoTestRoute()     { portal.handleApiDemoTest(); }
static void handleApiTimeRoute()         { portal.handleApiTime(); }
static void handleApiFormatsRoute()      { portal.handleApiFormats(); }
static void handleApiGetConfigRoute()    { portal.handleApiGetConfig(); }
static void handleApiSaveConfigRoute()   { portal.handleApiSaveConfig(); }
static void handleApiConfigRawRoute()    { portal.handleApiConfigRaw(); }
static void handleApiConfigDeleteRoute() { portal.handleApiConfigDelete(); }
static void handleApiWifiGetRoute()      { portal.handleApiWifiGet(); }
static void handleApiWifiSaveRoute()     { portal.handleApiWifiSave(); }
static void handleCaptiveRedirectRoute() { portal.handleCaptiveRedirect(); }

void webBegin(const char *ssid, const char *password) { portal.begin(ssid, password); }
void webHandleClients()                               { portal.handleClients(); }
void networkGetInfo(String &ssid, String &ip)          { portal.getNetworkInfo(ssid, ip); }

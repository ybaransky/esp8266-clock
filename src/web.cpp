#include "web.h"

#include <Arduino.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include "hardware.h"
#include "rtc_ds3231.h"

namespace Routes {
  constexpr const char *ROOT = "/";
}

// Forward declarations for server route callbacks.
static void handleRootRoute();
static void handleCaptiveRedirectRoute();

class WebPortal {
public:
  WebPortal() : server_(80) {}

  void begin(const char *ssid, const char *password) {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(ssid, password);
    waitForSoftApIp();

    Serial.printf("AP \"%s\" started  IP: %s\n", ssid, WiFi.softAPIP().toString().c_str());

    dnsRunning_ = dnsServer_.start(53, "*", WiFi.softAPIP());
    if (!dnsRunning_) {
      Serial.println("[DNS] Failed to start captive DNS server (no socket available)");
    }

    server_.on(Routes::ROOT, handleRootRoute);
    server_.onNotFound(handleCaptiveRedirectRoute);
    server_.begin();
    Serial.println("HTTP server started");
  }

  void handleClients() {
    if (dnsRunning_) {
      dnsServer_.processNextRequest();
    }
    server_.handleClient();
  }

  void getNetworkInfo(String &ssid, String &ip) {
    ssid = WiFi.softAPSSID();
    ip = WiFi.softAPIP().toString();
  }

  void handleRoot() {
    logRequest(200);
    String ssid;
    String ip;
    getNetworkInfo(ssid, ip);

    const I2CScanResult scanResult = i2cBusScanner.lastResult();
    const RtcStatus rtcStatus = rtcGetStatus();

    String html;
    html.reserve(1400);
    html += F(R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Yurij Clock</title>
  <style>
    body { font-family: sans-serif; max-width: 560px; margin: 40px auto; padding: 0 16px; background: #fff; color: #111; }
    h1 { font-size: 1.5rem; margin-bottom: 0.5rem; }
    .subtitle { color: #666; margin-bottom: 1.2rem; }
    .card { background: #f4f4f4; border-radius: 10px; padding: 16px 20px; margin: 12px 0; }
    .label { color: #777; font-size: 0.8rem; text-transform: uppercase; letter-spacing: .05em; margin-bottom: 6px; }
    .value { font-size: 1.15rem; font-weight: 700; word-break: break-word; }
    .list { margin: 0; padding-left: 20px; }
    .muted { color: #666; }
  </style>
</head>
<body>
  <h1>Yurij Clock</h1>
  <div class="subtitle">Captive portal status</div>

  <div class="card">
    <div class="label">Network</div>
)rawliteral");
    html += "    <div class=\"value\">SSID: " + ssid + "</div>\n";
    html += "    <div class=\"value\">IP: " + ip + "</div>\n";
    html += F(R"rawliteral(
  </div>

  <div class="card">
    <div class="label">I2C scan</div>
)rawliteral");
    html += String("    <div class=\"value\">Devices found: ") + scanResult.count + "</div>\n";
    if (scanResult.count == 0) {
      html += F("    <div class=\"muted\">No I2C devices detected.</div>\n");
    } else {
      html += F("    <ul class=\"list\">\n");
      for (size_t index = 0; index < scanResult.count; ++index) {
        char addressBuf[8];
        snprintf(addressBuf, sizeof(addressBuf), "0x%02X", scanResult.addresses[index]);
        html += "      <li>" + String(addressBuf) + "</li>\n";
      }
      html += F("    </ul>\n");
    }
    html += F(R"rawliteral(
  </div>

  <div class="card">
    <div class="label">RTC status</div>
)rawliteral");
    html += String("    <div class=\"value\">Present: ") + (rtcStatus.present ? "yes" : "no") + "</div>\n";
    html += String("    <div class=\"value\">Power lost: ") + (rtcStatus.powerLost ? "yes" : "no") + "</div>\n";
    html += String("    <div class=\"value\">Low battery: ") + (rtcStatus.lowBattery ? "yes" : "no") + "</div>\n";
    html += String("    <div class=\"value\">SQW configured: ") + (rtcStatus.sqwConfigured ? "yes" : "no") + "</div>\n";
    if (!rtcStatus.error.isEmpty()) {
      html += "    <div class=\"muted\">Error: " + rtcStatus.error + "</div>\n";
    }
    html += F(R"rawliteral(
  </div>
</body>
</html>
)rawliteral");

    server_.send(200, "text/html", html);
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
    // softAP() returns before the interface is fully up; poll until we have a real IP.
    while (WiFi.softAPIP() == IPAddress(0, 0, 0, 0)) {
      delay(10);
    }
  }

  void logRequest(int status) {
    Serial.printf("[HTTP] %s %s <- %s  => %d\n",
                  methodName(server_.method()),
                  server_.uri().c_str(),
                  server_.client().remoteIP().toString().c_str(),
                  status);
  }

  ESP8266WebServer server_;
  DNSServer dnsServer_;
  bool dnsRunning_ = false;
};

static WebPortal portal;

static void handleRootRoute()            { portal.handleRoot(); }
static void handleCaptiveRedirectRoute() { portal.handleCaptiveRedirect(); }

void webBegin(const char *ssid, const char *password) { portal.begin(ssid, password); }
void webHandleClients()                               { portal.handleClients(); }
void networkGetInfo(String &ssid, String &ip)          { portal.getNetworkInfo(ssid, ip); }

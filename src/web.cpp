#include "web.h"

#include "html.h"
#include <Arduino.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>

namespace Routes {
  constexpr const char *ROOT = "/";
}

// forward refernece needed for static route handlers
static void handleRootRoute();
static void handleCaptiveRedirectRoute();

class WebPortal {
public:
  WebPortal() : server(80) {}

  void begin(const char *ssid, const char *password) {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(ssid, password);
    waitForSoftApIp();

    Serial.printf("AP \"%s\" started  IP: %s\n", ssid, WiFi.softAPIP().toString().c_str());

    dnsRunning = dnsServer.start(53, "*", WiFi.softAPIP());
    if (!dnsRunning) {
      Serial.println("[DNS] Failed to start captive DNS server (no socket available)");
    }

    registerRoutes();
    server.begin();
    Serial.println("HTTP server started");
  }

  void handleClients() {
    if (dnsRunning) {
      dnsServer.processNextRequest();
    }
    server.handleClient();
  }

  void getNetworkInfo(String &ssid, String &ip) {
    ssid = WiFi.softAPSSID();
    ip   = WiFi.softAPIP().toString();
  }

  void handleRoot() {
    logRequest(200);
    server.send(200, "text/html", INDEX_HTML);
  }

  void handleCaptiveRedirect() {
    logRequest(302);
    server.sendHeader("Location", "http://192.168.4.1/", true);
    server.send(302, "text/plain", "");
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

  void registerRoutes() {
    server.on(Routes::ROOT,          handleRootRoute);
    server.onNotFound(handleCaptiveRedirectRoute);
  }

  void logRequest(int status) {
    Serial.printf("[HTTP] %s %s <- %s  => %d\n",
                  methodName(server.method()),
                  server.uri().c_str(),
                  server.client().remoteIP().toString().c_str(),
                  status);
  }

  ESP8266WebServer server;
  DNSServer      dnsServer;
  bool           dnsRunning = false;
};

static WebPortal webPortal;

static void handleRootRoute() { webPortal.handleRoot(); }
static void handleCaptiveRedirectRoute() { webPortal.handleCaptiveRedirect(); }

void webBegin(const char *ssid, const char *password) {
  webPortal.begin(ssid, password);
}

void webHandleClients() {
  webPortal.handleClients();
}

void networkGetInfo(String &ssid, String &ip) {
  webPortal.getNetworkInfo(ssid, ip);
}

#include "web_server.h"

#include <Arduino.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>

#include "clock_controller.h"
#include "config_api.h"
#include "config_validation.h"
#include "display_format.h"
#include "file_api.h"
#include "html.h"
#include "http_responder.h"
#include "log.h"
#include "location_api.h"
#include "time_api.h"
#include "wifi_api.h"
#include "wifi_connection_manager.h"
#include "generated_web_pages.h"

namespace {

class WebPortal {
 public:
  WebPortal(ClockController& clockController,
            ConfigManager& configManager,
            WifiConnectionManager& wifiConnectionManager,
            RtcService& rtc)
      : server_(80),
        responder_(server_),
        configApi_(server_, responder_, clockController, configManager),
        timeApi_(server_, responder_, clockController, rtc),
        fileApi_(server_, responder_),
        locationApi_(server_, responder_),
        wifiApi_(server_, responder_, configManager, wifiConnectionManager),
        clockController_(clockController),
        configManager_(configManager),
        wifiConnectionManager_(wifiConnectionManager) {}

  void begin() {
    if (wifiConnectionManager_.status().mode == WifiMode::kAccessPoint) {
      dnsRunning_ = dnsServer_.start(53, "*", WiFi.softAPIP());
      if (!dnsRunning_) {
        LOG_PRINTLN("Failed to start captive DNS server (no socket available)");
      }
    }

    server_.on("/", HTTP_GET, []() { activePortal->handleRoot(); });
    server_.on("/mode", HTTP_POST, []() { activePortal->handleModeForm(); });
    server_.on("/demo", HTTP_POST, []() { activePortal->handleDemoForm(); });
    server_.on("/settings", HTTP_GET, []() { activePortal->sendGzip(GZIP_SETTINGS_HTML, GZIP_SETTINGS_HTML_SIZE); });
    server_.on("/config", HTTP_GET, []() { activePortal->sendGzip(GZIP_CONFIG_JSON_HTML, GZIP_CONFIG_JSON_HTML_SIZE); });
    server_.on("/format", HTTP_GET, []() { activePortal->handleFormatForm(); });
    server_.on("/format", HTTP_POST, []() { activePortal->handleFormatSave(); });
    server_.on("/time", HTTP_GET, []() { activePortal->sendGzip(GZIP_TIME_HTML, GZIP_TIME_HTML_SIZE); });
    server_.on("/sunset", HTTP_GET, []() { activePortal->sendGzip(GZIP_SUNSET_HTML, GZIP_SUNSET_HTML_SIZE); });
    server_.on("/messages", HTTP_GET, []() { activePortal->sendGzip(GZIP_MESSAGE_HTML, GZIP_MESSAGE_HTML_SIZE); });
    server_.on("/location", HTTP_GET, []() { activePortal->sendGzip(GZIP_LOCATION_HTML, GZIP_LOCATION_HTML_SIZE); });
    server_.on("/wifi", HTTP_GET, []() { activePortal->sendGzip(GZIP_WIFI_HTML, GZIP_WIFI_HTML_SIZE); });
    server_.on("/view", HTTP_GET, []() { activePortal->sendGzip(GZIP_VIEW_FILE_HTML, GZIP_VIEW_FILE_HTML_SIZE); });
    server_.on("/favicon.ico", HTTP_GET,
               []() { activePortal->sendProbe204("image/x-icon"); });

    // OS connectivity probes must receive their expected response. Redirecting
    // these to Home makes Android, Apple, and Windows repeatedly show a
    // "Sign in to network" prompt for the clock's local-only AP.
    server_.on("/generate_204", HTTP_GET,
               []() { activePortal->sendProbe204("text/plain"); });
    server_.on("/gen_204", HTTP_GET,
               []() { activePortal->sendProbe204("text/plain"); });
    server_.on("/hotspot-detect.html", HTTP_GET, []() {
      activePortal->sendProbeText(
          "<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>");
    });
    server_.on("/library/test/success.html", HTTP_GET, []() {
      activePortal->sendProbeText("Success");
    });
    server_.on("/ncsi.txt", HTTP_GET,
               []() { activePortal->sendProbeText("Microsoft NCSI"); });
    server_.on("/connecttest.txt", HTTP_GET, []() {
      activePortal->sendProbeText("Microsoft Connect Test");
    });

    server_.on("/api/client-log", HTTP_POST,
               []() { activePortal->handleClientLog(); });

    server_.on("/api/demo/test", HTTP_POST, []() { activePortal->configApi_.handleDemoTest(); });
    server_.on("/api/message/test", HTTP_POST, []() { activePortal->configApi_.handleMessageTest(); });
    server_.on("/api/mode", HTTP_POST, []() { activePortal->configApi_.handleSetMode(); });
    server_.on("/api/brightness", HTTP_POST, []() { activePortal->configApi_.handleBrightness(); });
    server_.on("/api/time", HTTP_GET, []() { activePortal->timeApi_.handleGetTime(); });
    server_.on("/api/time", HTTP_POST, []() { activePortal->timeApi_.handleTimeSync(); });
    server_.on("/api/formats", HTTP_GET, []() { activePortal->configApi_.handleFormats(); });
    server_.on("/api/config", HTTP_GET, []() { activePortal->configApi_.handleGetConfig(); });
    server_.on("/api/config", HTTP_POST, []() { activePortal->configApi_.handleSaveConfig(); });
    server_.on("/api/sunset", HTTP_POST,
               []() { activePortal->locationApi_.handleSunset(); });
    server_.on("/api/zipcode/lookup", HTTP_GET,
               []() { activePortal->locationApi_.handleZipcodeLookup(); });
    server_.on("/api/field-mismatch", HTTP_POST,
               []() { activePortal->configApi_.handleFieldMismatch(); });

    server_.on("/api/files", HTTP_GET, []() { activePortal->fileApi_.handleListFiles(); });
    server_.on("/api/file", HTTP_GET, []() { activePortal->fileApi_.handleReadFile(); });
    server_.on("/api/file", HTTP_DELETE, []() { activePortal->fileApi_.handleDeleteFile(); });
    server_.on("/api/file/upload", HTTP_POST,
               []() { activePortal->fileApi_.handleUpload(); },
               []() { activePortal->fileApi_.handleUploadData(); });

    server_.on("/api/wifi/status", HTTP_GET, []() { activePortal->wifiApi_.handleStatus(); });
    server_.on("/api/wifi/scan", HTTP_GET, []() { activePortal->wifiApi_.handleScan(); });
    server_.on("/api/wifi/connect", HTTP_POST, []() { activePortal->wifiApi_.handleConnect(); });

    server_.onNotFound([]() { activePortal->handleCaptiveRedirect(); });
    // Small responses on the AP link should not sit in Nagle's buffer waiting
    // for an ACK that phones can be slow to send.
    WiFiClient::setDefaultNoDelay(true);
    server_.begin();
    LOG_PRINTLN("HTTP server started");
  }

  void handleClients() {
    // A large gap between calls means the main loop stalled (e.g. display
    // writes); queued requests experience it as time-to-first-byte.
    const uint32_t entryMs = millis();
    if (lastHandleClientsMs_ != 0) {
      const uint32_t gap = entryMs - lastHandleClientsMs_;
      if (gap > maxLoopGapMs_) {
        maxLoopGapMs_ = gap;
      }
    }
    lastHandleClientsMs_ = entryMs;

    if (dnsRunning_) {
      dnsServer_.processNextRequest();
    }
    const uint32_t responseBefore = responder_.responseSequence();
    const uint32_t startedUs = micros();
    server_.handleClient();
    if (responder_.responseSequence() != responseBefore) {
      responder_.logCompletion(micros() - startedUs);
    }
    if (pendingRebootMs_ != 0 && static_cast<long>(millis() - pendingRebootMs_) >= 0) {
      LOG_PRINTLN("Rebooting...");
      ESP.restart();
    }
    logTrafficSummary();
  }

  // Every 10s, summarize how much captive-portal noise (probes/redirects) the
  // single-threaded server handled. Page loads competing with a probe storm
  // are a prime suspect for stalled or truncated transfers in AP mode.
  void logTrafficSummary() {
    const uint32_t nowMs = millis();
    if (nowMs - lastTrafficLogMs_ < 10000) {
      return;
    }
    const uint32_t total = responder_.responseSequence();
    if (total != lastTrafficTotal_ || maxLoopGapMs_ > 50) {
      LOG_PRINTF("web traffic: %lu responses (%lu probes, %lu redirects), "
                 "max loop gap %lu ms in last 10s\n",
                 static_cast<unsigned long>(total - lastTrafficTotal_),
                 static_cast<unsigned long>(probeCount_ - lastProbeCount_),
                 static_cast<unsigned long>(redirectCount_ - lastRedirectCount_),
                 static_cast<unsigned long>(maxLoopGapMs_));
    }
    lastTrafficLogMs_ = nowMs;
    lastTrafficTotal_ = total;
    lastProbeCount_ = probeCount_;
    lastRedirectCount_ = redirectCount_;
    maxLoopGapMs_ = 0;
  }

  void sendProbe204(const char* contentType) {
    ++probeCount_;
    responder_.send(204, contentType, "");
  }

  void sendProbeText(const char* body) {
    ++probeCount_;
    responder_.sendText(200, body);
  }

  // Receives error beacons from page JavaScript (window.onerror and failed
  // /api/ fetches) so browser-side failures land in the serial timeline next
  // to the server-side request logs.
  void handleClientLog() {
    String body = server_.arg("plain");
    if (body.length() > 160) {
      body.remove(160);
    }
    for (size_t i = 0; i < body.length(); ++i) {
      const char c = body[i];
      if (c < 32 || c > 126) {
        body.setCharAt(i, '.');
      }
    }
    LOG_PRINTF("CLIENT %s: %s\n",
               server_.client().remoteIP().toString().c_str(), body.c_str());
    responder_.send(204, "text/plain", "");
  }

  void getNetworkInfo(String& ssid, String& ip) {
    const WifiRuntimeStatus status = wifiConnectionManager_.status();
    if (status.mode == WifiMode::kStation && status.connected) {
      ssid = status.ssid;
      ip = status.ip;
      return;
    }
    ssid = status.apSsid;
    ip = status.apIp;
  }

  void scheduleReboot(uint32_t delayMs) {
    pendingRebootMs_ = millis() + delayMs;
  }

  void handleCaptiveRedirect() {
    if (wifiConnectionManager_.status().mode != WifiMode::kAccessPoint) {
      responder_.sendText(404, "Not found");
      return;
    }

    ++redirectCount_;
    responder_.logRequest(302, 0);
    server_.sendHeader("Location", "http://192.168.4.1/", true);
    server_.send(302, "text/plain", "");
  }

  static WebPortal* activePortal;

 private:
  static String htmlEscape(const String& value) {
    String escaped = value;
    escaped.replace("&", "&amp;"); escaped.replace("<", "&lt;");
    escaped.replace(">", "&gt;"); escaped.replace("\"", "&quot;");
    return escaped;
  }

  void sendDynamicHtml(const String& page) {
    // A String append that fails to reallocate is silently dropped on the
    // ESP8266, producing a shorter page with a matching Content-Length: the
    // browser renders a cut-off page and nothing errors. Detect it here.
    if (!page.endsWith(F("</html>"))) {
      LOG_PRINTF("ERROR dynamic page truncated during build: len=%u heap=%u maxblk=%u\n",
                 page.length(), ESP.getFreeHeap(), ESP.getMaxFreeBlockSize());
    }
    responder_.logRequest(200, page.length());
    server_.sendHeader("Cache-Control", "private, no-cache");
    server_.send(200, "text/html", page);
  }

  void redirectTo(const char* path) {
    responder_.logRequest(303, 0);
    server_.sendHeader("Location", path, true);
    server_.send(303, "text/plain", "");
  }

  void handleRoot() {
    const char* activeMode = modeName(clockController_.activeMode());
    String ssid, ip; getNetworkInfo(ssid, ip);
    String page;
    if (!page.reserve(2400)) {
      LOG_PRINTF("ERROR home page reserve failed: heap=%u maxblk=%u\n",
                 ESP.getFreeHeap(), ESP.getMaxFreeBlockSize());
    }
    page += F("<!doctype html><html><head><meta name=viewport content='width=device-width,initial-scale=1'><title>");
    page += htmlEscape(ssid.isEmpty() ? String("Clock") : ssid);
    page += F("</title><script>function clog(d){d.page=location.pathname;try{navigator.sendBeacon('/api/client-log',JSON.stringify(d))}catch(e){}}window.onerror=function(m,s,l){clog({err:String(m).slice(0,120),line:l||0})};addEventListener('load',function(){var n=performance.getEntriesByType('navigation')[0];if(n&&n.loadEventStart>3000)clog({slow:1,conn:Math.round(n.connectEnd-n.connectStart),ttfb:Math.round(n.responseStart-n.requestStart),dl:Math.round(n.responseEnd-n.responseStart),load:Math.round(n.loadEventStart)})})</script><style>body{font-family:sans-serif;text-align:center;padding:20px;background:#111;color:#eee;max-width:520px;margin:auto}form{margin:10px 0}button,a{display:block;width:100%;padding:16px;font-size:1.3em;background:#397;color:white;border:0;border-radius:8px;box-sizing:border-box;text-decoration:none}button.current{animation:b 1s linear infinite}@keyframes b{50%{color:transparent}}hr{border:0;border-top:1px solid #333;margin:18px 0}.settings{max-width:240px;margin:auto;background:#357}</style></head><body><h1>");
    page += htmlEscape(ssid.isEmpty() ? String("Clock") : ssid); page += F("</h1>");
    const char* modes[] = {"countdown", "clock", "countup", "friday"};
    const char* labels[] = {"Countdown", "Clock", "Countup", "Friday"};
    for (uint8_t i = 0; i < 4; ++i) {
      page += F("<form method=post action=/mode><button name=mode value='"); page += modes[i];
      page += strcmp(activeMode, modes[i]) == 0 ? F("' class=current>") : F("'>");
      page += labels[i]; page += F("</button></form>");
    }
    page += F("<hr><form method=post action=/demo><button>Demo</button></form><hr><a class=settings href=/settings>Settings</a><div id=t style='text-align:right;color:#444;margin-top:12px'></div><script>addEventListener('load',function(){t.textContent=(performance.now()/1000).toFixed(2)})</script></body></html>");
    sendDynamicHtml(page);
  }

  void handleModeForm() {
    Mode mode;
    if (!modeFromName(server_.arg("mode"), &mode)) { responder_.sendText(400, "Invalid mode"); return; }
    ClockConfig config = configManager_.loadClockConfig(); config.activeMode = mode;
    if (!configManager_.saveClockConfig(config)) { responder_.sendText(500, "Configuration write failed"); return; }
    clockController_.applyConfig(config); redirectTo("/");
  }

  void handleDemoForm() { clockController_.showDemo(); redirectTo("/"); }

  void appendFormatSelect(String& page, const char* name, FormatGroup group, uint8_t selected) {
    page += F("<label>"); page += name; page += F("<select name='"); page += name; page += F("'>");
    for (uint8_t i = 0; i < displayFormatCount(group); ++i) {
      page += F("<option value='"); page += i; page += i == selected ? F("' selected>") : F("'>");
      page += displayFormatInfo(group, i).label; page += F("</option>");
    }
    page += F("</select></label>");
  }

  void handleFormatForm() {
    const ClockConfig c = configManager_.loadClockConfig(); String page;
    if (!page.reserve(7500)) {
      LOG_PRINTF("ERROR format page reserve failed: heap=%u maxblk=%u\n",
                 ESP.getFreeHeap(), ESP.getMaxFreeBlockSize());
    }
    page += F("<!doctype html><html><head><meta name=viewport content='width=device-width,initial-scale=1'><title>Formats</title><script>function clog(d){d.page=location.pathname;try{navigator.sendBeacon('/api/client-log',JSON.stringify(d))}catch(e){}}window.onerror=function(m,s,l){clog({err:String(m).slice(0,120),line:l||0})};addEventListener('load',function(){var n=performance.getEntriesByType('navigation')[0];if(n&&n.loadEventStart>3000)clog({slow:1,conn:Math.round(n.connectEnd-n.connectStart),ttfb:Math.round(n.responseStart-n.requestStart),dl:Math.round(n.responseEnd-n.responseStart),load:Math.round(n.loadEventStart)})})</script><style>body{font-family:sans-serif;padding:16px;background:#111;color:#eee;max-width:540px;margin:auto;color-scheme:dark}label{display:block;margin:12px 0}select,input{width:100%;padding:9px;box-sizing:border-box;background:#202020;color:#eee;border:1px solid #555;border-radius:5px}input[type=checkbox]{width:auto}fieldset{border:1px solid #444;margin:14px 0}button{padding:12px 24px;background:#397;color:white;border:0;border-radius:7px}a{color:#8cf}</style></head><body><h1>Format Settings</h1><form method=post action=/format><label>Mode<select id=mode name=mode onchange=showMode()>");
    const char* names[] = {"countdown","countup","clock","friday"};
    for (uint8_t i=0;i<4;++i){page+=F("<option value='");page+=names[i];page+=c.activeMode==static_cast<Mode>(i)?F("' selected>"):F("'>");page+=names[i];page+=F("</option>");}
    page += F("</select></label><fieldset id=f0><legend>Countdown</legend>"); appendFormatSelect(page,"countdownFormat",kFmtGroupCountdown,c.countdown.format);
    page += F("<label>End<input type=datetime-local step=1 name=countdownEnd value='"); String end(c.countdown.end); end.replace(" ","T"); page+=end; page+=F("'></label></fieldset><fieldset id=f1><legend>Countup</legend>"); appendFormatSelect(page,"countupFormat",kFmtGroupCountUp,c.countup.format);
    page += F("<label>Start<input type=datetime-local step=1 name=countupStart value='"); String start(c.countup.start); start.replace(" ","T"); page+=start; page+=F("'></label></fieldset><fieldset id=f2><legend>Clock</legend>"); appendFormatSelect(page,"clockFormat",kFmtGroupClock,c.display.clockFmt);
    page += F("<label><input type=checkbox name=hour12 value=1"); if(c.display.clockUse12Hour)page+=F(" checked"); page+=F("> 12-hour clock</label></fieldset><fieldset id=f3><legend>Friday</legend>"); appendFormatSelect(page,"fridayClock",kFmtGroupClock,c.friday.clockFmt); appendFormatSelect(page,"fridayTo",kFmtGroupCountdown,c.friday.toFridaySunsetFmt); appendFormatSelect(page,"saturdayTo",kFmtGroupCountdown,c.friday.toSaturdaySunsetFmt);
    page += F("</fieldset><label>Brightness<input type=range min=0 max=7 name=brightness value='");page+=c.display.brightness;page+=F("'></label><button type=submit>Save</button></form><p><a href=/ >Home</a> &nbsp; <a href=/settings>Settings</a><span id=t style='float:right;color:#444'></span></p><script>function showMode(){var m=document.getElementById('mode').selectedIndex;for(var i=0;i<4;i++)document.getElementById('f'+i).hidden=i!=m}showMode();var bt;document.getElementsByName('brightness')[0].oninput=function(){var v=+this.value;clearTimeout(bt);bt=setTimeout(function(){fetch('/api/brightness',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({brightness:v})})},120)};addEventListener('load',function(){document.getElementById('t').textContent=(performance.now()/1000).toFixed(2)})</script></body></html>"); sendDynamicHtml(page);
  }

  void handleFormatSave() {
    ClockConfig c=configManager_.loadClockConfig(); Mode m;if(modeFromName(server_.arg("mode"),&m))c.activeMode=m;
    c.countdown.format=server_.arg("countdownFormat").toInt();c.countup.format=server_.arg("countupFormat").toInt();c.display.clockFmt=server_.arg("clockFormat").toInt();c.friday.clockFmt=server_.arg("fridayClock").toInt();c.friday.toFridaySunsetFmt=server_.arg("fridayTo").toInt();c.friday.toSaturdaySunsetFmt=server_.arg("saturdayTo").toInt();c.display.brightness=server_.arg("brightness").toInt();c.display.clockUse12Hour=server_.hasArg("hour12");
    String value=server_.arg("countdownEnd");if(!value.isEmpty()){value.replace("T"," ");snprintf(c.countdown.end,sizeof(c.countdown.end),"%s",value.c_str());}value=server_.arg("countupStart");if(!value.isEmpty()){value.replace("T"," ");snprintf(c.countup.start,sizeof(c.countup.start),"%s",value.c_str());}
    c=configManager_.sanitizeClockConfig(c);if(!configManager_.saveClockConfig(c)){responder_.sendText(500,"Configuration write failed");return;}clockController_.applyConfig(c);redirectTo("/format");
  }

  void sendGzip(const uint8_t* html, size_t length) {
    responder_.sendGzipProgmem(200, "text/html", html, length);
  }

  ESP8266WebServer server_;        // HTTP server on port 80.
  HttpResponder responder_;        // Shared response helper.
  ConfigApi configApi_;            // Display/configuration API endpoints.
  TimeApi timeApi_;                // RTC read and synchronization endpoints.
  FileApi fileApi_;                // LittleFS file-management endpoints.
  LocationApi locationApi_;        // Location and ZIP-code endpoints.
  WifiApi wifiApi_;                // WiFi status/scan/connect endpoints.
  ClockController& clockController_; // Live state; avoids a LittleFS read on `/`.
  ConfigManager& configManager_;
  WifiConnectionManager& wifiConnectionManager_;
  DNSServer dnsServer_;            // Captive portal DNS responder.
  bool dnsRunning_ = false;        // True when captive DNS started successfully.
  uint32_t pendingRebootMs_ = 0;   // millis() deadline for deferred reboot.
  uint32_t probeCount_ = 0;        // OS connectivity-probe responses served.
  uint32_t redirectCount_ = 0;     // Captive-portal 302 redirects served.
  uint32_t lastTrafficLogMs_ = 0;  // Last traffic-summary log time.
  uint32_t lastTrafficTotal_ = 0;  // responseSequence() at last summary.
  uint32_t lastProbeCount_ = 0;    // probeCount_ at last summary.
  uint32_t lastRedirectCount_ = 0; // redirectCount_ at last summary.
  uint32_t lastHandleClientsMs_ = 0;  // Previous handleClients() entry time.
  uint32_t maxLoopGapMs_ = 0;      // Largest gap between calls this period.

};

WebPortal* WebPortal::activePortal = nullptr;

}  // namespace

void webBegin(ClockController& clockController,
              ConfigManager& configManager,
              WifiConnectionManager& wifiConnectionManager,
              RtcService& rtc) {
  static WebPortal portal(clockController, configManager, wifiConnectionManager, rtc);
  WebPortal::activePortal = &portal;
  portal.begin();
}

void webHandleClients() {
  WebPortal::activePortal->handleClients();
}

void networkGetInfo(String& ssid, String& ip) {
  WebPortal::activePortal->getNetworkInfo(ssid, ip);
}

void webScheduleReboot(uint32_t delayMs) {
  WebPortal::activePortal->scheduleReboot(delayMs);
}

# WiFi Connection Management Design

## Goal

Add a WiFi connection management system similar in spirit to tzapu WiFiManager:

- On boot, try to connect to a saved station network.
- If station connection fails, start the device's own access point.
- Serve the same web application in both station mode and access point mode.
- Keep the WiFi setup page available from the normal main page, not as the main page itself.

## Core Principle

Separate WiFi connection state from HTTP page serving.

```text
ConfigManager            stores WiFi credentials
WifiConnectionManager    chooses station mode or access point mode
WebPortal                serves the same UI/API in either mode
main.cpp                 wires startup together
```

`WebPortal` should not decide whether the device is in station mode or access point mode. It should ask the WiFi manager for status.

`WifiConnectionManager` should not serve HTML.

`ConfigManager` should not connect to WiFi.

## New Module

Add:

```text
src/wifi_connection_manager.h
src/wifi_connection_manager.cpp
```

Suggested API:

```cpp
enum class WifiMode {
  kStation,
  kAccessPoint,
};

struct WifiRuntimeStatus {
  WifiMode mode;
  bool connected;
  String ssid;
  String ip;
  String apSsid;
  String apIp;
};

class WifiConnectionManager {
 public:
  void begin(const WifiConfig& config);
  void tick();

  WifiRuntimeStatus status() const;
  void scanNetworks(JsonDocument& doc);
  bool connectAndSave(const String& ssid, const String& password);

 private:
  bool tryStationConnect(const String& ssid, const String& password);
  void startAccessPoint();
};

extern WifiConnectionManager wifiConnectionManager;
```

## Config Shape

Current `WifiConfig` should be split so station credentials and fallback AP credentials are distinct:

```cpp
struct WifiConfig {
  String staSsid;
  String staPassword;

  String apSsid;
  String apPassword;
};
```

Defaults:

```text
staSsid = ""
staPassword = ""
apSsid = "YuriClock"
apPassword = "12345678"
```

## Boot Flow

`main.cpp` starts WiFi before the web server:

```cpp
WifiConfig wifiConfig = configManager.loadWifiConfig();

wifiConnectionManager.begin(wifiConfig);
webBegin();

buttonBegin();
```

`webBegin()` does not call `WiFi.mode(WIFI_AP)` or `WiFi.softAP(...)`; that responsibility belongs to `WifiConnectionManager`.

## WiFi Startup Behavior

On boot:

1. Load `WifiConfig`.
2. If `staSsid` is present, try to connect in station mode.
3. If station connection succeeds, stay in station mode.
4. If station connection fails, start AP mode.
5. Start the HTTP server either way.

The HTTP routes are identical in both modes.

## Web Server Routes

The web server should serve the same pages regardless of WiFi mode:

```text
GET  /
GET  /config
GET  /format
GET  /time-sync
GET  /wifi
GET  /demo

GET  /api/config
POST /api/config

GET  /api/wifi/status
GET  /api/wifi/scan
POST /api/wifi/connect
```

The `/wifi` page should be reachable from the main page navigation, but `/` remains the normal main page.

In AP mode, captive DNS can redirect unknown hosts to `/`, but the root page should still be the normal application page.

## WiFi Page Behavior

The `/wifi` page should show:

- Current mode: station or access point
- Current IP
- Connected SSID when in station mode
- Available networks
- Password input
- Connect button

AP mode example:

```text
Mode: Access Point
IP: 192.168.4.1
Not connected to WiFi
```

Station mode example:

```text
Mode: Station
SSID: HomeNetwork
IP: 192.168.1.42
```

## API Design

### GET /api/wifi/status

Returns current WiFi runtime state:

```json
{
  "mode": "station",
  "connected": true,
  "ssid": "HomeNetwork",
  "ip": "192.168.1.42",
  "apSsid": "YuriClock",
  "apIp": ""
}
```

### GET /api/wifi/scan

Returns visible networks:

```json
{
  "networks": [
    { "ssid": "HomeNetwork", "rssi": -50, "secure": true },
    { "ssid": "Guest", "rssi": -70, "secure": false }
  ]
}
```

### POST /api/wifi/connect

Accepts:

```json
{
  "ssid": "HomeNetwork",
  "password": "secret"
}
```

Recommended first implementation:

1. Save the submitted station credentials.
2. Return a success response with `reboot: true`.
3. Reboot after a short delay.
4. Let normal boot attempt the station connection.

This is simpler and more reliable on ESP8266 than trying to fully switch modes live.

## Captive DNS

Captive DNS should only be active in access point mode.

When active, unknown hostnames can redirect to:

```text
http://192.168.4.1/
```

But the page served at `/` should still be the same normal app home page served in station mode.

## Current State

Network startup is owned by `WifiConnectionManager`. `webBegin()` starts the routes/server and captive DNS only when the device is running as an access point. `networkGetInfo(String& ssid, String& ip)` returns the active runtime network info from `WifiConnectionManager`.

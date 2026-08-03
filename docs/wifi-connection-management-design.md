# WiFi Connection Management Design

Status: implemented current design.

## Ownership

- `ConfigManager` persists station and fallback access-point credentials.
- `WifiConnectionManager` owns STA connection attempts, AP fallback, scanning,
  runtime network status, and deferred AP-client events.
- `WebPortal` serves the same pages and APIs in either WiFi mode.
- `WifiApi` exposes status, scans, and connect/save operations.

The WiFi manager does not serve HTML, and the web portal does not choose the
connection mode.

## Startup

`WifiConnectionManager::begin()` attempts the saved station network when an
SSID is configured. If it cannot connect within the configured attempt window,
it starts the fallback access point. Captive DNS is active only in AP mode.

## Runtime interface

`status()` returns `WifiRuntimeStatus`, including mode, connection state,
station SSID/IP, and AP SSID/IP. `scanNetworks()` fills a JSON document for the
WiFi page. `connectAndSave()` joins a selected station and persists credentials
through `ConfigManager`.

## Loop and callbacks

SDK AP-client callbacks only capture event data. `tick()` performs deferred
logging and DHCP lookup from normal loop context. `ClockApplication::tick()`
calls the WiFi manager and then services the web portal every iteration.

## AP radio settings

The 11g PHY mode, channel survey, and 17 dBm transmit power in
`wifi_connection_manager.cpp` are evidence-backed settings for reliable phone
transfers. Do not change them without new on-device evidence. WiFi performance
must be evaluated on the clock's own power supply away from the development
PC's nearby 2.4 GHz radios.

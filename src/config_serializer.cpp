#include "config_serializer.h"

#include <ArduinoJson.h>

#include "config.h"
#include "config_validation.h"

void serializeClockConfig(JsonDocument& doc, const ClockConfig& clock) {
  JsonObject display = doc["display"].to<JsonObject>();
  display["activeMode"]  = modeName(clock.activeMode);
  display["brightness"]  = clock.brightness;
  display["clock12Hour"] = clock.clockUse12Hour;

  JsonObject messages = display["messages"].to<JsonObject>();
  messages["splash"] = String(clock.splashMessage);
  messages["final"]  = String(clock.finalMessage);

  JsonObject modes = display["modes"].to<JsonObject>();

  JsonObject countdown = modes["countdown"].to<JsonObject>();
  countdown["format"] = clock.countdownFmt;
  countdown["end"]    = String(clock.countdownDatetime);

  JsonObject countup = modes["countup"].to<JsonObject>();
  countup["format"] = clock.countupFmt;
  countup["start"]  = String(clock.countupDatetime);

  JsonObject clockMode = modes["clock"].to<JsonObject>();
  clockMode["format"] = clock.clockFmt;

  JsonObject friday = modes["friday"].to<JsonObject>();
  friday["clockFormat"]            = clock.fridayClockFmt;
  friday["toFridaySunsetFormat"]   = clock.fridayToFridaySunsetFmt;
  friday["toSaturdaySunsetFormat"] = clock.fridayToSatSunsetFmt;

  JsonObject timezone = doc["time"]["timezone"].to<JsonObject>();
  timezone["name"]           = String(clock.timezone);
  timezone["utcOffsetMinutes"] = clock.utcOffsetMinutes;
  doc["time"]["dst"]         = clock.dst;

  JsonObject location = doc["location"].to<JsonObject>();
  location["zipcode"]   = String(clock.location.zipcode);
  location["latitude"]  = clock.location.latitude;
  location["longitude"] = clock.location.longitude;

  JsonObject sunset = doc["sunset"].to<JsonObject>();
  sunset["zipcode"]   = String(clock.sunsetTest.zipcode);
  sunset["latitude"]  = clock.sunsetTest.latitude;
  sunset["longitude"] = clock.sunsetTest.longitude;
}

void serializeWifiConfig(JsonDocument& doc, const WifiConfig& wifi) {
  JsonObject wifiDoc = doc["wifi"].to<JsonObject>();

  JsonObject station = wifiDoc["station"].to<JsonObject>();
  station["ssid"]     = wifi.staSsid;
  station["password"] = wifi.staPassword;

  JsonObject accessPoint = wifiDoc["accessPoint"].to<JsonObject>();
  accessPoint["ssid"]     = wifi.apSsid;
  accessPoint["password"] = wifi.apPassword;
}

void serializeWifiStatus(JsonDocument& doc, const WifiConfig& wifi) {
  JsonObject wifiDoc = doc["wifi"].to<JsonObject>();

  JsonObject station = wifiDoc["station"].to<JsonObject>();
  station["ssid"] = wifi.staSsid;
  // station password intentionally omitted from HTTP responses

  JsonObject accessPoint = wifiDoc["accessPoint"].to<JsonObject>();
  accessPoint["ssid"]     = wifi.apSsid;
  accessPoint["password"] = wifi.apPassword;
}

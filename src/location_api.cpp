#include "location_api.h"

#include "log.h"
#include "zipcode.h"

bool LocationApi::parseJsonBody(JsonDocument& doc, const char* route) {
  const DeserializationError error = deserializeJson(doc, server_.arg("plain"));
  if (!error) return true;

  LOG_PRINTF("%s failed: invalid JSON: %s\n", route, error.c_str());
  responder_.sendJson(400, "{\"error\":\"Invalid JSON\"}");
  return false;
}

void LocationApi::handleZipcodeLookup() {
  const String zipcode = server_.arg("zip");
  LOG_PRINTF("/api/zipcode/lookup requested: zip=\"%s\"\n", zipcode.c_str());
  if (!isValidZipcode(zipcode.c_str())) {
    LOG_PRINTF("/api/zipcode/lookup failed: invalid zipcode=\"%s\"\n",
               zipcode.c_str());
    responder_.sendJson(400, "{\"error\":\"ZIP code must be 5 digits\"}");
    return;
  }

  ZipcodeLocation location;
  if (!zipcodeLookupLocation(zipcode.c_str(), &location)) {
    LOG_PRINTF("/api/zipcode/lookup failed: zip not found or unreadable: \"%s\"\n",
               zipcode.c_str());
    responder_.sendJson(404, "{\"error\":\"ZIP code not found\"}");
    return;
  }

  LOG_PRINTF("/api/zipcode/lookup success: zip=\"%s\" lat=%.6f lon=%.6f\n",
             location.zipcode, location.latitude, location.longitude);
  char json[96];
  snprintf(json, sizeof(json),
           "{\"zipcode\":\"%s\",\"latitude\":%.6f,\"longitude\":%.6f}",
           location.zipcode, location.latitude, location.longitude);
  responder_.sendJson(200, json);
}

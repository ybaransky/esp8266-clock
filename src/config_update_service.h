#pragma once

#include <ArduinoJson.h>

struct SaveConfigResult {
  bool ok;
  bool wifiChanged;
  const char* errorJson;
};

class ConfigUpdateService {
 public:
  SaveConfigResult applySavePayload(const JsonDocument& doc);
};

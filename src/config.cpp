#include "config.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "config_serializer.h"
#include "config_validation.h"
#include "datetime_validation.h"
#include "defaults.h"
#include "log.h"
#include "storage_manager.h"

static constexpr const char* kConfigPath = "/config.json";      // Committed configuration.
static constexpr const char* kConfigTempPath = "/config.tmp";   // Save in progress, verified before install.
static constexpr const char* kConfigBackupPath = "/config.bak";  // Previous primary, kept until a save completes.

// -----------------------------------------------------------------------------
// ConfigManager
// -----------------------------------------------------------------------------

// -- WiFi ----------------------------------------------------------------------
bool ConfigManager::ensureLoaded() {
    if (loaded_) return true;

    DeviceConfig next;
    initDefaultClockConfig(next.clock);
    next.wifi = defaultWifiConfig();
    if (!readAll(next)) {
        current_ = next;
        loaded_ = true;
        return false;
    }
    current_ = next;
    loaded_ = true;
    return true;
}

bool ConfigManager::readAll(DeviceConfig& config) {
    const uint32_t startedUs = micros();
    if (!storageManager.ensureMounted("read complete config")) return false;
    JsonDocument doc;
    size_t bytes = 0;
    bool loaded = false;
    const bool hadFile = STORAGE.exists(kConfigPath) || STORAGE.exists(kConfigBackupPath);
    const char* candidates[] = {kConfigPath, kConfigBackupPath};
    for (const char* path : candidates) {
        File file = STORAGE.open(path, "r");
        if (!file) continue;
        bytes = file.size();
        doc.clear();
        const DeserializationError error = deserializeJson(doc, file);
        file.close();
        if (error || !doc.is<JsonObject>()) {
            LOG_PRINTF("Config read failed: %s (%s)", path,
                       error ? error.c_str() : "expected JSON object");
            continue;
        }
        loaded = true;
        if (path == kConfigBackupPath) {
            // Keep the backup intact if restoration fails; it remains the
            // recovery source on the next boot and throughout the next save.
            if ((STORAGE.exists(kConfigPath) && !STORAGE.remove(kConfigPath)) ||
                !STORAGE.rename(kConfigBackupPath, kConfigPath)) {
                LOG_PRINTLN("Config backup loaded; file restoration deferred");
            } else {
                LOG_PRINTLN("Config restored from backup");
            }
        } else {
            STORAGE.remove(kConfigBackupPath);  // A valid primary wins after a completed save.
        }
        break;
    }
    if (!loaded) {
        if (hadFile) {
            LOG_PRINTLN("No readable config or backup; using memory defaults, preserving files");
            return false;
        }
        LOG_PRINTLN("No config found; creating defaults");
        return writeAll(config, "create default config");
    }
    const char* validationError =
        applyJsonToClockConfig(doc.as<JsonVariantConst>(), config.clock);
    applyJsonToWifiConfig(doc.as<JsonVariantConst>(), config.wifi);
    if (validationError != nullptr) {
        LOG_PRINTF("Complete config has invalid values: %s", validationError);
    }
    sanitizeClockConfig(config.clock);
    sanitizeWifiConfig(config.wifi);
    LOG_PRINTF("Complete config read: bytes=%u time=%.2f ms",
               static_cast<unsigned>(bytes),
               (micros() - startedUs) / 1000.0f);
    return true;
}

// Serializes config to the temp file. The JsonDocument is scoped to this
// function so its pool is released before verifyTemp() builds its own -
// holding both at once roughly doubled the peak heap for a save.
bool ConfigManager::serializeToTemp(const DeviceConfig& config,
                                    const char* context, size_t& bytes) {
    JsonDocument doc;
    // Callers guarantee config.clock is sanitized (defaults, loaded config,
    // or a save path that sanitized in place) - no extra copy at this depth.
    // config outlives doc, which serializeClockConfig() requires.
    serializeClockConfig(doc, config.clock);
    serializeWifiConfig(doc, config.wifi);

    STORAGE.remove(kConfigTempPath);
    File file = STORAGE.open(kConfigTempPath, "w");
    if (!file) {
        LOG_PRINTF("Complete config write failed: cannot open temp file context=%s", context);
        return false;
    }
    bytes = serializeJson(doc, file);
    file.flush();
    file.close();
    if (bytes == 0) {
        STORAGE.remove(kConfigTempPath);
        LOG_PRINTF("Complete config write failed: serialization context=%s", context);
        return false;
    }
    return true;
}

// Reads the temp file back and confirms it parses, so a truncated or corrupt
// write is never installed over a good config.
bool ConfigManager::verifyTemp(const char* context) {
    File verifyFile = STORAGE.open(kConfigTempPath, "r");
    JsonDocument verifyDoc;
    const DeserializationError verifyError = deserializeJson(verifyDoc, verifyFile);
    verifyFile.close();
    if (verifyError) {
        STORAGE.remove(kConfigTempPath);
        LOG_PRINTF("Complete config verification failed: %s context=%s",
                   verifyError.c_str(), context);
        return false;
    }
    return true;
}

// Moves the verified temp file into place, keeping a recoverable copy of the
// previous config until the replacement is installed.
bool ConfigManager::installVerifiedTemp(const char* context) {
    const bool hadOriginal = STORAGE.exists(kConfigPath);
    const bool hadBackup = STORAGE.exists(kConfigBackupPath);
    // A surviving backup may be our only good copy after a failed recovery.
    // Never discard it before the verified replacement is installed.
    if (hadOriginal) {
        const bool preserved = hadBackup ? STORAGE.remove(kConfigPath)
                                         : STORAGE.rename(kConfigPath, kConfigBackupPath);
        if (!preserved) {
            STORAGE.remove(kConfigTempPath);
            LOG_PRINTF("Config write failed: cannot preserve original context=%s", context);
            return false;
        }
    }
    if (!STORAGE.rename(kConfigTempPath, kConfigPath)) {
        if (hadOriginal || hadBackup) STORAGE.rename(kConfigBackupPath, kConfigPath);
        STORAGE.remove(kConfigTempPath);
        LOG_PRINTF("Complete config write failed: cannot install temp file context=%s", context);
        return false;
    }
    STORAGE.remove(kConfigBackupPath);
    return true;
}

bool ConfigManager::writeAll(const DeviceConfig& config, const char* context) {
    const uint32_t startedUs = micros();
    if (!storageManager.ensureMounted(context)) return false;

    size_t bytes = 0;
    if (!serializeToTemp(config, context, bytes)) return false;
    if (!verifyTemp(context)) return false;
    if (!installVerifiedTemp(context)) return false;

    const uint32_t elapsedMs = (micros() - startedUs + 500U) / 1000U;
    LOG_PRINTF("Complete config write: bytes=%u time=%lu ms context=%s",
               static_cast<unsigned>(bytes),
               static_cast<unsigned long>(elapsedMs), context);
    return true;
}

const WifiConfig& ConfigManager::wifiConfig() {
    ensureLoaded();
    return current_.wifi;
}

bool ConfigManager::saveWifiConfig(const WifiConfig& config) {
    ensureLoaded();
    DeviceConfig next = current_;
    next.wifi = config;
    sanitizeWifiConfig(next.wifi);
    if (!writeAll(next, "save WiFi config")) return false;
    current_ = next;
    return true;
}

const ClockConfig& ConfigManager::clockConfig() {
    ensureLoaded();
    return current_.clock;
}

bool ConfigManager::saveClockConfig(ClockConfig& config) {
    ensureLoaded();
    sanitizeClockConfig(config);
    DeviceConfig next = current_;
    next.clock = config;
    if (!writeAll(next, "save clock config")) return false;
    current_ = next;
    return true;
}

bool ConfigManager::saveConfig(ClockConfig& clock, const WifiConfig& wifi) {
    ensureLoaded();
    sanitizeClockConfig(clock);
    DeviceConfig next{clock, wifi};
    sanitizeWifiConfig(next.wifi);
    if (!writeAll(next, "save complete config")) return false;
    current_ = next;
    return true;
}

void ConfigManager::sanitizeClockConfig(ClockConfig& config) const {
    // Reads individual defaults rather than building a whole default
    // ClockConfig: this runs on every save, on the 4KB cont stack.
    config.activeMode = sanitizeMode(static_cast<int>(config.activeMode), defaultActiveMode());
    DateTime parsed;
    if (!parseLocalDateTime(config.countdown.end, parsed)) {
        strlcpy(config.countdown.end, defaultCountdownEnd(), sizeof(config.countdown.end));
    }
    if ((strcmp(config.countup.start, kCountupStartNow) != 0) &&
        !parseLocalDateTime(config.countup.start, parsed)) {
        strlcpy(config.countup.start, defaultCountupStart(), sizeof(config.countup.start));
    }
    sanitizeFormatFields(config);
    if (!isValidTradingSchedule(config.trading.schedule)) {
      config.trading.schedule = defaultTradingSchedule();
    }
    config.display.brightness = sanitizeBrightness(config.display.brightness);
    config.friday.blinkBeforeMinutes =
        sanitizeBlinkMinutes(config.friday.blinkBeforeMinutes);
    config.friday.blinkAfterMinutes =
        sanitizeBlinkMinutes(config.friday.blinkAfterMinutes);
    config.timezone.utcOffsetMinutes =
        sanitizeUtcOffsetMinutes(config.timezone.utcOffsetMinutes);
    sanitizeMessageFields(config);
    sanitizeSoundFields(config);
    sanitizePrintableText(config.timezone.name, config.timezone.name,
                          sizeof(config.timezone.name));
    sanitizeLocationInfo(config.locations.device);
    sanitizeLocationInfo(config.locations.sunsetTest);
}

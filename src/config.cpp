#include "config.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "config_serializer.h"
#include "config_validation.h"
#include "datetime_validation.h"
#include "defaults.h"
#include "log.h"
#include "storage_manager.h"

static constexpr const char* kConfigPath = "/config.json";
static constexpr const char* kConfigTmpPath = "/config.tmp";
static constexpr const char* kConfigBackupPath = "/config.bak";

// -----------------------------------------------------------------------------
// ConfigManager
// -----------------------------------------------------------------------------

// -- WiFi ----------------------------------------------------------------------
bool ConfigManager::ensureLoaded() {
    if (loaded_) return true;

    DeviceConfig next{defaultClockConfig(), defaultWifiConfig()};
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

bool ConfigManager::writeAll(const DeviceConfig& config, const char* context) {
    const uint32_t startedUs = micros();
    if (!storageManager.ensureMounted(context)) return false;
    JsonDocument doc;
    // Callers guarantee config.clock is sanitized (defaults, loaded config,
    // or a save path that sanitized in place) - no extra copy at this depth.
    serializeClockConfig(doc, config.clock);
    serializeWifiConfig(doc, config.wifi);

    STORAGE.remove(kConfigTmpPath);
    File file = STORAGE.open(kConfigTmpPath, "w");
    if (!file) {
        LOG_PRINTF("Complete config write failed: cannot open temp file context=%s", context);
        return false;
    }
    const size_t bytes = serializeJson(doc, file);
    file.flush();
    file.close();
    if (bytes == 0) {
        STORAGE.remove(kConfigTmpPath);
        LOG_PRINTF("Complete config write failed: serialization context=%s", context);
        return false;
    }

    File verifyFile = STORAGE.open(kConfigTmpPath, "r");
    JsonDocument verifyDoc;
    const DeserializationError verifyError = deserializeJson(verifyDoc, verifyFile);
    verifyFile.close();
    if (verifyError) {
        STORAGE.remove(kConfigTmpPath);
        LOG_PRINTF("Complete config verification failed: %s context=%s",
                   verifyError.c_str(), context);
        return false;
    }

    const bool hadOriginal = STORAGE.exists(kConfigPath);
    const bool hadBackup = STORAGE.exists(kConfigBackupPath);
    // A surviving backup may be our only good copy after a failed recovery.
    // Never discard it before the verified replacement is installed.
    if (hadOriginal) {
        const bool preserved = hadBackup ? STORAGE.remove(kConfigPath)
                                         : STORAGE.rename(kConfigPath, kConfigBackupPath);
        if (!preserved) {
            STORAGE.remove(kConfigTmpPath);
            LOG_PRINTF("Config write failed: cannot preserve original context=%s", context);
            return false;
        }
    }
    if (!STORAGE.rename(kConfigTmpPath, kConfigPath)) {
        if (hadOriginal || hadBackup) STORAGE.rename(kConfigBackupPath, kConfigPath);
        STORAGE.remove(kConfigTmpPath);
        LOG_PRINTF("Complete config write failed: cannot install temp file context=%s", context);
        return false;
    }
    STORAGE.remove(kConfigBackupPath);
    const uint32_t elapsedMs = (micros() - startedUs + 500U) / 1000U;
    LOG_PRINTF("Complete config write: bytes=%u time=%lu ms context=%s",
               static_cast<unsigned>(bytes),
               static_cast<unsigned long>(elapsedMs), context);
    return true;
}

WifiConfig ConfigManager::loadWifiConfig() {
    ensureLoaded();
    return current_.wifi;
}

bool ConfigManager::saveWifiConfig(const WifiConfig& cfg) {
    ensureLoaded();
    DeviceConfig next = current_;
    next.wifi = cfg;
    sanitizeWifiConfig(next.wifi);
    if (!writeAll(next, "save WiFi config")) return false;
    current_ = next;
    return true;
}

ClockConfig ConfigManager::loadClockConfig() {
    ensureLoaded();
    return current_.clock;
}

bool ConfigManager::saveClockConfig(ClockConfig& cfg) {
    ensureLoaded();
    sanitizeClockConfig(cfg);
    DeviceConfig next = current_;
    next.clock = cfg;
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

void ConfigManager::sanitizeClockConfig(ClockConfig& cfg) const {
    const ClockConfig defaults = defaultClockConfig();
    cfg.activeMode = sanitizeMode(static_cast<int>(cfg.activeMode), defaults.activeMode);
    DateTime parsed;
    if (!parseLocalDateTime(cfg.countdown.end, parsed)) {
        strlcpy(cfg.countdown.end, defaults.countdown.end, sizeof(cfg.countdown.end));
    }
    if ((strcmp(cfg.countup.start, "now") != 0) &&
        !parseLocalDateTime(cfg.countup.start, parsed)) {
        strlcpy(cfg.countup.start, defaults.countup.start, sizeof(cfg.countup.start));
    }
    sanitizeFormatFields(cfg, defaults);
    if (!isValidTradingSchedule(cfg.trading.schedule)) {
      cfg.trading.schedule = defaults.trading.schedule;
    }
    cfg.display.brightness = sanitizeBrightness(cfg.display.brightness);
    cfg.friday.blinkBeforeMinutes =
        sanitizeBlinkMinutes(cfg.friday.blinkBeforeMinutes);
    cfg.friday.blinkAfterMinutes =
        sanitizeBlinkMinutes(cfg.friday.blinkAfterMinutes);
    cfg.timezone.utcOffsetMinutes =
        sanitizeUtcOffsetMinutes(cfg.timezone.utcOffsetMinutes);
    sanitizeMessageFields(cfg);
    sanitizeSoundFields(cfg);
    sanitizePrintableText(cfg.timezone.name, cfg.timezone.name,
                          sizeof(cfg.timezone.name));
    sanitizeLocationInfo(cfg.locations.device);
    sanitizeLocationInfo(cfg.locations.sunsetTest);
}

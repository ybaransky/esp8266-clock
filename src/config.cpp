#include "config.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "config_serializer.h"
#include "config_validation.h"
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
    File file = STORAGE.open(kConfigPath, "r");
    if (!file) {
        LOG_PRINTLN("config.json not found - creating complete default document");
        const bool saved = writeAll(config, "create default config");
        if (!saved) LOG_PRINTLN("Default config creation failed - using memory defaults");
        return saved;
    }
    const size_t bytes = file.size();
    JsonDocument doc;
    const DeserializationError error = deserializeJson(doc, file);
    file.close();
    if (error) {
        LOG_PRINTF("Complete config read failed: %s bytes=%u time=%.2f ms",
                   error.c_str(), static_cast<unsigned>(bytes),
                   (micros() - startedUs) / 1000.0f);
        return false;
    }
    const char* validationError =
        applyJsonToClockConfig(doc.as<JsonVariantConst>(), config.clock);
    applyJsonToWifiConfig(doc.as<JsonVariantConst>(), config.wifi);
    if (validationError != nullptr) {
        LOG_PRINTF("Complete config has invalid values: %s", validationError);
    }
    sanitizeClockConfig(config.clock);
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

    STORAGE.remove(kConfigBackupPath);
    const bool hadOriginal = STORAGE.exists(kConfigPath);
    if (hadOriginal && !STORAGE.rename(kConfigPath, kConfigBackupPath)) {
        STORAGE.remove(kConfigTmpPath);
        LOG_PRINTF("Complete config write failed: cannot create backup context=%s", context);
        return false;
    }
    if (!STORAGE.rename(kConfigTmpPath, kConfigPath)) {
        if (hadOriginal) STORAGE.rename(kConfigBackupPath, kConfigPath);
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
    if (!writeAll(next, "save WiFi config")) return false;
    current_ = next;
    return true;
}

ClockConfig ConfigManager::loadClockConfig() {
    ensureLoaded();
    return current_.clock;
}

bool ConfigManager::saveClockConfig(const ClockConfig& cfg) {
    ensureLoaded();
    DeviceConfig next = current_;
    next.clock = cfg;
    sanitizeClockConfig(next.clock);
    if (!writeAll(next, "save clock config")) return false;
    current_ = next;
    return true;
}

bool ConfigManager::saveConfig(const ClockConfig& clock, const WifiConfig& wifi) {
    ensureLoaded();
    DeviceConfig next{clock, wifi};
    sanitizeClockConfig(next.clock);
    if (!writeAll(next, "save complete config")) return false;
    current_ = next;
    return true;
}

void ConfigManager::sanitizeClockConfig(ClockConfig& cfg) const {
    const ClockConfig defaults = defaultClockConfig();
    cfg.activeMode   = sanitizeMode(static_cast<int>(cfg.activeMode), defaults.activeMode);
    cfg.countdown.format = sanitizeFormatIndex(
        kFmtGroupCountdown, cfg.countdown.format, defaults.countdown.format);
    cfg.countup.format = sanitizeFormatIndex(
        kFmtGroupCountUp, cfg.countup.format, defaults.countup.format);
    cfg.display.clockFmt     = sanitizeFormatIndex(kFmtGroupClock,     cfg.display.clockFmt,     defaults.display.clockFmt);
    cfg.friday.clockFmt = sanitizeFormatIndex(
        kFmtGroupClock, cfg.friday.clockFmt, defaults.friday.clockFmt);
    cfg.friday.toFridaySunsetFmt = sanitizeFormatIndex(
        kFmtGroupCountdown, cfg.friday.toFridaySunsetFmt,
        defaults.friday.toFridaySunsetFmt);
    cfg.friday.toSaturdaySunsetFmt = sanitizeFormatIndex(
        kFmtGroupCountdown, cfg.friday.toSaturdaySunsetFmt,
        defaults.friday.toSaturdaySunsetFmt);
    cfg.trading.format = sanitizeFormatIndex(
        kFmtGroupCountdown, cfg.trading.format, defaults.trading.format);
    cfg.trading.formatOver24 = sanitizeOptionalFormatIndex(
        kFmtGroupCountdown, cfg.trading.formatOver24,
        defaults.trading.formatOver24);
    cfg.display.brightness = sanitizeBrightness(cfg.display.brightness);
    cfg.timezone.utcOffsetMinutes =
        sanitizeUtcOffsetMinutes(cfg.timezone.utcOffsetMinutes);
    sanitizeDisplayMessage(cfg.messages.splash, cfg.messages.splash, sizeof(cfg.messages.splash));
    sanitizeDisplayMessage(cfg.messages.final, cfg.messages.final, sizeof(cfg.messages.final));
    sanitizeDisplayMessage(cfg.messages.fridaySunset, cfg.messages.fridaySunset,
                           sizeof(cfg.messages.fridaySunset));
    sanitizeDisplayMessage(cfg.messages.tradingOpen, cfg.messages.tradingOpen,
                           sizeof(cfg.messages.tradingOpen));
    sanitizeDisplayMessage(cfg.messages.tradingClose, cfg.messages.tradingClose,
                           sizeof(cfg.messages.tradingClose));
    sanitizePrintableText(cfg.timezone.name, cfg.timezone.name,
                          sizeof(cfg.timezone.name));
}

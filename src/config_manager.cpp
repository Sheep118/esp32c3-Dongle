#include "config_manager.h"
#include <LittleFS.h>
#include <ArduinoJson.h>

static const char* CONFIG_FILE = "/ble_dongle.json";

bool ConfigManager::begin() {
    if (!LittleFS.begin(true)) {
        Serial.println("[Config] LittleFS mount failed!");
        return false;
    }
    Serial.println("[Config] LittleFS mounted");
    _load();
    return true;
}

String ConfigManager::_configPath() const {
    return CONFIG_FILE;
}

bool ConfigManager::_load() {
    if (!LittleFS.exists(CONFIG_FILE)) {
        Serial.println("[Config] No config file, using defaults");
        return true;
    }

    File file = LittleFS.open(CONFIG_FILE, "r");
    if (!file) {
        Serial.println("[Config] Failed to open config for reading");
        return false;
    }

    String json = file.readString();
    file.close();

    bool ok = configFromJson(json);
    if (ok) {
        Serial.printf("[Config] Loaded: scanDuplicate=%d whitelist=%zu\n",
                      _scanParams.scanDuplicate, _whitelist.size());
    }
    return ok;
}

bool ConfigManager::_save() {
    String json = configToJson();
    File file = LittleFS.open(CONFIG_FILE, "w");
    if (!file) {
        Serial.println("[Config] Failed to open config for writing");
        return false;
    }
    file.print(json);
    file.flush();   // 确保写入完成
    file.close();
    Serial.printf("[Config] Config saved (scanDuplicate=%d)\n", _scanParams.scanDuplicate);
    return true;
}

const std::vector<WhitelistEntry>& ConfigManager::getWhitelist() const {
    return _whitelist;
}

bool ConfigManager::setWhitelist(const std::vector<WhitelistEntry>& list) {
    _whitelist = list;
    return _save();
}

bool ConfigManager::addEntry(const WhitelistEntry& entry) {
    _whitelist.push_back(entry);
    return _save();
}

bool ConfigManager::removeEntry(size_t index) {
    if (index >= _whitelist.size()) return false;
    _whitelist.erase(_whitelist.begin() + index);
    return _save();
}

bool ConfigManager::clearWhitelist() {
    _whitelist.clear();
    return _save();
}

String ConfigManager::configToJson() const {
    JsonDocument doc;
    doc["deviceMode"] = static_cast<uint8_t>(_deviceMode);
    doc["wifiMode"]   = _wifiMode;

    // 白名单
    JsonArray arr = doc["whitelist"].to<JsonArray>();
    for (const auto& e : _whitelist) {
        JsonObject obj = arr.add<JsonObject>();
        obj["type"]     = static_cast<int>(e.type);
        obj["value"]    = e.value;
        obj["companyId"] = e.companyId;
        obj["enabled"]  = e.enabled;
    }

    // BLE 扫描参数
    JsonObject sp = doc["scanParams"].to<JsonObject>();
    sp["scanInterval"]     = _scanParams.scanInterval;
    sp["scanWindow"]       = _scanParams.scanWindow;
    sp["scanType"]         = _scanParams.scanType;
    sp["scanDuplicate"]    = _scanParams.scanDuplicate;
    sp["ownAddrType"]      = _scanParams.ownAddrType;
    sp["scanFilterPolicy"] = _scanParams.scanFilterPolicy;

    // BLE 广播参数
    JsonObject ap = doc["advConfig"].to<JsonObject>();
    ap["customMac"]       = _advConfig.customMac;
    ap["advDataHex"]      = _advConfig.advDataHex;
    ap["scanRespHex"]     = _advConfig.scanRespHex;
    ap["txPower"]         = _advConfig.txPower;
    ap["advIntervalMin"]  = _advConfig.advIntervalMin;
    ap["advIntervalMax"]  = _advConfig.advIntervalMax;
    ap["advDuration"]     = _advConfig.advDuration;
    ap["advType"]         = _advConfig.advType;
    ap["channelMap"]      = _advConfig.channelMap;

    String out;
    serializeJson(doc, out);
    return out;
}

bool ConfigManager::configFromJson(const String& json) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err) {
        Serial.printf("[Config] JSON parse error: %s\n", err.c_str());
        return false;
    }

    // 设备模式
    if (doc["deviceMode"].is<uint8_t>()) {
        _deviceMode = static_cast<DeviceMode>(doc["deviceMode"].as<uint8_t>());
    } else if (doc["deviceMode"].is<int>()) {
        _deviceMode = static_cast<DeviceMode>(doc["deviceMode"].as<int>());
    }

    if (doc["wifiMode"].is<bool>()) {
        _wifiMode = doc["wifiMode"].as<bool>();
    }

    // 白名单
    _whitelist.clear();
    if (doc["whitelist"].is<JsonArray>()) {
        for (const auto& item : doc["whitelist"].as<JsonArray>()) {
            WhitelistEntry e;
            e.type     = static_cast<WhitelistEntry::Type>(item["type"] | 0);
            e.value    = item["value"] | "";
            e.companyId = item["companyId"] | 0;
            e.enabled  = item["enabled"] | true;
            _whitelist.push_back(e);
        }
    }

    // BLE 扫描参数
    JsonObject sp = doc["scanParams"];
    if (!sp.isNull()) {
        _scanParams.scanInterval     = sp["scanInterval"]   | 100;
        _scanParams.scanWindow       = sp["scanWindow"]     | 99;
        _scanParams.scanType         = sp["scanType"]       | 1;
        _scanParams.scanDuplicate    = sp["scanDuplicate"]  | false;
        _scanParams.ownAddrType      = sp["ownAddrType"]    | 0;
        _scanParams.scanFilterPolicy = sp["scanFilterPolicy"] | 0;
    }

    // BLE 广播参数
    JsonObject ap = doc["advConfig"];
    if (!ap.isNull()) {
        _advConfig.customMac       = ap["customMac"]       | "";
        _advConfig.advDataHex      = ap["advDataHex"]      | "";
        _advConfig.scanRespHex     = ap["scanRespHex"]     | "";
        _advConfig.txPower         = ap["txPower"]         | 0;
        _advConfig.advIntervalMin  = ap["advIntervalMin"]  | 100;
        _advConfig.advIntervalMax  = ap["advIntervalMax"]  | 100;
        _advConfig.advDuration     = ap["advDuration"]     | 0;
        _advConfig.advType         = ap["advType"]         | 0;
        _advConfig.channelMap      = ap["channelMap"]      | 7;
    }

    return true;
}

DeviceMode ConfigManager::getDeviceMode() const {
    return _deviceMode;
}

void ConfigManager::setDeviceMode(DeviceMode mode) {
    _deviceMode = mode;
    _save();
}

BleAdvConfig ConfigManager::getAdvConfig() const {
    return _advConfig;
}

void ConfigManager::setAdvConfig(const BleAdvConfig& cfg) {
    _advConfig = cfg;
    _save();
}

BleScanParams ConfigManager::getScanParams() const {
    return _scanParams;
}

void ConfigManager::setScanParams(const BleScanParams& params) {
    _scanParams = params;
    _save();
}

bool ConfigManager::save() {
    return _save();
}

bool ConfigManager::isWifiMode() const {
    return _wifiMode;
}

void ConfigManager::setWifiMode(bool wifiMode) {
    _wifiMode = wifiMode;
    _save();
}

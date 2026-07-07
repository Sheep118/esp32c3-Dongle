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

    bool ok = whitelistFromJson(json);
    if (ok) {
        Serial.printf("[Config] Loaded: scanDuplicate=%d whitelist=%zu\n",
                      _scanParams.scanDuplicate, _whitelist.size());
    }
    return ok;
}

bool ConfigManager::_save() {
    String json = whitelistToJson();
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

String ConfigManager::whitelistToJson() const {
    JsonDocument doc;
    JsonArray arr = doc["whitelist"].to<JsonArray>();

    for (const auto& e : _whitelist) {
        JsonObject obj = arr.add<JsonObject>();
        obj["type"]    = static_cast<int>(e.type);
        obj["value"]   = e.value;
        obj["companyId"] = e.companyId;
        obj["enabled"] = e.enabled;
    }
    doc["wifiMode"] = _wifiMode;

    // BLE 扫描参数
    JsonObject sp = doc["scanParams"].to<JsonObject>();
    sp["scanInterval"]     = _scanParams.scanInterval;
    sp["scanWindow"]       = _scanParams.scanWindow;
    sp["scanType"]         = _scanParams.scanType;
    sp["scanDuplicate"]    = _scanParams.scanDuplicate;
    sp["ownAddrType"]      = _scanParams.ownAddrType;
    sp["scanFilterPolicy"] = _scanParams.scanFilterPolicy;

    String out;
    serializeJson(doc, out);
    return out;
}

bool ConfigManager::whitelistFromJson(const String& json) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err) {
        Serial.printf("[Config] JSON parse error: %s\n", err.c_str());
        return false;
    }

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

    if (doc["wifiMode"].is<bool>()) {
        _wifiMode = doc["wifiMode"].as<bool>();
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

    return true;
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

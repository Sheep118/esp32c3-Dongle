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
    return whitelistFromJson(json);
}

bool ConfigManager::_save() {
    String json = whitelistToJson();
    File file = LittleFS.open(CONFIG_FILE, "w");
    if (!file) {
        Serial.println("[Config] Failed to open config for writing");
        return false;
    }
    file.print(json);
    file.close();
    Serial.println("[Config] Config saved");
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

    return true;
}

bool ConfigManager::isWifiMode() const {
    return _wifiMode;
}

void ConfigManager::setWifiMode(bool wifiMode) {
    _wifiMode = wifiMode;
    _save();
}

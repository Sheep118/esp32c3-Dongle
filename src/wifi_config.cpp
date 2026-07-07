#include "wifi_config.h"
#include <WiFi.h>
#include <ArduinoJson.h>
#include "ble_scanner.h"
#include "webpage.h"   // 独立 HTML 页面（编译期由 embed_html.py 生成）

// =============== WifiConfigServer ===============
WifiConfigServer::WifiConfigServer() : _config(nullptr), _server(nullptr), _bleScanner(nullptr) {}

void WifiConfigServer::setBleScanner(BleScanner* scanner) {
    _bleScanner = scanner;
}

bool WifiConfigServer::begin(ConfigManager* config) {
    _config = config;

    // 启动 Wi-Fi AP
    WiFi.mode(WIFI_AP);
    WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD, WIFI_AP_CHANNEL, 0, WIFI_AP_MAX_CLIENTS);

    IPAddress ip = WiFi.softAPIP();
    Serial.printf("[WiFi] AP started: SSID=%s IP=%s\n", WIFI_AP_SSID, ip.toString().c_str());

    // 启动 Web 服务器
    _server = new WebServer(80);
    _setupRoutes();
    _server->begin();
    Serial.println("[WiFi] HTTP server started on port 80");

    return true;
}

void WifiConfigServer::update() {
    if (_server) {
        _server->handleClient();
    }
}

int WifiConfigServer::getClientCount() const {
    return WiFi.softAPgetStationNum();
}

void WifiConfigServer::_setupRoutes() {
    _server->on("/",                   HTTP_GET,  [this](){ _handleRoot(); });
    _server->on("/api/config",         HTTP_GET,  [this](){ _handleGetConfig(); });
    _server->on("/api/config",         HTTP_POST, [this](){ _handleSaveConfig(); });
    _server->on("/api/config",         HTTP_DELETE, [this](){ _handleClearAll(); });
    _server->on("/api/entry",          HTTP_POST, [this](){ _handleAddEntry(); });
    _server->on("/api/entry",          HTTP_DELETE, [this](){ _handleDeleteEntry(); });
    _server->on("/api/scanparams",     HTTP_GET,  [this](){ _handleGetScanParams(); });
    _server->on("/api/reboot",         HTTP_POST, [this](){ _handleReboot(); });
    _server->onNotFound([this](){ _handleNotFound(); });
}

void WifiConfigServer::_handleRoot() {
    _server->send_P(200, "text/html; charset=utf-8", INDEX_HTML);
}

void WifiConfigServer::_handleGetConfig() {
    String json = _config->whitelistToJson();
    _server->send(200, "application/json; charset=utf-8", json);
}

void WifiConfigServer::_handleSaveConfig() {
    String body = _server->arg("plain");
    if (_config->whitelistFromJson(body)) {
        // whitelistFromJson 同时解析 whitelist 和 scanParams
        _config->save();
        // 通知 BLE 扫描器热应用新的扫描参数
        if (_bleScanner) {
            _bleScanner->applyScanParams();
        }
        _server->send(200, "application/json", "{\"message\":\"保存成功\",\"status\":\"ok\"}");
        Serial.println("[WiFi] Config saved via web");
    } else {
        _server->send(400, "application/json", "{\"message\":\"JSON 解析失败\",\"status\":\"error\"}");
    }
}

void WifiConfigServer::_handleAddEntry() {
    String body = _server->arg("plain");
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);
    if (err) {
        _server->send(400, "application/json", "{\"message\":\"JSON 解析失败\",\"status\":\"error\"}");
        return;
    }
    WhitelistEntry entry;
    entry.type      = static_cast<WhitelistEntry::Type>(doc["type"] | 0);
    entry.value     = doc["value"] | "";
    entry.companyId = doc["companyId"] | 0;
    entry.enabled   = doc["enabled"] | true;
    _config->addEntry(entry);
    _server->send(200, "application/json", "{\"message\":\"添加成功\",\"status\":\"ok\"}");
}

void WifiConfigServer::_handleDeleteEntry() {
    String idxStr = _server->arg("index");
    if (idxStr.isEmpty()) {
        // 从 body 中取
        String body = _server->arg("plain");
        JsonDocument doc;
        deserializeJson(doc, body);
        idxStr = doc["index"] | "";
    }
    int idx = idxStr.toInt();
    if (_config->removeEntry(idx)) {
        _server->send(200, "application/json", "{\"message\":\"删除成功\",\"status\":\"ok\"}");
    } else {
        _server->send(400, "application/json", "{\"message\":\"索引无效\",\"status\":\"error\"}");
    }
}

void WifiConfigServer::_handleClearAll() {
    _config->clearWhitelist();
    _server->send(200, "application/json", "{\"message\":\"已清空所有规则\",\"status\":\"ok\"}");
}

void WifiConfigServer::_handleGetScanParams() {
    if (!_config) {
        _server->send(500, "application/json", "{\"status\":\"error\",\"message\":\"config not available\"}");
        return;
    }
    BleScanParams p = _config->getScanParams();
    JsonDocument doc;
    doc["scanInterval"]     = p.scanInterval;
    doc["scanWindow"]       = p.scanWindow;
    doc["scanType"]         = p.scanType;
    doc["scanDuplicate"]    = p.scanDuplicate;
    doc["ownAddrType"]      = p.ownAddrType;
    doc["scanFilterPolicy"] = p.scanFilterPolicy;
    String out;
    serializeJson(doc, out);
    _server->send(200, "application/json; charset=utf-8", out);
}

void WifiConfigServer::_handleReboot() {
    _server->send(200, "application/json", "{\"message\":\"设备即将重启...\",\"status\":\"ok\"}");
    delay(500);
    ESP.restart();
}

void WifiConfigServer::_handleNotFound() {
    _server->send(404, "text/plain", "404 Not Found");
}

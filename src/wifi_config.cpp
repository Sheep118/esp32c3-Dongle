#include "wifi_config.h"
#include <WiFi.h>
#include <ArduinoJson.h>
#include <lwip/dns.h>
#include <lwip/netif.h>
#include "webpage.h"

#define LOG_TAG "WiFi"
#include "log.h"

// =============== WifiConfigServer ===============
WifiConfigServer::WifiConfigServer() : _config(nullptr), _server(nullptr) {}

bool WifiConfigServer::begin(ConfigManager* config) {
    _config = config;

    // 先关闭 WiFi，确保干净状态
    WiFi.mode(WIFI_OFF);
    delay(100);
    WiFi.mode(WIFI_AP);

    // 用 esp_wifi 底层 API 配置 DHCP-DNS 为 AP IP
    // 这样客户端获取的 DNS 就是 192.168.4.1，任何 DNS 查询都被发到 ESP32
    WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD, WIFI_AP_CHANNEL, 0, WIFI_AP_MAX_CLIENTS);

    IPAddress apIp = WiFi.softAPIP();

    // ====== 关键修改：用 lwIP DNS 服务器直接接管 ======
    // 在 ESP32 NAT 模式下，将所有 DNS 查询指向自己
    // 把 DHCP 广播的 DNS 服务地址设置成 AP IP
    {
        ip_addr_t dns_ip;
        IP_ADDR4(&dns_ip, apIp[0], apIp[1], apIp[2], apIp[3]);
        dhcps_dns_setserver(&dns_ip);
        dns_setserver(0, &dns_ip);  // lwIP 主 DNS 指向自己
    }
    _apIp = apIp;

    // 启动自定义 UDP DNS 响应
    _dnsUdp.begin(53);  // 监听 53 端口

    LOG_INFO("AP started: SSID=%s IP=%s", WIFI_AP_SSID, apIp.toString().c_str());
    LOG_INFO("DHCP DNS set to AP IP, UDP DNS on port 53");

    // ====== Web 服务器 ======
    _server = new WebServer(80);
    _setupRoutes();
    _server->begin();
    LOG_INFO("HTTP server on port 80 | Visit http://any.domain in browser");

    return true;
}

/**
 * 手动处理 UDP DNS 查询 —— 替代 DNSServer 库
 * DNSServer 内部用 _udp.begin(53) 和 lwIP dns_setserver 冲突
 * 这里直接用 WiFiUDP + 手动构建 DNS 响应包
 */
static bool _resolveDnsQuery(WiFiUDP& udp, IPAddress& apIp) {
    int pktSize = udp.parsePacket();
    if (pktSize < 12) return false;  // DNS 头最小 12 字节

    uint8_t buf[256];
    int len = udp.read(buf, sizeof(buf));
    if (len < 12) return false;

    // 只处理标准查询 (QR=0, OPCode=0)
    if ((buf[2] & 0x80) != 0) return false;             // 不是查询
    if (((buf[2] >> 3) & 0x0F) != 0) return false;      // 不是标准查询

    // 构建 DNS 响应
    uint8_t response[512];
    memcpy(response, buf, len);  // 复制查询部分

    // DNS 标志: QR=1 (响应), OPCode=0, AA=1, RD 继承查询
    response[2] = 0x85;   // QR=1, OPCode=0, AA=1, TC=0, RD=1
    response[3] = 0x80;   // RA=1, Z=0, RCODE=0
    // 问题数保持原样
    // 回答数 = 1
    response[6] = 0x00; response[7] = 0x01;
    // 权威记录数 = 0
    response[8] = 0x00; response[9] = 0x00;
    // 附加记录数 = 0
    response[10] = 0x00; response[11] = 0x00;

    // 答案部分：Name(压缩指针: 0xC00C 指向查询中的域名) + Type(A=1) + Class(IN=1)
    // + TTL(60s) + DataLength(4) + IP(4)
    int ansPos = len;
    response[ansPos++] = 0xC0; response[ansPos++] = 0x0C;  // 压缩指针
    response[ansPos++] = 0x00; response[ansPos++] = 0x01;  // Type A
    response[ansPos++] = 0x00; response[ansPos++] = 0x01;  // Class IN
    response[ansPos++] = 0x00; response[ansPos++] = 0x00;
    response[ansPos++] = 0x00; response[ansPos++] = 0x3C;  // TTL = 60
    response[ansPos++] = 0x00; response[ansPos++] = 0x04;  // Data length = 4
    response[ansPos++] = apIp[0]; response[ansPos++] = apIp[1];
    response[ansPos++] = apIp[2]; response[ansPos++] = apIp[3];

    udp.beginPacket(udp.remoteIP(), udp.remotePort());
    udp.write(response, ansPos);
    udp.endPacket();
    return true;
}

void WifiConfigServer::update() {
    // DNS: 手动处理所有排队的 UDP 包
    for (int i = 0; i < 10; i++) {
        if (_resolveDnsQuery(_dnsUdp, _apIp)) {
            // 解析成功
        } else {
            break;  // 没有更多包了
        }
    }
    if (_server) {
        _server->handleClient();
    }
}

int WifiConfigServer::getClientCount() const {
    return WiFi.softAPgetStationNum();
}

void WifiConfigServer::_setupRoutes() {
    // 首页
    _server->on("/",                   HTTP_GET,  [this](){ _handleRoot(); });

    // API 路由
    _server->on("/api/config",         HTTP_GET,  [this](){ _handleGetConfig(); });
    _server->on("/api/config",         HTTP_POST, [this](){ _handleSaveConfig(); });
    _server->on("/api/config",         HTTP_DELETE, [this](){ _handleClearAll(); });
    _server->on("/api/entry",          HTTP_POST, [this](){ _handleAddEntry(); });
    _server->on("/api/entry",          HTTP_DELETE, [this](){ _handleDeleteEntry(); });
    _server->on("/api/scanparams",     HTTP_GET,  [this](){ _handleGetScanParams(); });
    _server->on("/api/scanformat",     HTTP_GET,  [this](){ _handleGetScanFormat(); });
    _server->on("/api/reboot",         HTTP_POST, [this](){ _handleReboot(); });

    // Captive Portal 检测端点
    _server->on("/generate_204",              HTTP_GET, [this](){ _handleCaptivePortal(); });  // Android / ChromeOS
    _server->on("/hotspot-detect.html",       HTTP_GET, [this](){ _handleCaptivePortal(); });  // iOS / macOS
    _server->on("/library/test/success.html", HTTP_GET, [this](){ _handleCaptivePortal(); });  // iOS 7+
    _server->on("/success.txt",               HTTP_GET, [this](){ _handleCaptivePortal(); });  // 部分 Android
    _server->on("/canonical.html",            HTTP_GET, [this](){ _handleCaptivePortal(); });  // Firefox
    _server->on("/favicon.ico",               HTTP_GET, [this](){ _handleHttp302ToRoot(); });  // 导到首页

    // 通配：所有未匹配 GET → 302 重定向到首页（通用网站跳转 + Windows fallback）
    _server->onNotFound([this](){ _handleCaptivePortal(); });
}

void WifiConfigServer::_handleRoot() {
    LOG_INFO("GET /  (client IP=%s)", _server->client().remoteIP().toString().c_str());
    _server->send_P(200, "text/html; charset=utf-8", INDEX_HTML);
}

void WifiConfigServer::_handleGetConfig() {
    String json = _config->configToJson();
    _server->send(200, "application/json; charset=utf-8", json);
}

void WifiConfigServer::_handleSaveConfig() {
    String body = _server->arg("plain");
    if (_config->configFromJson(body)) {
        // configFromJson 从json中更新 _config 的内容，调用 save() 保存到文件
        _config->save();

        _server->send(200, "application/json", "{\"message\":\"保存成功\",\"status\":\"ok\"}");
        LOG_INFO("Config saved via web");
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

void WifiConfigServer::_handleGetScanFormat() {
    if (!_config) {
        _server->send(500, "application/json", "{\"status\":\"error\"}");
        return;
    }
    BleScanFormat f = _config->getScanFormat();
    JsonDocument doc;
    doc["scanStartFmt"]  = f.scanStartFmt;
    doc["scanResultFmt"] = f.scanResultFmt;
    doc["scanEndFmt"]    = f.scanEndFmt;
    String out;
    serializeJson(doc, out);
    _server->send(200, "application/json; charset=utf-8", out);
}

void WifiConfigServer::_handleReboot() {
    _server->send(200, "application/json", "{\"message\":\"设备即将重启...\",\"status\":\"ok\"}");
    delay(500);
    ESP.restart();
}

/**
 * Captive Portal Handler
 *
 * Android/iOS: 系统探测 URL → 收到 302 → 自动弹出 Portal 浏览器
 * 通用:     用户浏览器访问任意网址 → DNS 劫持 to ESP32 → 302 到首页
 */
void WifiConfigServer::_handleCaptivePortal() {
    String uri = _server->uri();
    String host = _server->hostHeader();
    LOG_INFO("Portal: uri=%s host=%s", uri.c_str(), host.c_str());

    // iOS / macOS hotspot detect: 返回非 "Success" 触发 Portal
    if (uri == "/hotspot-detect.html" || uri == "/library/test/success.html") {
        _server->send(200, "text/html", "Redirecting...");
        return;
    }

    // Android generate_204 及所有其他 URL: 302 重定向到首页
    _server->sendHeader("Location", "http://192.168.4.1/", true);
    _server->send(302, "text/plain", "Redirecting...");
}

void WifiConfigServer::_handleHttp302ToRoot() {
    _server->sendHeader("Location", "/", true);
    _server->send(302, "text/plain", "");
}

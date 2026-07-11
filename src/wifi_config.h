#ifndef WIFI_CONFIG_H
#define WIFI_CONFIG_H

#include <Arduino.h>
#include <WebServer.h>
#include <WiFiUdp.h>
#include "config_manager.h"

class BleScanner; // 前向声明

/**
 * Wi-Fi AP 配置
 */
#define WIFI_AP_SSID       "BLE-Dongle-Config"
#define WIFI_AP_PASSWORD   "12345678"
#define WIFI_AP_CHANNEL    1
#define WIFI_AP_MAX_CLIENTS 4

/**
 * Wi-Fi 配置模式
 * - 启动 Wi-Fi AP 热点
 * - DNS 劫持 + Captive Portal（Android/iOS 自动弹窗，通用 HTTP 302 跳转）
 * - 提供 Web 配置页面用于管理 BLE 白名单和参数
 */
class WifiConfigServer {
public:
    WifiConfigServer();

    /** 启动 AP 和 Web 服务器，同时启动 Captive Portal (DNS + NotFound) */
    bool begin(ConfigManager* config);

    /** 必须在主 loop() 中周期性调用（处理 Web + DNS 请求） */
    void update();

    /** 获取已连接的客户端数 */
    int getClientCount() const;

private:
    ConfigManager* _config;
    WebServer*     _server;
    WiFiUDP        _dnsUdp;       ///< 手动 DNS 响应 UDP socket
    IPAddress      _apIp;          ///< AP IP 地址（DNS 解析到此）

    /** 注册所有 API 路由 */
    void _setupRoutes();

    // ---- HTTP 处理函数 ----
    void _handleRoot();
    void _handleGetConfig();
    void _handleSaveConfig();
    void _handleAddEntry();
    void _handleDeleteEntry();
    void _handleClearAll();
    void _handleGetScanParams();
    void _handleReboot();
    /** Captive Portal：任何未匹配 GET 请求返回首页（接管 404） */
    void _handleCaptivePortal();
    /** 302 重定向到根路径 */
    void _handleHttp302ToRoot();
};

#endif // WIFI_CONFIG_H

#ifndef WIFI_CONFIG_H
#define WIFI_CONFIG_H

#include <Arduino.h>
#include <WebServer.h>
#include "config_manager.h"

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
 * - 提供 Web 配置页面用于管理 BLE 白名单
 */
class WifiConfigServer {
public:
    WifiConfigServer();

    /** 启动 AP 和 Web 服务器 */
    bool begin(ConfigManager* config);

    /** 必须在主 loop() 中周期性调用 */
    void update();

    /** 获取已连接的客户端数 */
    int getClientCount() const;

private:
    ConfigManager* _config;
    WebServer*     _server;

    /** 注册所有 API 路由 */
    void _setupRoutes();

    // ---- HTTP 处理函数 ----
    /** 首页 - 配置管理页面 */
    void _handleRoot();

    /** 获取当前白名单 (JSON) */
    void _handleGetConfig();

    /** 保存白名单 (JSON) */
    void _handleSaveConfig();

    /** 添加单条白名单 */
    void _handleAddEntry();

    /** 删除指定条目 */
    void _handleDeleteEntry();

    /** 清除所有条目 */
    void _handleClearAll();

    /** 获取 BLE 扫描参数 */
    void _handleGetScanParams();

    /** 保存 BLE 扫描参数 */
    void _handleSaveScanParams();

    /** 重启设备 */
    void _handleReboot();

    /** 404 */
    void _handleNotFound();
};

#endif // WIFI_CONFIG_H

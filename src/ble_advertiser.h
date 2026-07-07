#ifndef BLE_ADVERTISER_H
#define BLE_ADVERTISER_H

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLEAdvertising.h>

#include "config_manager.h"

/**
 * BLE 模拟广播模块
 *
 * 功能：
 *   - 按配置参数广播 BLE 数据包
 *   - 支持自定义 MAC 地址（需在 begin() 前设置）
 *   - 支持自定义广播数据 / 扫描响应数据（HEX 格式）
 *   - 可配置广播间隔、功率、类型、时长
 *   - 支持非阻塞定时停止（advDuration > 0 时）
 */
class BleAdvertiser {
public:
    BleAdvertiser();

    /** 初始化 BLE 设备并应用广播参数 */
    bool begin(ConfigManager* config);

    /** 主循环中周期性调用，处理定时停止逻辑 */
    void update();

    /** 从 ConfigManager 重新加载配置并应用 */
    void applyConfig();

    /** 开始广播 */
    void startAdvertising();

    /** 停止广播 */
    void stopAdvertising();

    /** 是否正在广播 */
    bool isAdvertising() const { return _advertising; }

private:
    ConfigManager*  _config;
    BLEAdvertising* _pAdvertising;
    BLEServer*      _pServer;
    bool            _advertising;
    uint32_t        _startTime;   // 本次广播开始时间 (millis)
    bool            _initialized; // BLE 设备是否已初始化

    /** 应用程序自定义 MAC（需在 BLEDevice::init 前调用） */
    bool _applyCustomMac(const String& macStr);

    /** 解析 HEX 字符串为字节数组并设置广播数据 */
    bool _setAdvDataFromHex(BLEAdvertisementData& advData, const String& hexStr, bool isScanResp);
};

#endif // BLE_ADVERTISER_H

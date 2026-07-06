#ifndef BLE_SCANNER_H
#define BLE_SCANNER_H

#include <Arduino.h>
#include <vector>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>

#include "config_manager.h"

/**
 * BLE 扫描结果数据结构
 */
struct BleScanResult {
    String  mac;        // MAC 地址 "AA:BB:CC:DD:EE:FF"
    String  name;       // 广播名（可能为空）
    int     rssi;       // 信号强度 dBm
    uint8_t addrType;   // 地址类型
    String  rawDataHex; // 原始广播数据 HEX
    uint16_t companyId; // 厂商 ID（如果存在）
};

/**
 * BLE 扫描器
 * - 循环扫描周围蓝牙广播
 * - 根据 ConfigManager 中的白名单进行过滤
 * - 将筛选后的数据格式化为 JSON 输出到 Serial
 */
class BleScanner {
public:
    BleScanner();

    /** 初始化 BLE 并开始扫描 */
    bool begin(ConfigManager* config);

    /** 设置扫描持续时间（秒），默认 5 秒一个周期 */
    void setScanDuration(uint32_t seconds);

    /** 必须在主 loop() 中周期性调用 */
    void update();

    /** 手动触发一次扫描 */
    void startScan();

    /** 获取最近一次扫描的结果数 */
    size_t getLastResultCount() const;

private:
    ConfigManager* _config;
    BLEScan*       _pBLEScan;
    uint32_t       _scanDuration;   // 每次扫描持续秒数
    uint32_t       _scanStartMs;
    bool           _scanning;
    size_t         _lastCount;

    /** 检查 BLE 广播是否匹配白名单 */
    bool _matchWhitelist(BLEAdvertisedDevice& device) const;

    /** 格式化并输出一条扫描结果到 Serial */
    void _outputResult(BLEAdvertisedDevice& device);

    /** BLE 扫描期间发现设备回调 */
    class AdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
    public:
        AdvertisedDeviceCallbacks(BleScanner* parent) : _parent(parent) {}
        void onResult(BLEAdvertisedDevice advertisedDevice) override;
    private:
        BleScanner* _parent;
    };

    /** BLE 一轮扫描完成回调（静态函数） */
    static void scanCompleteCB(BLEScanResults results);
};

#endif // BLE_SCANNER_H

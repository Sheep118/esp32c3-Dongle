#ifndef BLE_SCANNER_H
#define BLE_SCANNER_H

#include <Arduino.h>
#include <vector>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>

#include "config_manager.h"

/** 环形队列：一行输出缓存 */
#define TX_QUEUE_SIZE  32
#define TX_LINE_MAX    256

/**
 * 输出格式说明（USB-CDC 串口）：
 *   $BLE|MAC:AA:BB:CC:DD:EE:FF|RSSI:-42|ADDR:1|NAME:DeviceName|MANUF:4C00|UUID:0000fdaa-...
 *   $SCAN_END|COUNT:18
 *   $SCAN_START|DURATION:5
 *
 *   ⚠ 关键安全设计：
 *   回调 onResult() 运行在 BTC_TASK 中。在此上下文中绝对不能调用
 *   Serial.print/Serial.printf/Serial.println，因为 UART 驱动内部使用
 *   互斥锁，与主 loop() 中的 Serial 操作冲突 → 死锁 → 崩溃。
 *
 *   因此回调中仅将格式化后的数据压入 _txQueue（无锁环形队列），
 *   然后让 flushOutput() 在 main loop() 中统一输出到串口。
 */

class BleScanner {
public:
    BleScanner();

    bool begin(ConfigManager* config);
    void setScanDuration(uint32_t seconds);

    /** 驱动 BLE 扫描周期状态机 */
    void update();

    /** 从环形队列取数据输出到 Serial（在 main loop 末尾调用） */
    void flushOutput();

    void startScan();
    size_t getLastResultCount() const;

private:
    ConfigManager* _config;
    BLEScan*       _pBLEScan;
    uint32_t       _scanDuration;
    uint32_t       _scanStartMs;
    bool           _scanning;
    size_t         _lastCount;

    // ---- 无锁环形队列 ----
    char  _txQueue[TX_QUEUE_SIZE][TX_LINE_MAX];
    volatile int _txHead;   // 生产者（BTC_TASK）写入位置
    volatile int _txTail;   // 消费者（loop）读取位置

    void _enqueue(const char* line);

    // ---- BLE 逻辑 ----
    bool _matchWhitelist(BLEAdvertisedDevice& device) const;
    void _outputResult(BLEAdvertisedDevice& device);

    class AdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
    public:
        AdvertisedDeviceCallbacks(BleScanner* parent) : _parent(parent) {}
        void onResult(BLEAdvertisedDevice advertisedDevice) override;
    private:
        BleScanner* _parent;
    };

    static void scanCompleteCB(BLEScanResults results);
};

#endif // BLE_SCANNER_H

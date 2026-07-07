#ifndef BLE_SCANNER_H
#define BLE_SCANNER_H

#include <Arduino.h>
#include <vector>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <esp_gap_ble_api.h>

#include "config_manager.h"

/**
 * 环形队列参数
 *   扩展广播一个 PDU 最大可达 1650 字节（HEX 输出约 3300 字符），
 *   但通常实际数据量在几百字节内。TX_LINE_MAX=1024 可覆盖绝大多数场景。
 */
#define TX_QUEUE_SIZE  48
#define TX_LINE_MAX    512

/**
 * 输出格式说明（USB-CDC 串口）：
 *   $BLE|MAC:AA:BB:CC:DD:EE:FF|RSSI:-42|AT:1[|NM:xxx][|MF:HEX][|SV:uuid]
 *   $SCAN_START|DURATION:5
 *   $SCAN_END|TOTAL:85|MATCHED:17
 *
 *   BLE 设备行以 $BLE| 开头，| 分隔的 key:value 格式：
 *     MAC — MAC 地址（大写 hex，带冒号）
 *     RSSI— 信号强度
 *     AT  — 地址类型 (0=public, 1=random)
 *     NM  — 设备名称（可选）
 *     MF  — 厂商数据 HEX（可选）
 *     SV  — Service UUID（可选）
 *
 *   扫描周期：
 *     $SCAN_START|DURATION:5    — 扫描开始
 *     $SCAN_END|TOTAL:85|MATCHED:17 — 扫描结束，TOTAL=总包数, MATCHED=匹配数
 *
 *   ⚠ 零堆分配原则：
 *   onResult() 运行在 BTC_TASK（协议栈任务）中，绝对不能使用
 *   JsonDocument / String / std::string / new / malloc 等动态分配。
 *   全部使用栈 buffer + snprintf，否则堆碎片 → abort() 崩溃。
 */

class BleScanner {
public:
    BleScanner();

    bool begin(ConfigManager* config);

    /** 应用配置中的 BLE 扫描参数（扫描间隔/窗口/类型等） */
    void applyScanParams();

    /** 获取当前是否启用扩展扫描 */
    bool isExtScanEnabled() const { return _extScanEnabled; }

    /** 设置扫描持续时间（秒），默认 5 秒一个周期 */
    void setScanDuration(uint32_t seconds);

    /** 驱动 BLE 扫描周期状态机 */
    void update();

    /** 从环形队列取数据输出到 Serial（在 main loop 末尾调用） */
    void flushOutput();

    void startScan();
    /** 立即停止当前扫描（用于切换扫描模式时） */
    void stopScan();
    size_t getLastResultCount() const;

    /** 非阻塞扫描完成回调 */
    static void scanCompleteCB(BLEScanResults results);

private:
    static BleScanner* g_self;
    ConfigManager* _config;
    BLEScan*       _pBLEScan;
    uint32_t       _scanDuration;
    uint32_t       _scanStartMs;
    bool           _scanning;         // 是否正在扫描中
    bool           _extScanEnabled;   // 是否启用扩展广播扫描
    size_t         _lastCount;
    volatile size_t _packetCount;   // BTC_TASK 中自增，记录本轮实际收到的广播包总数
    volatile size_t _matchedCount;  // BTC_TASK 中自增，记录匹配白名单的包数

    // ---- 无锁环形队列 ----
    char  _txQueue[TX_QUEUE_SIZE][TX_LINE_MAX];
    volatile int _txHead;   // 生产者（BTC_TASK）写入位置
    volatile int _txTail;   // 消费者（loop）读取位置

    void _enqueue(const char* line);

    // ---- BLE 逻辑 ----
    /** 纯栈匹配：不允许 String/std::string/new/malloc */
    bool _matchWhitelist(BLEAdvertisedDevice& device) const;
    /** 纯栈格式化：同上零分配约束 */
    void _outputResult(BLEAdvertisedDevice& device);

    class AdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
    public:
        AdvertisedDeviceCallbacks(BleScanner* parent) : _parent(parent) {}
        void onResult(BLEAdvertisedDevice advertisedDevice) override;
    private:
        BleScanner* _parent;
    };

    /** 处理单条扩展扫描结果（供 ExtScanCallbacks 调用） */
    void _outputExtResult(esp_ble_gap_ext_adv_reprot_t& report);

    /** 扩展扫描结果回调 */
    class ExtScanCallbacks : public BLEExtAdvertisingCallbacks {
    public:
        ExtScanCallbacks(BleScanner* parent) : _parent(parent) {}
        void onResult(esp_ble_gap_ext_adv_reprot_t report) override;
    private:
        BleScanner* _parent;
    };
};

#endif // BLE_SCANNER_H

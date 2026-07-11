#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <Arduino.h>
#include <vector>

/** 默认扫描输出格式（用户前台占位符版本），由 ble_scanner 编译为 printf 格式串 */
extern const char* DEFAULT_SCAN_START_FMT;
extern const char* DEFAULT_SCAN_RESULT_FMT;
extern const char* DEFAULT_SCAN_END_FMT;

/**
 * 设备工作模式
 */
enum class DeviceMode : uint8_t {
    SCANNER    = 0,   // BLE 扫描器
    ADVERTISER = 1,   // BLE 模拟广播
    UART_TRANS = 2    // BLE 串口透传（预留，后续实现）
};

/**
 * 白名单条目 —— 一条过滤规则
 */
struct WhitelistEntry {
    enum Type : uint8_t {
        MAC_ADDR   = 0,  // 按 MAC 地址过滤
        NAME       = 1,  // 按蓝牙广播名（部分匹配）
        MANUF_DATA = 2,  // 按厂商自定义数据 (companyId)
    };

    Type   type;
    String value;        // MAC 地址(大写带冒号 "AA:BB:CC:DD:EE:FF") / 名称子串
    uint16_t companyId;  // 仅 MANUF_DATA 类型有效
    bool   enabled;      // 是否启用

    WhitelistEntry() : type(MAC_ADDR), companyId(0), enabled(true) {}
};

/**
 * BLE 扫描参数配置
 */
struct BleScanParams {
    uint16_t scanInterval = 100;     // 扫描间隔 (0x0004 ~ 0x4000, 单位 0.625ms)
    uint16_t scanWindow   = 99;      // 扫描窗口 (≤ scanInterval, 单位 0.625ms)
    uint8_t  scanType     = 1;       // 0=被动扫描, 1=主动扫描
    bool     scanDuplicate = false;  // true=过滤重复广播, false=不过滤
    uint8_t  ownAddrType  = 0;       // 0=公共地址, 1=随机地址
    uint8_t  scanFilterPolicy = 0;   // 0=接受所有, 1=只接受白名单中的
};

/**
 * BLE 扫描自定义输出格式（用户侧占位符 → 运行时编译为 printf 格式串）
 *
 * 用户在前端输入的格式使用友好占位符：
 *   <duration>  扫描持续时间（仅 scanStart 有效）
 *   <mac>       设备 MAC 地址
 *   <rssi>      RSSI 信号强度
 *   <addr>      地址类型 (0=Public, 1=Random, ...)
 *   <name>      设备广播名
 *   <manuf>     厂商数据 HEX
 *   <uuid>      Service UUID
 *   <total>     本轮扫描总包数（仅 scanEnd 有效）
 *   <matched>   匹配白名单的包数（仅 scanEnd 有效）
 *
 * 默认值（即当前硬编码格式）：
 *   scanStart  = "$SCAN_START|DURATION:<duration>"
 *   scanResult = "$BLE|MAC:<mac>|RSSI:<rssi>|ADDR:<addr>|NAME:<name>|MANUF:<manuf>|UUID:<uuid>"
 *   scanEnd    = "$SCAN_END|TOTAL:<total>|MATCHED:<matched>"
 *
 * 开机时 ble_scanner 会调用 compileFormats() 将这些占位符替换为 %s/%d/%u 等
 * printf 格式串，存入 _fmt* 成员供后续 snprintf 使用。
 */
struct BleScanFormat {
    String scanStartFmt;   // 用户侧格式（含占位符），如 "$SCAN_START|DURATION:<duration>"
    String scanResultFmt;  // 用户侧格式
    String scanEndFmt;     // 用户侧格式
};

/**
 * BLE 模拟广播参数配置
 */
struct BleAdvConfig {
    String  customMac;        // 自定义 MAC 地址 (空=使用默认)
    String  advDataHex;       // 广播数据 HEX 字符串 (如 "02010603034C00")
    String  scanRespHex;      // 扫描响应数据 HEX 字符串
    int8_t  txPower = 0;      // 发射功率 (dBm, 范围 -12 ~ +9)
    uint16_t advIntervalMin = 100;  // 最小广播间隔 (单位 0.625ms, 默认 100≈62.5ms)
    uint16_t advIntervalMax = 100;  // 最大广播间隔 (单位 0.625ms, 默认 100≈62.5ms)
    uint32_t advDuration = 0;       // 广播时长 (秒, 0=持续广播)
    uint8_t  advType = 0;           // 0=ADV_IND, 2=ADV_SCAN_IND, 3=ADV_NONCONN_IND
    uint8_t  channelMap = 7;        // 广播信道位掩码: bit0=CH37(1), bit1=CH38(2), bit2=CH39(4), 默认 7=全部
};

/**
 * 配置管理器
 * - 使用 LittleFS 存储全部配置 JSON
 * - 提供读写接口，WiFi 配置页、BLE 扫描器、BLE 广播器共用
 */
class ConfigManager {
public:
    /** 初始化文件系统，加载配置 */
    bool begin();

    // ===== 设备模式 =====
    DeviceMode getDeviceMode() const;
    void setDeviceMode(DeviceMode mode);

    // ===== 白名单 =====
    const std::vector<WhitelistEntry>& getWhitelist() const;
    bool setWhitelist(const std::vector<WhitelistEntry>& list);
    bool addEntry(const WhitelistEntry& entry);
    bool removeEntry(size_t index);
    bool clearWhitelist();

    // ===== BLE 扫描参数 =====
    BleScanParams getScanParams() const;
    void setScanParams(const BleScanParams& params);

    // ===== BLE 扫描输出格式 =====
    BleScanFormat getScanFormat() const;
    void setScanFormat(const BleScanFormat& fmt);

    // ===== BLE 广播参数 =====
    BleAdvConfig getAdvConfig() const;
    void setAdvConfig(const BleAdvConfig& cfg);

    // ===== JSON 序列化/反序列化 =====
    String configToJson() const;
    bool configFromJson(const String& json);

    // ===== 杂项 =====
    bool isWifiMode() const;
    void setWifiMode(bool wifiMode);
    bool save();

private:
    DeviceMode  _deviceMode = DeviceMode::SCANNER;
    std::vector<WhitelistEntry> _whitelist;
    BleScanParams _scanParams;
    BleScanFormat _scanFormat;
    BleAdvConfig  _advConfig;
    bool _wifiMode = false;

    bool _save();
    bool _load();
    String _configPath() const;
};

#endif // CONFIG_MANAGER_H

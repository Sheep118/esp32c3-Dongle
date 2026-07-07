#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <Arduino.h>
#include <vector>

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
    bool     extScanEnabled = false; // true=启用扩展广播扫描, false=仅经典扫描
};

/**
 * 配置管理器
 * - 使用 LittleFS 存储白名单 + BLE 扫描参数 JSON
 * - 提供读写接口，WiFi 配置页和 BLE 扫描器共用
 */
class ConfigManager {
public:
    /** 初始化文件系统，加载配置 */
    bool begin();

    /** 获取白名单列表 */
    const std::vector<WhitelistEntry>& getWhitelist() const;

    /** 设置白名单列表（会立即保存） */
    bool setWhitelist(const std::vector<WhitelistEntry>& list);

    /** 添加一条白名单条目 */
    bool addEntry(const WhitelistEntry& entry);

    /** 删除指定索引的条目 */
    bool removeEntry(size_t index);

    /** 清空白名单 */
    bool clearWhitelist();

    /** 将白名单导出为 JSON 字符串（用于 Web 页面） */
    String whitelistToJson() const;

    /** 从 JSON 字符串导入白名单（用于 Web 页面提交） */
    bool whitelistFromJson(const String& json);

    /** 获取当前模式（true=WiFi配置模式，false=BLE扫描模式） */
    bool isWifiMode() const;

    /** 设置模式 */
    void setWifiMode(bool wifiMode);

    /** 获取 BLE 扫描参数 */
    BleScanParams getScanParams() const;

    /** 设置 BLE 扫描参数（立即保存） */
    void setScanParams(const BleScanParams& params);

    /** 将当前内存状态保存到 LittleFS */
    bool save();

private:
    std::vector<WhitelistEntry> _whitelist;
    BleScanParams _scanParams;
    bool _wifiMode = false;

    bool _save();
    bool _load();
    String _configPath() const;
};

#endif // CONFIG_MANAGER_H

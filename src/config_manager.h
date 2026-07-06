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
 * 配置管理器
 * - 使用 LittleFS 存储白名单 JSON
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

private:
    std::vector<WhitelistEntry> _whitelist;
    bool _wifiMode = false;

    bool _save();
    bool _load();
    String _configPath() const;
};

#endif // CONFIG_MANAGER_H

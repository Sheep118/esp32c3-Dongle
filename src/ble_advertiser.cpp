#include "ble_advertiser.h"
#include <esp_gap_ble_api.h>
#include <esp_bt_main.h>

// =============== BleAdvertiser ===============

BleAdvertiser::BleAdvertiser()
    : _config(nullptr)
    , _pAdvertising(nullptr)
    , _pServer(nullptr)
    , _advertising(false)
    , _startTime(0)
    , _initialized(false)
{
}

bool BleAdvertiser::begin(ConfigManager* config) {
    _config = config;
    if (!_config) {
        Serial.println("[Adv] FATAL: config pointer is null");
        return false;
    }

    // 从配置中读取广播参数
    const BleAdvConfig& cfg = _config->getAdvConfig();

    // 1. 设置 MAC — 必须在 BLEDevice::init 之前（public地址）
    if (cfg.customMac.length() > 0) {
        _applyCustomMac(cfg.customMac);
    }

    // 2. 初始化 BLE 设备
    BLEDevice::init("BLE-Dongle-Adv");
    {
        esp_power_level_t pwr;
        switch (cfg.txPower) {
            case -12: pwr = ESP_PWR_LVL_N12; break;
            case -9:  pwr = ESP_PWR_LVL_N9;  break;
            case -6:  pwr = ESP_PWR_LVL_N6;  break;
            case -3:  pwr = ESP_PWR_LVL_N3;  break;
            case 0:   pwr = ESP_PWR_LVL_N0;  break;
            case 3:   pwr = ESP_PWR_LVL_P3;  break;
            case 6:   pwr = ESP_PWR_LVL_P6;  break;
            case 9:   pwr = ESP_PWR_LVL_P9;  break;
            default:  pwr = ESP_PWR_LVL_N0;  break;
        }
        BLEDevice::setPower(pwr);
    }

    // 如果 MAC 不是 public (bit 47-46 ≠ 00)，需要设置 own_addr_type 为 RANDOM
    if (cfg.customMac.length() > 0) {
        uint8_t top2 = (cfg.customMac.charAt(cfg.customMac.length()-2) >= 'A' ?
                        (cfg.customMac.charAt(cfg.customMac.length()-2)-'A'+10) :
                        (cfg.customMac.charAt(cfg.customMac.length()-2)-'0')) << 4;
        top2 |= (cfg.customMac.charAt(cfg.customMac.length()-1) >= 'A' ?
                 (cfg.customMac.charAt(cfg.customMac.length()-1)-'A'+10) :
                 (cfg.customMac.charAt(cfg.customMac.length()-1)-'0'));
        if ((top2 & 0xC0) != 0) {
            // Random 地址：设 own_addr_type
            esp_ble_gap_config_local_privacy(true);
            Serial.println("[Adv] own_addr_type set to RANDOM");
        }
    }

    // 3. 不创建 BLEServer — 避免自动塞 Complete Name / TX Power
    _pServer = nullptr;

    // 4. 获取广播对象
    _pAdvertising = BLEDevice::getAdvertising();

    // 5. 应用广播参数
    applyConfig();

    _initialized = true;
    Serial.println("[Adv] Advertiser initialized");
    return true;
}

void BleAdvertiser::applyConfig() {
    if (!_pAdvertising || !_config) return;

    const BleAdvConfig& cfg = _config->getAdvConfig();

    // 停止广播（如果正在广播）
    bool wasAdvertising = _advertising;
    if (wasAdvertising) {
        _pAdvertising->stop();
        _advertising = false;
    }

    // --- 设置广播数据 ---
    BLEAdvertisementData advData = BLEAdvertisementData();

    // 广播类型（0=ADV_TYPE_IND, 1=ADV_TYPE_DIRECT_IND_HIGH, 2=ADV_TYPE_SCAN_IND, 3=ADV_TYPE_NONCONN_IND）
    _pAdvertising->setAdvertisementType(static_cast<esp_ble_adv_type_t>(cfg.advType));

    // 设置原始广播数据（HEX 字符串）
    if (cfg.advDataHex.length() > 0) {
        _setAdvDataFromHex(advData, cfg.advDataHex, false);
    } else {
        // 手动构造 Complete Name AD Structure，避免 setName() 自动附加 TX Power 等
        const char* defName = "BLE-Dongle-Adv";
        size_t nameLen = strlen(defName);
        // AD Structure = Length(1) + Type(1) + Data
        uint8_t buf[2 + nameLen];
        buf[0] = nameLen + 1;   // Length
        buf[1] = 0x09;          // Complete Local Name
        memcpy(buf + 2, defName, nameLen);
        advData.addData(std::string((char*)buf, 2 + nameLen));
    }

    _pAdvertising->setAdvertisementData(advData);

    // 强行清零 include_name / include_txpower
    // BLEAdvertising 构造函数默认 include_name=true, include_txpower=true
    // 即使我们调了 setAdvertisementData()（走 raw 路径），底层可能仍读这些标志
    // 通过间接调用 esp_ble_gap_config_adv_data 清零它们
    {
        esp_ble_adv_data_t zeroData = {};
        zeroData.set_scan_rsp = false;
        zeroData.include_name = false;
        zeroData.include_txpower = false;
        zeroData.appearance = 0;
        zeroData.flag = 0;
        zeroData.min_interval = 0x20;
        zeroData.max_interval = 0x40;
        // 不设 manufacturer / service_data / service_uuid
        ::esp_ble_gap_config_adv_data(&zeroData);
    }

    // --- 设置扫描响应数据 ---
    if (cfg.scanRespHex.length() > 0) {
        BLEAdvertisementData scanRespData = BLEAdvertisementData();
        _setAdvDataFromHex(scanRespData, cfg.scanRespHex, true);
        _pAdvertising->setScanResponseData(scanRespData);
    }

    // --- 设置广播间隔（单位 0.625ms）---
    _pAdvertising->setMinInterval(cfg.advInterval);
    _pAdvertising->setMaxInterval(cfg.advInterval);

    // --- 设置广播时长 ---
    // BLEAdvertising::start() 支持 timeout 参数，但我们用 update() 自行计时
    // 以支持非阻塞运行

    Serial.printf("[Adv] Config applied: interval=%u power=%d type=%d duration=%u\n",
                  cfg.advInterval, cfg.txPower, cfg.advType, cfg.advDuration);

    // 如果之前正在广播，自动恢复
    if (wasAdvertising) {
        startAdvertising();
    }
}

void BleAdvertiser::startAdvertising() {
    if (!_pAdvertising || !_initialized) {
        Serial.println("[Adv] Cannot start: not initialized");
        return;
    }

    const BleAdvConfig& cfg = _config->getAdvConfig();

    // 非阻塞方式启动广播
    _pAdvertising->start();
    _advertising = true;
    _startTime = millis();

    Serial.printf("[Adv] Broadcasting started (duration %us)\n", cfg.advDuration);
}

void BleAdvertiser::stopAdvertising() {
    if (_pAdvertising && _advertising) {
        _pAdvertising->stop();
        _advertising = false;
        Serial.println("[Adv] Broadcasting stopped");
    }
}

void BleAdvertiser::update() {
    if (!_advertising || !_config) return;

    const BleAdvConfig& cfg = _config->getAdvConfig();

    // 如果设置了广播时长，到期自动停止
    if (cfg.advDuration > 0) {
        uint32_t elapsed = (millis() - _startTime) / 1000;
        if (elapsed >= cfg.advDuration) {
            Serial.printf("[Adv] Duration reached (%us), stopping\n", cfg.advDuration);
            stopAdvertising();
        }
    }
}

// =============== 辅助方法 ===============

bool BleAdvertiser::_applyCustomMac(const String& macStr) {
    uint8_t mac[6];
    int parsed = sscanf(macStr.c_str(), "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
                        &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]);
    if (parsed != 6) {
        Serial.printf("[Adv] Invalid MAC format: %s\n", macStr.c_str());
        return false;
    }
    // 最高字节最后 2 位 (bit1-bit0): 00=public, 10/01/11=random
    bool isPublic = ((mac[5] & 0xC0) == 0);
    if (isPublic) {
        // 校验单播 (bit0=0)
        if (mac[0] & 0x01) {
            Serial.printf("[Adv] MAC %s bit0=1 (multicast), rejected. Set first byte to even value.\n", macStr.c_str());
            return false;
        }
        esp_err_t ret = esp_base_mac_addr_set(mac);
        if (ret != ESP_OK) {
            Serial.printf("[Adv] Failed set public MAC: %d (ESP_ERR=0x%X). Is bit0=0?\n", ret, ret);
            return false;
        }
        Serial.printf("[Adv] Public MAC set: %s\n", macStr.c_str());
    } else {
        esp_err_t ret = esp_ble_gap_set_rand_addr(mac);
        if (ret != ESP_OK) {
            Serial.printf("[Adv] Failed set random addr: %d\n", ret);
            return false;
        }
        Serial.printf("[Adv] Random MAC set: %s\n", macStr.c_str());
    }
    return true;
}

bool BleAdvertiser::_setAdvDataFromHex(BLEAdvertisementData& advData, const String& hexStr, bool isScanResp) {
    // 移除所有空格和冒号
    String clean = hexStr;
    clean.replace(" ", "");
    clean.replace(":", "");
    clean.replace("-", "");

    if (clean.length() == 0 || clean.length() % 2 != 0) {
        Serial.printf("[Adv] Invalid HEX data length: %d\n", clean.length());
        return false;
    }

    size_t len = clean.length() / 2;
    uint8_t* data = new uint8_t[len];
    for (size_t i = 0; i < len; i++) {
        char byteStr[3] = { clean[i * 2], clean[i * 2 + 1], '\0' };
        data[i] = strtol(byteStr, nullptr, 16);
    }

    if (isScanResp) {
        advData.addData(std::string((char*)data, len));
    } else {
        advData.addData(std::string((char*)data, len));
    }

    delete[] data;
    return true;
}

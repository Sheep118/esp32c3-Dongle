#include "raw_ble_advertiser.h"

#define LOG_TAG "RawAdv"
#include "log.h"

RawBleAdvertiser* RawBleAdvertiser::s_instance = nullptr;

RawBleAdvertiser::RawBleAdvertiser() {
    _advParams.adv_int_min = 0x20; // 0.625*32= 20ms
    _advParams.adv_int_max = 0x40; // 0.625*64= 40ms
    _advParams.adv_type = ADV_TYPE_IND;
    _advParams.own_addr_type = BLE_ADDR_TYPE_PUBLIC;
    _advParams.channel_map = ADV_CHNL_ALL;
    _advParams.adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY;
    _advParams.peer_addr_type = BLE_ADDR_TYPE_PUBLIC;
}

bool RawBleAdvertiser::begin(const char* deviceName) {
    _deviceName = deviceName ? deviceName : "";
    BLEDevice::init(_deviceName.c_str());
    s_instance = this;
    _initialized = true;
    return true;
}

bool RawBleAdvertiser::begin(ConfigManager* config) {
    _config = config;
    if (!_config) {
        LOG_ERROR("config pointer is null");
        return false;
    }

    const BleAdvConfig& cfg = _config->getAdvConfig();
    if (cfg.customMac.length() > 0) {
        setCustomMac(cfg.customMac);
    }
    //初始化BLEdevice
    if (!begin()) { //默认参数是 "BLE-Dongle-Adv"
        return false;
    }

    applyConfig();
    return true;
}

void RawBleAdvertiser::applyConfig() {
    if (!_config) {
        return;
    }

    const BleAdvConfig& cfg = _config->getAdvConfig();
    setAdvertisementType(static_cast<esp_ble_adv_type_t>(cfg.advType));
    setAdvertisementIntervals(cfg.advIntervalMin, cfg.advIntervalMax);
    setAdvertisementChannelMap(static_cast<esp_ble_adv_channel_t>(cfg.channelMap));
    setTxPower(cfg.txPower);
    setScanResponseEnabled(cfg.scanRespHex.length() > 0);
    setDuration(cfg.advDuration);

    if (cfg.advDataHex.length() > 0) {
        setAdvertisementDataHex(cfg.advDataHex);
    } else {
        clearAdvertisementData();
    }

    if (cfg.scanRespHex.length() > 0) {
        setScanResponseDataHex(cfg.scanRespHex);
    } else {
        clearScanResponseData();
    }

    if(cfg.advDataHex.length() == 0 && cfg.scanRespHex.length() == 0) { //如果两个数据包都为空，则生成默认广播数据包
        // 构造默认广播数据包: Flags + 设备名 + TX Power + 连接时的偏好的连接间隔
        std::vector<uint8_t> advData = {0x02, 0x01, 0x06}; // Flags
        std::vector<uint8_t> nameData;
        genAdStructByDeviceName(_deviceName, nameData);
        advData.insert(advData.end(), nameData.begin(), nameData.end());
        std::vector<uint8_t> txPowerData;
        genAdStructByTxPower(cfg.txPower, txPowerData);
        advData.insert(advData.end(), txPowerData.begin(), txPowerData.end());
        // std::vector<uint8_t> intervalData;
        // 这里拿广播间隔放在广播数据包中是错误的，因为广播数据包的间隔数据是偏好的连接间隔，所以暂时在默认数据包中去掉
        // genAdStructByInternal(cfg.advInterval, cfg.advInterval, intervalData);
        // advData.insert(advData.end(), intervalData.begin(), intervalData.end());
        setAdvertisementData(advData.data(), advData.size());
    }
}

void RawBleAdvertiser::setDeviceName(const char* deviceName) {
    _deviceName = deviceName ? deviceName : "";
    if (!_deviceName.isEmpty()) {
        esp_ble_gap_set_device_name(_deviceName.c_str());
    }
}

void RawBleAdvertiser::genAdStructByDeviceName(const String& deviceNanme, std::vector<uint8_t>& advData) {
    advData.clear();
    if (deviceNanme.isEmpty()) return;

    size_t nameLen = deviceNanme.length();
    // AD Structure: Length (1) + AD Type (1) + Name bytes
    advData.push_back((uint8_t)(nameLen + 1));  // Length = AD type (1) + name length
    advData.push_back(0x09);                     // AD type: Complete Local Name
    for (size_t i = 0; i < nameLen; i++) {
        advData.push_back((uint8_t)deviceNanme[i]);
    }
}

bool RawBleAdvertiser::setCustomMac(const String& macStr) {
    _customMac = macStr;
    return applyCustomMacFromConfig(macStr);
}

void RawBleAdvertiser::setTxPower(int8_t txPowerDbm) {
    esp_power_level_t powerLevel;
    switch (txPowerDbm) {
    case -12: powerLevel = ESP_PWR_LVL_N12; break;
    case -9:  powerLevel = ESP_PWR_LVL_N9; break;
    case -6:  powerLevel = ESP_PWR_LVL_N6; break;
    case -3:  powerLevel = ESP_PWR_LVL_N3; break;
    case 0:   powerLevel = ESP_PWR_LVL_N0; break;
    case 3:   powerLevel = ESP_PWR_LVL_P3; break;
    case 6:   powerLevel = ESP_PWR_LVL_P6; break;
    case 9:   powerLevel = ESP_PWR_LVL_P9; break;
    default:  powerLevel = ESP_PWR_LVL_N0; break;
    }

    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, powerLevel);
}

void RawBleAdvertiser::genAdStructByTxPower(const int8_t txPower, std::vector<uint8_t>& advData) {
    advData.clear();
    // AD Structure: Length (1) + AD Type (1) + TX Power bytes
    advData.push_back(0x02); // Length of TX Power Level field
    advData.push_back(0x0A); // TX Power Level AD type
    advData.push_back((uint8_t)txPower);
}

void RawBleAdvertiser::setAdvertisementType(esp_ble_adv_type_t type) {
    _advParams.adv_type = type;
}

void RawBleAdvertiser::setAdvertisementIntervals(uint16_t minInterval, uint16_t maxInterval) {
    _advParams.adv_int_min = minInterval;
    _advParams.adv_int_max = maxInterval;
}

void RawBleAdvertiser::setAdvertisementChannelMap(esp_ble_adv_channel_t channelMap) {
    _advParams.channel_map = channelMap;
}

void RawBleAdvertiser::genAdStructByInternal(uint16_t minInterval, uint16_t maxInterval, std::vector<uint8_t>& advData) {
    advData.clear();
    // AD Structure: Length (1) + AD Type (1) + Min Interval (2) + Max Interval (2)
    advData.push_back(0x05); // Length of Slave Connection Interval Range field
    advData.push_back(0x12); // Slave Connection Interval Range AD type
    advData.push_back(minInterval & 0xFF);
    advData.push_back((minInterval >> 8) & 0xFF);
    advData.push_back(maxInterval & 0xFF);
    advData.push_back((maxInterval >> 8) & 0xFF);
}

void RawBleAdvertiser::setScanResponseEnabled(bool enabled) {
    _scanResponseEnabled = enabled;
}

void RawBleAdvertiser::setDuration(uint32_t durationSeconds) {
    _durationSeconds = durationSeconds;
}

bool RawBleAdvertiser::setAdvertisementData(const uint8_t* data, size_t length) {
    if (length == 0) {
        _advertisementData.clear();
        return true;
    }
    _advertisementData.assign(data, data + length);
    return true;
}

bool RawBleAdvertiser::setScanResponseData(const uint8_t* data, size_t length) {
    if (length == 0) {
        _scanResponseData.clear();
        return true;
    }
    _scanResponseData.assign(data, data + length);
    return true;
}

bool RawBleAdvertiser::setAdvertisementDataHex(const String& hex) {
    return parseHex(hex, _advertisementData);
}

bool RawBleAdvertiser::setScanResponseDataHex(const String& hex) {
    setScanResponseEnabled(!hex.isEmpty()); //如果设置了扫描响应包，则启用扫描响应
    return parseHex(hex, _scanResponseData);
}

void RawBleAdvertiser::clearAdvertisementData() {
    _advertisementData.clear();
}

void RawBleAdvertiser::clearScanResponseData() {
    _scanResponseData.clear();
}

bool RawBleAdvertiser::startAdvertising() {
    if (!_initialized) {
        LOG_ERROR("Cannot start: not initialized");
        return false;
    }

    // ===== 完全对齐 BLEAdvertising::start() 的 fire-and-forget 方式 =====
    // 官方库从不等待 ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT
    // （其 handleGAPEvent 中该事件的信号量已被注释掉）
    // 正确做法：配置数据后直接启播，由 Bluedroid 内部队列保证顺序

    // 1. 配置原始广播数据（异步发送到 Bluedroid 任务队列）
    uint8_t* advPtr = _advertisementData.empty() ? nullptr : const_cast<uint8_t*>(_advertisementData.data());
    esp_err_t err = esp_ble_gap_config_adv_data_raw(advPtr, _advertisementData.size());
    if (err != ESP_OK) {
        LOG_ERROR("esp_ble_gap_config_adv_data_raw failed: %d", err);
        return false;
    }

    // 2. 配置原始扫描响应数据（可选）
    if (_scanResponseEnabled) {
        uint8_t* rspPtr = _scanResponseData.empty() ? nullptr : const_cast<uint8_t*>(_scanResponseData.data());
        err = esp_ble_gap_config_scan_rsp_data_raw(rspPtr, _scanResponseData.size());
        if (err != ESP_OK) {
            LOG_ERROR("esp_ble_gap_config_scan_rsp_data_raw failed: %d", err);
            return false;
        }
    }

    err = esp_ble_gap_start_advertising(&_advParams);
    if (err != ESP_OK) {
        LOG_ERROR("esp_ble_gap_start_advertising failed: %d", err);
        return false;
    }

    _advertising = true;
    _startTime = millis();
    LOG_INFO("Advertising started");
    return true;
}

void RawBleAdvertiser::stopAdvertising() {
    if (!_advertising) {
        return;
    }

    esp_err_t err = esp_ble_gap_stop_advertising();
    if (err != ESP_OK) {
        LOG_ERROR("esp_ble_gap_stop_advertising failed: %d", err);
        return;
    }

    _advertising = false;
    LOG_INFO("Advertising stopped");
}

void RawBleAdvertiser::update() {
    if (!_advertising || _durationSeconds == 0) {
        return;
    }

    const uint32_t elapsedSeconds = (millis() - _startTime) / 1000;
    if (elapsedSeconds >= _durationSeconds) {
        stopAdvertising();
    }
}



bool RawBleAdvertiser::parseHex(const String& hex, std::vector<uint8_t>& out) const {
    String clean = hex;
    clean.replace(" ", "");
    clean.replace(":", "");
    clean.replace("-", "");

    if (clean.length() == 0 || (clean.length() % 2) != 0) {
        return false;
    }

    out.clear();
    out.reserve(clean.length() / 2);

    for (size_t i = 0; i < clean.length(); i += 2) {
        char byteStr[3] = { clean[i], clean[i + 1], '\0' };
        out.push_back(static_cast<uint8_t>(strtoul(byteStr, nullptr, 16)));
    }

    return true;
}

bool RawBleAdvertiser::applyCustomMacFromConfig(const String& macStr) {
    uint8_t mac[6];
    int parsed = sscanf(macStr.c_str(), "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
                        &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]);
    if (parsed != 6) {
        LOG_ERROR("Invalid MAC format: %s", macStr.c_str());
        return false;
    }

    bool isPublic = ((mac[5] & 0xC0) == 0);
    if (isPublic) {
        // ESP32 MAC 偏移量表:
        //   Wi-Fi STA = base_mac + 0
        //   Wi-Fi AP  = base_mac + 1
        //   BLE       = base_mac + 2   ← 我们要设 BLE 地址，所以 base_mac = 目标 - 2
        //   Ethernet  = base_mac + 3
        uint8_t baseMac[6];
        memcpy(baseMac, mac, 6);
        // 减去 2（带进位借位）
        uint16_t borrow = 2;
        for (int i = 5; i >= 0 && borrow > 0; i--) {
            if (baseMac[i] >= borrow) {
                baseMac[i] -= (uint8_t)borrow;
                borrow = 0;
            } else {
                baseMac[i] -= (uint8_t)borrow;
                borrow = 1;
            }
        }

        if (baseMac[0] & 0x01) {
            LOG_ERROR("MAC %s: base_mac bit0=1 (multicast), rejected", macStr.c_str());
            return false;
        }

        esp_err_t ret = esp_base_mac_addr_set(baseMac);
        if (ret != ESP_OK) {
            LOG_ERROR("Failed set base MAC (for BLE MAC %s): %d", macStr.c_str(), ret);
            return false;
        }
        LOG_INFO("BLE Public MAC set: %s (base_mac: %02X:%02X:%02X:%02X:%02X:%02X)",
                 macStr.c_str(),
                 baseMac[0], baseMac[1], baseMac[2], baseMac[3], baseMac[4], baseMac[5]);
    } else {
        esp_err_t ret = esp_ble_gap_set_rand_addr(mac);
        if (ret != ESP_OK) {
            LOG_ERROR("Failed set random addr: %d", ret);
            return false;
        }

        esp_ble_gap_config_local_privacy(true);
        _advParams.own_addr_type = BLE_ADDR_TYPE_RANDOM;
        LOG_INFO("Random MAC set: %s", macStr.c_str());
    }
    return true;
}

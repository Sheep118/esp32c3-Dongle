#include "raw_ble_advertiser.h"

RawBleAdvertiser* RawBleAdvertiser::s_instance = nullptr;

RawBleAdvertiser::RawBleAdvertiser() {
    _advParams.adv_int_min = 0x20;
    _advParams.adv_int_max = 0x40;
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
        Serial.println("[RawAdv] FATAL: config pointer is null");
        return false;
    }

    const BleAdvConfig& cfg = _config->getAdvConfig();
    if (cfg.customMac.length() > 0) {
        setCustomMac(cfg.customMac);
    }

    if (!begin("BLE-Dongle-Adv")) {
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
    setAdvertisementIntervals(cfg.advInterval, cfg.advInterval);
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
}

void RawBleAdvertiser::setDeviceName(const char* deviceName) {
    _deviceName = deviceName ? deviceName : "";
    if (!_deviceName.isEmpty()) {
        esp_ble_gap_set_device_name(_deviceName.c_str());
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
        Serial.println("[RawAdv] Cannot start: not initialized");
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
        Serial.printf("[RawAdv] esp_ble_gap_config_adv_data_raw failed: %d\n", err);
        return false;
    }

    // 2. 配置原始扫描响应数据（可选）
    if (_scanResponseEnabled) {
        uint8_t* rspPtr = _scanResponseData.empty() ? nullptr : const_cast<uint8_t*>(_scanResponseData.data());
        err = esp_ble_gap_config_scan_rsp_data_raw(rspPtr, _scanResponseData.size());
        if (err != ESP_OK) {
            Serial.printf("[RawAdv] esp_ble_gap_config_scan_rsp_data_raw failed: %d\n", err);
            return false;
        }
    }

    // 3. 立即启动广播 —— 不等待配置完成事件！
    //    Bluedroid 内部使用消息队列，start_advertising 会在 config 完成后才被处理
    err = esp_ble_gap_start_advertising(&_advParams);
    if (err != ESP_OK) {
        Serial.printf("[RawAdv] esp_ble_gap_start_advertising failed: %d\n", err);
        return false;
    }

    _advertising = true;
    _startTime = millis();
    Serial.println("[RawAdv] Advertising started");
    return true;
}

void RawBleAdvertiser::stopAdvertising() {
    if (!_advertising) {
        return;
    }

    esp_err_t err = esp_ble_gap_stop_advertising();
    if (err != ESP_OK) {
        Serial.printf("[RawAdv] esp_ble_gap_stop_advertising failed: %d\n", err);
        return;
    }

    _advertising = false;
    Serial.println("[RawAdv] Advertising stopped");
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
        Serial.printf("[RawAdv] Invalid MAC format: %s\n", macStr.c_str());
        return false;
    }

    bool isPublic = ((mac[5] & 0xC0) == 0);
    if (isPublic) {
        if (mac[0] & 0x01) {
            Serial.printf("[RawAdv] MAC %s bit0=1 (multicast), rejected.\n", macStr.c_str());
            return false;
        }

        esp_err_t ret = esp_base_mac_addr_set(mac);
        if (ret != ESP_OK) {
            Serial.printf("[RawAdv] Failed set public MAC: %d\n", ret);
            return false;
        }
        Serial.printf("[RawAdv] Public MAC set: %s\n", macStr.c_str());
    } else {
        esp_err_t ret = esp_ble_gap_set_rand_addr(mac);
        if (ret != ESP_OK) {
            Serial.printf("[RawAdv] Failed set random addr: %d\n", ret);
            return false;
        }

        esp_ble_gap_config_local_privacy(true);
        _advParams.own_addr_type = BLE_ADDR_TYPE_RANDOM;
        Serial.printf("[RawAdv] Random MAC set: %s\n", macStr.c_str());
    }
    return true;
}

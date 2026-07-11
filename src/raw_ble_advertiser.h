#ifndef RAW_BLE_ADVERTISER_H
#define RAW_BLE_ADVERTISER_H

#include <Arduino.h>
#include <BLEDevice.h>
#include <esp_gap_ble_api.h>
#include <esp_bt.h>
#include <vector>

#include "config_manager.h"

class RawBleAdvertiser {
public:
    RawBleAdvertiser();

    bool begin(const char* deviceName = "BLE-Dongle-Adv");
    bool begin(ConfigManager* config);
    void applyConfig();

    void setDeviceName(const char* deviceName);
    void genAdStructByDeviceName(const String& deviceName, std::vector<uint8_t>& advData);
    bool setCustomMac(const String& macStr);
    void setTxPower(int8_t txPowerDbm);
    void genAdStructByTxPower(const int8_t txPower, std::vector<uint8_t>& advData);
    void setAdvertisementType(esp_ble_adv_type_t type);
    void setAdvertisementIntervals(uint16_t minInterval, uint16_t maxInterval);
    void genAdStructByInternal(uint16_t minInterval, uint16_t maxInterval, std::vector<uint8_t>& advData);
    void setAdvertisementChannelMap(esp_ble_adv_channel_t channelMap);
    void setScanResponseEnabled(bool enabled);
    void setDuration(uint32_t durationSeconds);

    bool setAdvertisementData(const uint8_t* data, size_t length);
    bool setScanResponseData(const uint8_t* data, size_t length);
    bool setAdvertisementDataHex(const String& hex);
    bool setScanResponseDataHex(const String& hex);

    void clearAdvertisementData();
    void clearScanResponseData();

    bool startAdvertising();
    void stopAdvertising();
    void update();

    bool isAdvertising() const { return _advertising; }

private:
    static RawBleAdvertiser* s_instance;

    bool parseHex(const String& hex, std::vector<uint8_t>& out) const;
    bool applyCustomMacFromConfig(const String& macStr);

    ConfigManager* _config = nullptr;
    String _deviceName;
    String _customMac;

    esp_ble_adv_params_t _advParams{};
    std::vector<uint8_t> _advertisementData;
    std::vector<uint8_t> _scanResponseData;

    bool _initialized = false;
    bool _advertising = false;
    bool _scanResponseEnabled = false;

    uint32_t _startTime = 0;
    uint32_t _durationSeconds = 0;
};

#endif // RAW_BLE_ADVERTISER_H
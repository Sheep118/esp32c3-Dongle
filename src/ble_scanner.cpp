#include "ble_scanner.h"
#include <ArduinoJson.h>

// ========== 设备发现回调 ==========
void BleScanner::AdvertisedDeviceCallbacks::onResult(BLEAdvertisedDevice device) {
    _parent->_outputResult(device);
}

// ========== 扫描完成回调（静态） ==========
void BleScanner::scanCompleteCB(BLEScanResults results) {
    // 这个回调仅用于标记 — 实际的循环重新扫描在 update() 中触发
    // 注意: 这个静态回调没有 BleScanner 实例指针，
    // 所以我们用全局标记来处理
    Serial.printf("[BLE] Scan finished, %d devices found\n", results.getCount());
}

// ========== BleScanner ==========
BleScanner::BleScanner()
    : _config(nullptr)
    , _pBLEScan(nullptr)
    , _scanDuration(5)
    , _scanStartMs(0)
    , _scanning(false)
    , _lastCount(0)
{
}

bool BleScanner::begin(ConfigManager* config) {
    _config = config;

    BLEDevice::init("BLE-Dongle");
    _pBLEScan = BLEDevice::getScan();
    _pBLEScan->setAdvertisedDeviceCallbacks(new AdvertisedDeviceCallbacks(this));
    _pBLEScan->setActiveScan(true);   // 主动扫描（可获取设备名）
    _pBLEScan->setInterval(100);
    _pBLEScan->setWindow(99);

    Serial.println("[BLE] Scanner initialized");
    startScan();
    return true;
}

void BleScanner::setScanDuration(uint32_t seconds) {
    _scanDuration = seconds;
}

void BleScanner::startScan() {
    if (_pBLEScan) {
        // 使用回调版 start：duration 秒后自动停止并调用 scanCompleteCB
        _pBLEScan->start(_scanDuration, scanCompleteCB, false);
        _scanning = true;
        _scanStartMs = millis();
        _lastCount = 0;
        Serial.printf("[BLE] Scan started (%u sec)\n", _scanDuration);
    }
}

void BleScanner::update() {
    if (!_scanning || !_pBLEScan) return;

    // 检查扫描是否完成（duration 时间到，蓝牙协议栈自动停止）
    if (millis() - _scanStartMs < (_scanDuration * 1000UL)) {
        return; // 仍在扫描中
    }

    // 扫描已自动结束，获取结果
    _scanning = false;
    _lastCount = _pBLEScan->getResults().getCount();

    // 清除结果并开始下一轮
    _pBLEScan->clearResults();
    startScan();
}

size_t BleScanner::getLastResultCount() const {
    return _lastCount;
}

bool BleScanner::_matchWhitelist(BLEAdvertisedDevice& device) const {
    if (!_config) return true; // 无配置，不过滤

    const auto& whitelist = _config->getWhitelist();
    if (whitelist.empty()) return true; // 白名单为空，输出所有

    String devMac = device.getAddress().toString().c_str();
    devMac.toUpperCase();
    String devName = device.haveName() ? device.getName().c_str() : "";

    // 厂商数据（只取前 2 字节作为 Company ID）
    uint16_t devCompanyId = 0;
    if (device.haveManufacturerData()) {
        String manufStr = device.getManufacturerData().c_str();
        size_t mLen = manufStr.length();
        if (mLen >= 2) {
            devCompanyId = (uint8_t)manufStr[0] | ((uint8_t)manufStr[1] << 8);
        }
    }

    for (const auto& rule : whitelist) {
        if (!rule.enabled) continue;

        switch (rule.type) {
        case WhitelistEntry::MAC_ADDR: {
            String ruleMac = rule.value;
            ruleMac.toUpperCase();
            if (devMac == ruleMac) return true;
            break;
        }
        case WhitelistEntry::NAME:
            if (devName.indexOf(rule.value) >= 0) return true;
            break;
        case WhitelistEntry::MANUF_DATA:
            if (devCompanyId != 0 && devCompanyId == rule.companyId) return true;
            break;
        }
    }
    return false;
}

void BleScanner::_outputResult(BLEAdvertisedDevice& device) {
    // 先过白名单
    if (!_matchWhitelist(device)) return;

    // 用固定 512 字节的 JSON buffer 避免动态分配溢出
    char buffer[512];
    JsonDocument doc;

    doc["type"] = "ble_scan";

    String mac = device.getAddress().toString().c_str();
    mac.toUpperCase();
    doc["mac"]  = mac;
    doc["rssi"] = device.getRSSI();
    doc["addrType"] = static_cast<int>(device.getAddressType());

    if (device.haveName()) {
        doc["name"] = device.getName().c_str();
    }
    if (device.haveManufacturerData()) {
        String manuf = device.getManufacturerData().c_str();
        // 截断超长数据（超过 80 字节 HEX 的部分丢弃）
        size_t maxBytes = 40; // 40 字节原始数据 → 80 字符 HEX
        size_t len = manuf.length();
        if (len > maxBytes) len = maxBytes;

        String hex;
        for (size_t i = 0; i < len; i++) {
            char buf[4];
            snprintf(buf, sizeof(buf), "%02X", (uint8_t)manuf[i]);
            hex += buf;
        }
        if (manuf.length() > maxBytes) {
            hex += "...";
        }
        doc["manufacturerData"] = hex;
    }
    if (device.haveServiceUUID()) {
        doc["serviceUUID"] = device.getServiceUUID().toString().c_str();
    }

    serializeJson(doc, buffer, sizeof(buffer));
    Serial.println(buffer);
}

#include "ble_scanner.h"

// 静态指针，供 scanCompleteCB 使用（静态回调无 this 指针）
static BleScanner* g_self = nullptr;

// ========== 设备发现回调 ==========
void BleScanner::AdvertisedDeviceCallbacks::onResult(BLEAdvertisedDevice device) {
    _parent->_outputResult(device);
}

// ========== 扫描完成回调（静态） ==========
void BleScanner::scanCompleteCB(BLEScanResults results) {
    if (g_self) {
        char buf[48];
        snprintf(buf, sizeof(buf), "$SCAN_END|COUNT:%d", results.getCount());
        g_self->_enqueue(buf);
    }
}

// ========== BleScanner ==========
BleScanner::BleScanner()
    : _config(nullptr)
    , _pBLEScan(nullptr)
    , _scanDuration(5)
    , _scanStartMs(0)
    , _scanning(false)
    , _lastCount(0)
    , _txHead(0)
    , _txTail(0)
{
    g_self = this;
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
        char buf[48];
    snprintf(buf, sizeof(buf), "$SCAN_START|DURATION:%u", _scanDuration);
    _enqueue(buf);
    }
}

void BleScanner::update() {
    if (!_scanning || !_pBLEScan) return;

    // 检查扫描是否完成
    if (millis() - _scanStartMs < (_scanDuration * 1000UL)) {
        return;
    }

    // 扫描已自动结束
    _scanning = false;
    _lastCount = _pBLEScan->getResults().getCount();
    _pBLEScan->clearResults();
    startScan();
}

// ========== 环形队列 ==========
void BleScanner::_enqueue(const char* line) {
    int next = (_txHead + 1) % TX_QUEUE_SIZE;
    if (next == _txTail) {
        return; // 队列满，丢弃
    }
    strncpy(_txQueue[_txHead], line, TX_LINE_MAX - 1);
    _txQueue[_txHead][TX_LINE_MAX - 1] = '\0';
    _txHead = next;
}

void BleScanner::flushOutput() {
    while (_txTail != _txHead) {
        Serial.println(_txQueue[_txTail]);
        _txTail = (_txTail + 1) % TX_QUEUE_SIZE;
    }
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
    std::string devName = device.haveName() ? device.getName().c_str() : "";

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
            if (devName.find(rule.value.c_str()) != std::string::npos) return true;
            break;
        case WhitelistEntry::MANUF_DATA:
            if (devCompanyId != 0 && devCompanyId == rule.companyId) return true;
            break;
        }
    }
    return false;
}

/**
 * 输出格式：$BLE|MAC:XX:XX:XX:XX:XX:XX|RSSI:-42|ADDR:0|NAME:xxx|MANUF:AABBCC|UUID:xxx
 *
 * 注意：此函数在 BTC_TASK 上下文中调用，绝对不能调用 Serial 系列函数。
 * 格式化后通过 _enqueue 压入环形队列，由主 loop 的 flushOutput() 统一输出。
 */
void BleScanner::_outputResult(BLEAdvertisedDevice& device) {
    if (!_matchWhitelist(device)) return;

    char buf[TX_LINE_MAX];
    int pos = 0;

    // 前缀 + MAC
    {
        std::string mac = device.getAddress().toString();
        for (auto& c : mac) c = toupper(c);
        int r = snprintf(buf, sizeof(buf), "$BLE|MAC:%s|RSSI:%d|ADDR:%d",
                         mac.c_str(), device.getRSSI(),
                         static_cast<int>(device.getAddressType()));
        if (r < 0) return;
        pos = (r < (int)sizeof(buf) - 8) ? r : (int)sizeof(buf) - 8;
    }

    // 设备名（注意：getName() 返回临时 String，必须用 std::string 持有一份拷贝）
    if (device.haveName()) {
        std::string devName(device.getName().c_str());
        int r = snprintf(buf + pos, sizeof(buf) - pos, "|NAME:%s", devName.c_str());
        if (r > 0) pos += (r < (int)sizeof(buf) - pos) ? r : (int)sizeof(buf) - pos - 1;
    }

    // 厂商数据 HEX
    if (device.haveManufacturerData()) {
        std::string manuf = device.getManufacturerData();
        int r = snprintf(buf + pos, sizeof(buf) - pos, "|MANUF:");
        if (r > 0) pos += (r < (int)sizeof(buf) - pos) ? r : (int)sizeof(buf) - pos - 1;
        for (size_t i = 0; i < manuf.length(); i++) {
            if ((int)sizeof(buf) - pos < 4) break;
            r = snprintf(buf + pos, sizeof(buf) - pos, "%02X", (uint8_t)manuf[i]);
            if (r < 0) break;
            pos += r;
        }
    }

    // Service UUID
    if (device.haveServiceUUID()) {
        snprintf(buf + pos, sizeof(buf) - pos, "|UUID:%s",
                 device.getServiceUUID().toString().c_str());
    }

    _enqueue(buf);
}

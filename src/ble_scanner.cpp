#include "ble_scanner.h"

// ========== 设备发现回调 ==========
void BleScanner::AdvertisedDeviceCallbacks::onResult(BLEAdvertisedDevice device) {
    _parent->_outputResult(device);
}

// ========== BleScanner ==========
BleScanner* BleScanner::g_self = nullptr;

BleScanner::BleScanner()
    : _config(nullptr)
    , _pBLEScan(nullptr)
    , _scanDuration(5)
    , _scanStartMs(0)
    , _scanning(false)
    , _restartPending(false)
    , _lastCount(0)
    , _packetCount(0)
    , _matchedCount(0)
    , _txHead(0)
    , _txTail(0)
{
    g_self = this;
}

bool BleScanner::begin(ConfigManager* config) {
    _config = config;

    BLEDevice::init("BLE-Dongle");
    _pBLEScan = BLEDevice::getScan();

    // 从配置加载扫描参数（含 setCallbacks 的 wantDuplicates）
    applyScanParams();

    Serial.println("[BLE] Scanner initialized");
    startScan();
    return true;
}

void BleScanner::applyScanParams() {
    if (!_pBLEScan || !_config) return;

    BleScanParams p = _config->getScanParams();

    // wantDuplicates=true → 不过滤重复广播；false → 过滤
    // scanDuplicate 配置选项 user 可设置：0=不过滤(显示所有)，1=过滤重复
    _pBLEScan->setAdvertisedDeviceCallbacks(
        new AdvertisedDeviceCallbacks(this),
        !p.scanDuplicate      // wantDuplicates = true 表示不过滤重复
    );

    _pBLEScan->setActiveScan(p.scanType == 1);
    _pBLEScan->setInterval(p.scanInterval);
    _pBLEScan->setWindow(p.scanWindow);

    Serial.printf("[BLE] Scan params: interval=%u window=%u active=%s dupFilter=%s\n",
                  p.scanInterval, p.scanWindow, p.scanType ? "Y" : "N",
                  p.scanDuplicate ? "ON" : "OFF");
}

void BleScanner::setScanDuration(uint32_t seconds) {
    _scanDuration = seconds;
}

void BleScanner::startScan() {
    if (_pBLEScan) {
        _scanning = true;
        _scanStartMs = millis();
        _lastCount = 0;
        _packetCount = 0;
        _matchedCount = 0;
        // 非阻塞模式启动扫描，完成后回调 scanCompleteCB
        _pBLEScan->start(_scanDuration, scanCompleteCB, false);
        char buf[48];
        snprintf(buf, sizeof(buf), "$SCAN_START|DURATION:%u", _scanDuration);
        _enqueue(buf);
    }
}

void BleScanner::update() {
    if (!_pBLEScan) return;

    // 检查是否有重启待处理（由 scanCompleteCB 设置的标记）
    if (_restartPending) {
        _restartPending = false;
        startScan();
    }
}

void BleScanner::requestRestart() {
    // 可在 BLE 回调上下文中安全调用：仅设置标记，不调任何 BLE API
    _restartPending = true;
}

void BleScanner::scanCompleteCB(BLEScanResults results) {
    BleScanner* self = g_self;
    if (!self) return;

    self->_scanning = false;
    // 使用自增的 _packetCount（实际接收到的广播包数），而非 results.getCount()
    //（results.getCount() 在非阻塞 + wantDuplicates 模式下只统计唯一设备地址，不准确）
    self->_lastCount = self->_packetCount;

    char buf[64];
    snprintf(buf, sizeof(buf), "$SCAN_END|TOTAL:%d|MATCHED:%d",
             self->_packetCount, self->_matchedCount);
    self->_enqueue(buf);

    // ⚠ 不能在此回调中直接调 startScan()（= BLEScan::start()）
    // 因为 start() 内部获取信号量，而回调仍在 BLE 事件上下文中
    // 只能设置标记，让 loop() 中的 update() 择机重启
    self->requestRestart();
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

    // 厂商数据（取前 2 字节，小端序 → Company ID）
    uint16_t devCompanyId = 0;
    if (device.haveManufacturerData()) {
        std::string manufRaw = device.getManufacturerData();
        if (manufRaw.length() >= 2) {
            // BLE 规范中 Company ID 以小端序（LE）传输
            devCompanyId = (uint8_t)manufRaw[0] | ((uint8_t)manufRaw[1] << 8);
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
 * 注意：BTC_TASK 上下文，零堆分配，全部栈内存 snprintf。
 */
void BleScanner::_outputResult(BLEAdvertisedDevice& device) {
    _packetCount++;

    if (!_matchWhitelist(device)) return;
    _matchedCount++;

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

    // 厂商数据 HEX — 不截断，全部输出
    if (device.haveManufacturerData()) {
        std::string manuf = device.getManufacturerData();
        int r = snprintf(buf + pos, sizeof(buf) - pos, "|MANUF:");
        if (r > 0) pos += (r < (int)sizeof(buf) - pos) ? r : (int)sizeof(buf) - pos - 1;
        for (size_t i = 0; i < manuf.length(); i++) {
            if ((int)sizeof(buf) - pos < 4) { pos = sizeof(buf) - 1; break; }
            r = snprintf(buf + pos, sizeof(buf) - pos, "%02X", (uint8_t)manuf[i]);
            if (r < 0) break;
            pos += r;
        }
    }

    // Service UUID
    if (device.haveServiceUUID()) {
        if ((int)sizeof(buf) - pos > 8) {
            snprintf(buf + pos, sizeof(buf) - pos, "|UUID:%s",
                     device.getServiceUUID().toString().c_str());
        }
    }

    _enqueue(buf);
}

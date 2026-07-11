#include "ble_scanner.h"
#include "config_manager.h"

#define LOG_TAG "BLE"
#include "log.h"

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

    // 编译用户格式 → printf 格式串（只做一次）
    compileFormats(_config->getScanFormat());

    LOG_INFO("Scanner initialized");
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

    LOG_INFO("Scan params: interval=%u window=%u active=%s dupFilter=%s",
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

        // 使用编译后的格式输出扫描开始信息；空格式 = 不输出
        if (_fmtScanStart[0] != '\0') {
            char buf[TX_LINE_MAX];
            snprintf(buf, sizeof(buf), _fmtScanStart, _scanDuration);
            _enqueue(buf);
        }
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

    // 使用编译后的格式输出扫描结束信息；空格式 = 不输出
    if (self->_fmtScanEnd[0] != '\0') {
        char buf[TX_LINE_MAX];
        snprintf(buf, sizeof(buf), self->_fmtScanEnd,
                 self->_packetCount, self->_matchedCount);
        self->_enqueue(buf);
    }

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

// ========== 格式编译：用户占位符 → printf 格式串 ==========

static void _replacePlaceholder(String& fmt, const char* placeholder, const char* printfSpec) {
    fmt.replace(placeholder, printfSpec);
}

/**
 * 将用户友好的占位符格式转换为 snprintf 可用的 % 格式串。
 *
 * 占位符映射：
 *   <duration>  → %u       (仅 scanStart)
 *   <mac>       → %s       (仅 scanResult)
 *   <rssi>      → %s       (仅 scanResult，预格式化为字符串)
 *   <addr>      → %s       (仅 scanResult)
 *   <name>      → %s       (仅 scanResult)
 *   <manuf>     → %s       (仅 scanResult)
 *   <uuid>      → %s       (仅 scanResult)
 *   <total>     → %d       (仅 scanEnd)
 *   <matched>   → %d       (仅 scanEnd)
 *
 * 输入空字符串 → 输出空字符串（表示不打印该行）
 */
static void _compileOneFormat(const String& userFmt, char* outBuf, size_t outSize,
                               const char** placeholders, const char** printfSpecs,
                               size_t numPairs) {
    if (userFmt.isEmpty()) {
        outBuf[0] = '\0';
        return;
    }
    String compiled = userFmt;
    for (size_t i = 0; i < numPairs; i++) {
        _replacePlaceholder(compiled, placeholders[i], printfSpecs[i]);
    }
    strncpy(outBuf, compiled.c_str(), outSize - 1);
    outBuf[outSize - 1] = '\0';
}

void BleScanner::compileFormats(const BleScanFormat& fmt) {
    // ---- scanStart: 只允许 <duration> ----
    {
        const char* placeholders[] = {"<duration>"};
        const char* specs[]        = {"%1$u"};
        _compileOneFormat(fmt.scanStartFmt, _fmtScanStart, sizeof(_fmtScanStart),
                          placeholders, specs, 1);
    }

    // ---- scanResult: <mac> <rssi> <addr> <name> <manuf> <uuid> ----
    // 使用 POSIX 位置参数 %n$s，确保用户跳过某个占位符时参数映射不乱。
    {
        const char* placeholders[] = {"<mac>", "<rssi>", "<addr>", "<name>", "<manuf>", "<uuid>"};
        const char* specs[]        = {"%1$s",  "%2$s",   "%3$s",  "%4$s",  "%5$s",   "%6$s"};
        _compileOneFormat(fmt.scanResultFmt, _fmtScanResult, sizeof(_fmtScanResult),
                          placeholders, specs, 6);
    }

    // ---- scanEnd: <total> <matched> ----
    {
        const char* placeholders[] = {"<total>", "<matched>"};
        const char* specs[]        = {"%1$d",    "%2$d"};
        _compileOneFormat(fmt.scanEndFmt, _fmtScanEnd, sizeof(_fmtScanEnd),
                          placeholders, specs, 2);
    }

    LOG_INFO("Scan format compiled:");
    LOG_INFO("  start:  \"%s\"", _fmtScanStart[0] ? _fmtScanStart : "(empty → no output)");
    LOG_INFO("  result: \"%s\"", _fmtScanResult[0] ? _fmtScanResult : "(empty → no output)");
    LOG_INFO("  end:    \"%s\"", _fmtScanEnd[0] ? _fmtScanEnd : "(empty → no output)");
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
 * 输出格式：由编译后的 _fmtScanResult 决定（默认 = "$BLE|MAC:%s|RSSI:%d|ADDR:%d|NAME:%s|MANUF:%s|UUID:%s"）
 *
 * 注意：BTC_TASK 上下文，零堆分配，全部栈内存 snprintf。
 * 空格式 = 不输出任何扫描结果行。
 */
void BleScanner::_outputResult(BLEAdvertisedDevice& device) {
    _packetCount++;

    if (!_matchWhitelist(device)) return;
    _matchedCount++;

    // 空格式 → 不输出
    if (_fmtScanResult[0] == '\0') return;

    // 准备各字段的值字符串
    char macStr[18]  = "";
    char rssiStr[8]  = "";
    char addrStr[4]  = "";
    char nameStr[64] = "";
    char manufStr[256] = "";
    char uuidStr[48] = "";

    // MAC
    {
        std::string mac = device.getAddress().toString();
        for (auto& c : mac) c = toupper(c);
        snprintf(macStr, sizeof(macStr), "%s", mac.c_str());
    }
    // RSSI
    snprintf(rssiStr, sizeof(rssiStr), "%d", device.getRSSI());
    // Address type
    snprintf(addrStr, sizeof(addrStr), "%d", static_cast<int>(device.getAddressType()));
    // 设备名
    if (device.haveName()) {
        snprintf(nameStr, sizeof(nameStr), "%s", device.getName().c_str());
    }
    // 厂商数据 HEX
    if (device.haveManufacturerData()) {
        std::string manuf = device.getManufacturerData();
        size_t off = 0;
        for (size_t i = 0; i < manuf.length() && off < sizeof(manufStr) - 3; i++) {
            off += snprintf(manufStr + off, sizeof(manufStr) - off, "%02X", (uint8_t)manuf[i]);
        }
    }
    // Service UUID
    if (device.haveServiceUUID()) {
        snprintf(uuidStr, sizeof(uuidStr), "%s", device.getServiceUUID().toString().c_str());
    }

    char buf[TX_LINE_MAX];
    snprintf(buf, sizeof(buf), _fmtScanResult,
             macStr, rssiStr, addrStr, nameStr, manufStr, uuidStr);

    _enqueue(buf);
}

#include "ble_scanner.h"

// ========== 设备发现回调 ==========
void BleScanner::AdvertisedDeviceCallbacks::onResult(BLEAdvertisedDevice device) {
    _parent->_outputResult(device);
}

// ========== 扩展扫描回调 ==========
void BleScanner::ExtScanCallbacks::onResult(esp_ble_gap_ext_adv_reprot_t report) {
    if (_parent) {
        _parent->_outputExtResult(report);
    }
}

// ========== BleScanner ==========
BleScanner* BleScanner::g_self = nullptr;

BleScanner::BleScanner()
    : _config(nullptr)
    , _pBLEScan(nullptr)
    , _scanDuration(5)
    , _scanStartMs(0)
    , _scanning(false)
    , _lastCount(0)
    , _packetCount(0)
    , _matchedCount(0)
    , _extScanEnabled(false)
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

    // 注册扩展扫描回调
    _pBLEScan->setExtendedScanCallback(new ExtScanCallbacks(this));

    _pBLEScan->setActiveScan(p.scanType == 1);
    _pBLEScan->setInterval(p.scanInterval);
    _pBLEScan->setWindow(p.scanWindow);

    Serial.printf("[BLE] Scan params: interval=%u window=%u active=%s dupFilter=%s%s\n",
                  p.scanInterval, p.scanWindow, p.scanType ? "Y" : "N",
                  p.scanDuplicate ? "ON" : "OFF",
                  p.extScanEnabled ? " EXT" : "");

    // 更新扩展扫描开关
    _extScanEnabled = p.extScanEnabled;
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

        BleScanParams p = _config ? _config->getScanParams() : BleScanParams();

        if (p.extScanEnabled) {
            // == 扩展扫描（BLE 5.0 Extended Advertising）==
            esp_ble_ext_scan_params_t ext_params = {};
            ext_params.own_addr_type = BLE_ADDR_TYPE_PUBLIC;
            ext_params.filter_policy = BLE_SCAN_FILTER_ALLOW_ALL;
            ext_params.scan_duplicate = p.scanDuplicate ? BLE_SCAN_DUPLICATE_ENABLE : BLE_SCAN_DUPLICATE_DISABLE;
            ext_params.cfg_mask = ESP_BLE_GAP_EXT_SCAN_CFG_UNCODE_MASK;
            ext_params.uncoded_cfg.scan_type = p.scanType ? BLE_SCAN_TYPE_ACTIVE : BLE_SCAN_TYPE_PASSIVE;
            ext_params.uncoded_cfg.scan_interval = p.scanInterval;
            ext_params.uncoded_cfg.scan_window = p.scanWindow;
            ext_params.coded_cfg = ext_params.uncoded_cfg;

            if (esp_ble_gap_set_ext_scan_params(&ext_params) == ESP_OK) {
                esp_ble_gap_start_ext_scan(_scanDuration, 0);
            }
            _enqueue("$SCAN_START|DURATION:5|EXT:1");
        } else {
            // == 经典扫描 ==
            _pBLEScan->start(_scanDuration, scanCompleteCB, false);
            _enqueue("$SCAN_START|DURATION:5|EXT:0");
        }
    }
}

void BleScanner::update() {
    if (!_scanning || !_pBLEScan) return;

    // 非阻塞扫描：start() 已启动扫描，等待 scanCompleteCB 回调
    // update() 只需要处理环形队列输出
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

    self->_pBLEScan->clearResults();

    // 立即开始下一轮（间隔仅微秒级）
    self->startScan();
}

// ========== 扩展扫描结果输出（原始 payload 模式）==========
/**
 * 扩展广播回调传出的是原始 payload + 元数据（不含 BLEAdvertisedDevice 对象），
 * 需要用 esp_ble_gap_ext_adv_report_t 中的字段构造输出。
 *
 * 注意：此函数同样在 BLE 协议栈任务中调用，不能使用 Serial / malloc / String。
 */
void BleScanner::_outputExtResult(esp_ble_gap_ext_adv_reprot_t& report) {
    _packetCount++;

    // 从 raw payload 中提取厂商数据的前 2 字节作为 companyId（小端序）
    uint16_t companyId = 0;
    if (report.adv_data_len >= 2) {
        companyId = report.adv_data[0] | (report.adv_data[1] << 8);
    }

    // 白名单匹配（仅 MAC + companyId）
    if (_config) {
        const auto& wl = _config->getWhitelist();
        if (!wl.empty()) {
            bool matched = false;
            for (const auto& rule : wl) {
                if (!rule.enabled) continue;
                if (rule.type == WhitelistEntry::MANUF_DATA && companyId == rule.companyId) {
                    matched = true;
                    break;
                }
                // MAC 匹配太复杂（需要实时转 hex），先用 companyId
            }
            if (!matched) return;
        }
    }

    _matchedCount++;

    char buf[TX_LINE_MAX];
    int pos = 0;

    // MAC 地址 hex
    {
        char mac[18];
        snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
                 report.addr[0], report.addr[1], report.addr[2],
                 report.addr[3], report.addr[4], report.addr[5]);
        int r = snprintf(buf, sizeof(buf), "$BLE|MAC:%s|RSSI:%d|AT:%d",
                         mac, report.rssi, report.addr_type);
        if (r < 0) return;
        pos = (r < (int)sizeof(buf)) ? r : (int)sizeof(buf) - 1;
    }

    // 厂商数据 HEX（完整输出，不截断）
    if (report.adv_data_len > 0) {
        int r = snprintf(buf + pos, sizeof(buf) - pos, "|MF:");
        if (r > 0) pos = (pos + r < (int)sizeof(buf)) ? pos + r : (int)sizeof(buf) - 1;
        for (int i = 0; i < report.adv_data_len; i++) {
            if ((int)sizeof(buf) - pos < 4) {
                if ((int)sizeof(buf) - pos > 3) {
                    memcpy(buf + pos, "...", 3);
                    pos += 3;
                }
                break;
            }
            r = snprintf(buf + pos, sizeof(buf) - pos, "%02X", report.adv_data[i]);
            if (r < 0) break;
            pos += r;
        }
    }

    _enqueue(buf);
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
 * 注意：此函数在 BTC_TASK 上下文中调用，绝对不能调用 Serial 系列函数。
 * 格式化后通过 _enqueue 压入环形队列，由主 loop 的 flushOutput() 统一输出。
 */
void BleScanner::_outputResult(BLEAdvertisedDevice& device) {
    // 自增计数器：记录本轮实际收到的广播包总数
    _packetCount++;

    if (!_matchWhitelist(device)) return;

    // 匹配白名单，自增匹配计数器
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

    // 厂商数据 HEX
    if (device.haveManufacturerData()) {
        std::string manuf = device.getManufacturerData();
        int r = snprintf(buf + pos, sizeof(buf) - pos, "|MANUF:");
        if (r > 0) pos += (r < (int)sizeof(buf) - pos) ? r : (int)sizeof(buf) - pos - 1;
        for (size_t i = 0; i < manuf.length(); i++) {
            if ((int)sizeof(buf) - pos < 4) {
                // 剩余空间写不下了，截断并加省略标记
                memcpy(buf + pos, "...", 3);
                pos += 3;
                break;
            }
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

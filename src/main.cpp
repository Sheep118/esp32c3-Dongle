/**
 * ============================================================================
 *  BLE Dongle — ESP32-C3
 *  功能：BLE 扫描器 / BLE 模拟广播 / BLE 串口透传
 *  配置：Wi-Fi AP 热点 Web 页面配置
 * ============================================================================
 *
 *  模式切换（开机时检测按键）：
 *    - 按住按键上电 → Wi-Fi 配置模式（AP 热点）
 *    - 不按按键上电 → 根据配置的 deviceMode 运行（扫描器/广播器/串口透传）
 *
 *  引脚定义：
 *    - LED1 (状态灯): GPIO12
 *    - LED2 (数据灯): GPIO13（串口透传收发数据时闪烁）
 *    - 按键: GPIO9
 *
 *  串口输出 (USB-CDC, 115200)：
 *    - 扫描模式：$ 前缀格式输出 BLE 设备数据
 *    - 日志信息：[模块] 前缀输出
 * ============================================================================
 */

#include <Arduino.h>
#define LOG_TAG "Main"
#include "log.h"
#include "led.h"
#include "button.h"
#include "config_manager.h"
#include "ble_scanner.h"
#include "raw_ble_advertiser.h"
#include "wifi_config.h"

// ==================== 全局对象 ====================
Led         ledStatus(PIN_LED_STATUS, LED_ACTIVE_HIGH);   // 状态指示灯
Led         ledError(PIN_LED_DATA, LED_ACTIVE_HIGH);      // 数据指示灯（预留）
Button      btnBoot(PIN_BUTTON, BUTTON_ACTIVE_LOW);
ConfigManager config;
BleScanner  bleScanner;
RawBleAdvertiser bleAdvertiser;
WifiConfigServer wifiServer;

// ==================== 运行时模式 ====================
enum class RunMode {
    BOOT_CHECK,    // 开机检测（检测按键状态）
    BLE_SCAN,      // BLE 扫描模式
    BLE_ADV,       // BLE 模拟广播模式
    WIFI_CONFIG    // Wi-Fi 配置模式
};

static RunMode gMode = RunMode::BOOT_CHECK;

// ==================== 前向声明 ====================
void setupBleScanMode();
void setupBleAdvMode();
void setupWifiConfigMode();
void loopBleScanMode();
void loopBleAdvMode();
void loopWifiConfigMode();
void onButtonEvent(ButtonEvent evt);
#if 1
// ==================== 初始化 ====================
void setup() {
    Serial.begin(115200);
    for (int i = 0; i < 10; i++) {
        if (Serial) break;
        delay(50);
    }
    Serial.println("\r\n==========================================");
    Serial.println("  BLE Dongle v2.0 — ESP32-C3");
    Serial.println("==========================================");

    // 1. LED 初始状态：自检中
    ledStatus.setMode(LedMode::ON);
    ledError.setMode(LedMode::OFF);

    // 2. 文件系统 + 配置
    if (!config.begin()) {
        LOG_ERROR("Config manager init failed!");
    }

    // 3. 按键
    btnBoot.onEvent(onButtonEvent);

    // 4. 开机检测（2 秒按键窗口）
    LOG_INFO("Hold button within %us for WiFi Config mode...", (BOOT_WINDOW_MS / 1000));
    ledStatus.setMode(LedMode::ON);

    const uint32_t BOOT_WINDOW_MS_VAL = BOOT_WINDOW_MS;
    uint32_t checkStart = millis();
    bool buttonWasPressed = false;

    while (millis() - checkStart < BOOT_WINDOW_MS_VAL) {
        btnBoot.update();
        if (btnBoot.isPressed()) {
            buttonWasPressed = true;
        }
        delay(10);
    }

    if (buttonWasPressed) {
        // 按键按下 → Wi-Fi 配置模式（不论配置中的 deviceMode）
        LOG_INFO(">>> Button pressed -> Wi-Fi Config Mode");
        gMode = RunMode::WIFI_CONFIG;
        config.setWifiMode(true);
        setupWifiConfigMode();
    } else {
        // 未按键 → 根据配置中的 deviceMode 运行
        config.setWifiMode(false);
        DeviceMode devMode = config.getDeviceMode();
        switch (devMode) {
        case DeviceMode::ADVERTISER:
            LOG_INFO(">>> Device mode: BLE Advertiser");
            gMode = RunMode::BLE_ADV;
            setupBleAdvMode();
            break;
        case DeviceMode::UART_TRANS:
            LOG_INFO(">>> Device mode: BLE UART (not yet implemented)");
            gMode = RunMode::BLE_SCAN;
            setupBleScanMode();
            break;
        case DeviceMode::SCANNER:
        default:
            LOG_INFO(">>> Device mode: BLE Scanner");
            gMode = RunMode::BLE_SCAN;
            setupBleScanMode();
            break;
        }
    }

    LOG_INFO("Setup complete");
}

// ==================== BLE 扫描模式 ====================
void setupBleScanMode() {
    ledStatus.setMode(LedMode::BLINK, LED_SCAN_BLINK_MS);
    if (!bleScanner.begin(&config)) {
        LOG_ERROR("BLE scanner init failed!");
        ledStatus.setMode(LedMode::OFF);
    }
}

// ==================== BLE 模拟广播模式 ====================
void setupBleAdvMode() {
    ledStatus.setMode(LedMode::DOUBLE_BLINK, LED_ADV_DB_FLASH_MS, LED_ADV_DB_GAP_MS);
    if (!bleAdvertiser.begin(&config)) {
        LOG_ERROR("BLE advertiser init failed!");
        ledStatus.setMode(LedMode::OFF);
        return;
    }
    bleAdvertiser.startAdvertising();
}

// ==================== Wi-Fi 配置模式 ====================
void setupWifiConfigMode() {
    ledStatus.setMode(LedMode::BLINK, LED_WIFI_SLOW_BLINK_MS);
    wifiServer.setBleScanner(&bleScanner);
    if (!wifiServer.begin(&config)) {
        LOG_ERROR("WiFi config server init failed!");
        ledStatus.setMode(LedMode::OFF);
    }
}

// ==================== 主循环 ====================
void loop() {
    // 1. 底层模块刷新
    ledStatus.update();
    ledError.update();
    btnBoot.update();

    // 2. 模式分发
    switch (gMode) {
    case RunMode::BLE_SCAN:
        loopBleScanMode();
        break;
    case RunMode::BLE_ADV:
        loopBleAdvMode();
        break;
    case RunMode::WIFI_CONFIG:
        loopWifiConfigMode();
        break;
    default:
        break;
    }

    // 3. 统一输出 BLE 队列数据
    bleScanner.flushOutput();
}

void loopBleScanMode() {
    bleScanner.update();
}

void loopBleAdvMode() {
    bleAdvertiser.update();
}

void loopWifiConfigMode() {
    wifiServer.update();

    static int lastClients = -1;
    int clients = wifiServer.getClientCount();
    if (clients != lastClients) {
        lastClients = clients;
        if (clients > 0) {
            ledStatus.setMode(LedMode::ON);
            LOG_INFO("WiFi client connected (%d)", clients);
        } else {
            ledStatus.setMode(LedMode::BLINK, 500);
        }
    }
}

// ==================== 按键事件回调 ====================
void onButtonEvent(ButtonEvent evt) {
    switch (evt) {
    case ButtonEvent::PRESSED:
        LOG_INFO("Pressed");
        break;
    case ButtonEvent::RELEASED:
        LOG_INFO("Released");
        break;
    case ButtonEvent::LONG_PRESS:
        LOG_INFO("Long press detected");
        if (gMode == RunMode::BLE_SCAN) {
            bleScanner.startScan();
        }
        break;
    }
}

#else

#include "raw_ble_advertiser.h"
RawBleAdvertiser advertiser;

void setup() {
    Serial.begin(115200);
    for (int i = 0; i < 10; i++) {
        if (Serial) break;
        delay(50);
    }
    Serial.println("\n\n==========================================");
    Serial.println("  BLE Dongle v2.0 — ESP32-C3");
    Serial.println("==========================================");

    advertiser.setCustomMac("18:00:00:00:00:28");
    advertiser.begin("BLE-Adv");
    advertiser.setAdvertisementType(ADV_TYPE_IND);
    advertiser.setAdvertisementIntervals(0x20, 0x40);

    // advertiser.setAdvertisementDataHex("02010606097368656570");
    // advertiser.setScanResponseDataHex("03FF1122");
    // advertiser.setScanResponseEnabled(true);
    advertiser.startAdvertising();
    ledStatus.setMode(LedMode::DOUBLE_BLINK, 100, 1500);
}

void loop(){
    advertiser.update();
    ledStatus.update();
    // Serial.println("adversting....");
    // delay(1000);
}
#endif
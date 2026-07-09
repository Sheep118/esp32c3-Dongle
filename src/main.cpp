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
#include "led.h"
#include "button.h"
#include "config_manager.h"
#include "ble_scanner.h"
#include "ble_advertiser.h"
#include "wifi_config.h"

// ==================== 全局对象 ====================
Led         ledStatus(LED_BUILTIN_1_PIN, LED_ACTIVE_HIGH);   // 状态指示灯 (GPIO12)
Led         ledError(LED_BUILTIN_2_PIN, LED_ACTIVE_HIGH);    // 数据/错误灯 (GPIO13)
Button      btnBoot(BUTTON_PIN, BUTTON_ACTIVE_LOW);
ConfigManager config;
BleScanner  bleScanner;
BleAdvertiser bleAdvertiser;
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
#if 0
// ==================== 初始化 ====================
void setup() {
    Serial.begin(115200);
    for (int i = 0; i < 10; i++) {
        if (Serial) break;
        delay(50);
    }
    Serial.println("\n\n==========================================");
    Serial.println("  BLE Dongle v2.0 — ESP32-C3");
    Serial.println("==========================================");

    // 1. LED 初始状态：自检中
    ledStatus.setMode(LedMode::ON);
    ledError.setMode(LedMode::OFF);

    // 2. 文件系统 + 配置
    if (!config.begin()) {
        Serial.println("[Main] FATAL: Config manager init failed!");
    }

    // 3. 按键
    btnBoot.onEvent(onButtonEvent);

    // 4. 开机检测（2 秒按键窗口）
    Serial.println("[Main] Hold button within 2s for WiFi Config mode...");
    ledStatus.setMode(LedMode::ON);

    const uint32_t BOOT_WINDOW_MS = 2000;
    uint32_t checkStart = millis();
    bool buttonWasPressed = false;

    while (millis() - checkStart < BOOT_WINDOW_MS) {
        btnBoot.update();
        if (btnBoot.isPressed()) {
            buttonWasPressed = true;
        }
        delay(10);
    }

    if (buttonWasPressed) {
        // 按键按下 → Wi-Fi 配置模式（不论配置中的 deviceMode）
        Serial.println("[Main] >>> Button pressed -> Wi-Fi Config Mode");
        gMode = RunMode::WIFI_CONFIG;
        config.setWifiMode(true);
        setupWifiConfigMode();
    } else {
        // 未按键 → 根据配置中的 deviceMode 运行
        config.setWifiMode(false);
        DeviceMode devMode = config.getDeviceMode();
        switch (devMode) {
        case DeviceMode::ADVERTISER:
            Serial.println("[Main] >>> Device mode: BLE Advertiser");
            gMode = RunMode::BLE_ADV;
            setupBleAdvMode();
            break;
        case DeviceMode::UART_TRANS:
            Serial.println("[Main] >>> Device mode: BLE UART (not yet implemented)");
            // 当 UART 透传实现后，此处设置 gMode 并调用对应 setup
            // 回退为扫描模式
            gMode = RunMode::BLE_SCAN;
            setupBleScanMode();
            break;
        case DeviceMode::SCANNER:
        default:
            Serial.println("[Main] >>> Device mode: BLE Scanner");
            gMode = RunMode::BLE_SCAN;
            setupBleScanMode();
            break;
        }
    }

    Serial.println("[Main] Setup complete");
}

// ==================== BLE 扫描模式 ====================
void setupBleScanMode() {
    ledStatus.setMode(LedMode::BLINK, 200);
    if (!bleScanner.begin(&config)) {
        Serial.println("[Main] FATAL: BLE scanner init failed!");
        ledStatus.setMode(LedMode::BLINK, 100);
    }
}

// ==================== BLE 模拟广播模式 ====================
void setupBleAdvMode() {
    ledStatus.setMode(LedMode::ON);
    if (!bleAdvertiser.begin(&config)) {
        Serial.println("[Main] FATAL: BLE advertiser init failed!");
        ledStatus.setMode(LedMode::BLINK, 100);
        return;
    }
    bleAdvertiser.startAdvertising();
}

// ==================== Wi-Fi 配置模式 ====================
void setupWifiConfigMode() {
    ledStatus.setMode(LedMode::BLINK, 800);
    wifiServer.setBleScanner(&bleScanner);
    if (!wifiServer.begin(&config)) {
        Serial.println("[Main] FATAL: WiFi config server init failed!");
        ledStatus.setMode(LedMode::BLINK, 100);
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
            Serial.printf("[Main] WiFi client connected (%d)\n", clients);
        } else {
            ledStatus.setMode(LedMode::BLINK, 500);
        }
    }
}

// ==================== 按键事件回调 ====================
void onButtonEvent(ButtonEvent evt) {
    switch (evt) {
    case ButtonEvent::PRESSED:
        Serial.println("[Button] Pressed");
        break;
    case ButtonEvent::RELEASED:
        Serial.println("[Button] Released");
        break;
    case ButtonEvent::LONG_PRESS:
        Serial.println("[Button] Long press detected");
        if (gMode == RunMode::BLE_SCAN) {
            bleScanner.startScan();
        }
        break;
    }
}

#endif
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

    advertiser.begin("BLE-Dongle-Adv");
    advertiser.setAdvertisementType(ADV_TYPE_IND);
    advertiser.setAdvertisementIntervals(0x20, 0x40);

    advertiser.setAdvertisementDataHex("02010606097368656570");
    advertiser.setScanResponseDataHex("03FF1122");
    advertiser.setScanResponseEnabled(true);
    advertiser.startAdvertising();
}

void loop(){
    advertiser.update();
    Serial.println("adversting....");
    delay(1000);
}

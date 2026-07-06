/**
 * ============================================================================
 *  BLE Dongle — ESP32-C3
 *  功能：BLE 广播扫描 + 白名单过滤 + Wi-Fi 热点 Web 配置
 * ============================================================================
 *
 *  模式切换（开机时检测按键）：
 *    - 按住按键上电 → Wi-Fi 配置模式（AP 热点，可网页配置白名单）
 *    - 不按按键上电 → BLE 扫描模式（根据白名单过滤并输出到串口）
 *
 *  引脚定义（见各模块头文件顶部宏定义）：
 *    - LED1: GPIO12
 *    - LED2: GPIO13
 *    - 按键: GPIO9  (BUTTON_PIN，可在 button.h 中修改)
 *
 *  串口输出 (USB-CDC)：
 *    - BLE 扫描结果以 JSON 格式逐行输出
 *    - 日志信息以 [模块] 前缀输出
 * ============================================================================
 */

#include <Arduino.h>
#include "led.h"
#include "button.h"
#include "config_manager.h"
#include "ble_scanner.h"
#include "wifi_config.h"

// ==================== 全局对象 ====================
Led         ledStatus(LED_BUILTIN_1_PIN, LED_ACTIVE_HIGH);   // 状态指示灯
Led         ledError(LED_BUILTIN_2_PIN, LED_ACTIVE_HIGH);    // 错误/告警灯
Button      btnBoot(BUTTON_PIN, BUTTON_ACTIVE_LOW);
ConfigManager config;
BleScanner  bleScanner;
WifiConfigServer wifiServer;

// ==================== 模式枚举 ====================
enum class DeviceMode {
    BOOT_CHECK,    // 开机检测（检测按键状态）
    BLE_SCAN,      // BLE 扫描模式
    WIFI_CONFIG    // Wi-Fi 配置模式
};

static DeviceMode gMode = DeviceMode::BOOT_CHECK;

// ==================== 前向声明 ====================
void setupBleScanMode();
void setupWifiConfigMode();
void loopBleScanMode();
void loopWifiConfigMode();
void onButtonEvent(ButtonEvent evt);

// ==================== 初始化 ====================
void setup() {
    Serial.begin(115200);
    // 等待串口就绪（USB-CDC 需要一点时间）
    for (int i = 0; i < 10; i++) {
        if (Serial) break;
        delay(50);
    }
    Serial.println("\n\n==========================================");
    Serial.println("  BLE Dongle v1.0 — ESP32-C3");
    Serial.println("==========================================");

    // 1. 初始化 LED — 数据 LED 暂时不动，状态 LED 常亮表示正在自检
    ledStatus.setMode(LedMode::ON);          // 状态 LED 常亮（指示启动中）
    ledError.setMode(LedMode::OFF);          // 数据 LED 暂不操作

    // 2. 初始化文件系统 + 配置
    if (!config.begin()) {
        Serial.println("[Main] FATAL: Config manager init failed!");
    }

    // 3. 初始化按键
    btnBoot.onEvent(onButtonEvent);

    // 4. 开机检测：状态 LED 常亮 2 秒，在此期间检测按键
    //    - 2 秒内按过按键 → Wi-Fi 配置模式
    //    - 2 秒内没按     → BLE 扫描模式
    Serial.println("[Main] Press button within 2s for WiFi Config mode...");
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
        Serial.println("[Main] >>> Button pressed during boot -> Wi-Fi Config Mode");
        gMode = DeviceMode::WIFI_CONFIG;
        config.setWifiMode(true);
        setupWifiConfigMode();
    } else {
        Serial.println("[Main] >>> No button press -> BLE Scan Mode");
        gMode = DeviceMode::BLE_SCAN;
        config.setWifiMode(false);
        setupBleScanMode();
    }

    Serial.println("[Main] Setup complete");
}

// ==================== BLE 扫描模式初始化 ====================
void setupBleScanMode() {
    // 状态 LED 快闪表示 BLE 扫描中
    ledStatus.setMode(LedMode::BLINK, 200);

    if (!bleScanner.begin(&config)) {
        Serial.println("[Main] FATAL: BLE scanner init failed!");
        ledStatus.setMode(LedMode::BLINK, 100);
    }
}

// ==================== Wi-Fi 配置模式初始化 ====================
void setupWifiConfigMode() {
    // 状态 LED 慢闪表示 Wi-Fi 配置模式
    ledStatus.setMode(LedMode::BLINK, 800);

    if (!wifiServer.begin(&config)) {
        Serial.println("[Main] FATAL: WiFi config server init failed!");
        ledStatus.setMode(LedMode::BLINK, 100);
    }
}

// ==================== 主循环 ====================
void loop() {
    // 1. 异步模块刷新（非阻塞，必须在最前面）
    ledStatus.update();
    ledError.update();
    btnBoot.update();

    // 2. 根据模式分发
    switch (gMode) {
    case DeviceMode::BLE_SCAN:
        loopBleScanMode();
        break;
    case DeviceMode::WIFI_CONFIG:
        loopWifiConfigMode();
        break;
    default:
        break;
    }
}

void loopBleScanMode() {
    bleScanner.update();
}

void loopWifiConfigMode() {
    wifiServer.update();

    // LED 根据客户端连接情况指示
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
        Serial.println("[Button] Released (short press)");
        break;
    case ButtonEvent::LONG_PRESS:
        Serial.println("[Button] Long press detected");
        // 长按可在 BLE 模式下手动触发一次扫描
        if (gMode == DeviceMode::BLE_SCAN) {
            bleScanner.startScan();
        }
        break;
    }
}
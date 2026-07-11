#ifndef LED_H
#define LED_H

#include <Arduino.h>

/*
 * LED 引脚和极性定义在 platformio.ini 的 build_flags 中：
 *   -DPIN_LED_STATUS=12   状态指示灯 GPIO
 *   -DPIN_LED_DATA=13     数据指示灯 GPIO（预留）
 *   -DLED_ACTIVE_HIGH=true 高电平点亮
 *
 * 此处提供默认值以防 build_flags 未定义。
 */
#ifndef PIN_LED_STATUS
#define PIN_LED_STATUS  12
#endif
#ifndef PIN_LED_DATA
#define PIN_LED_DATA    13
#endif
#ifndef LED_ACTIVE_HIGH
#define LED_ACTIVE_HIGH true
#endif

/*
 * 指示灯时序参数默认值（platformio.ini 中可通过 -D 覆盖）：
 *   -DBOOT_WINDOW_MS=2000          开机检测窗口时长
 *   -DLED_SCAN_BLINK_MS=200        扫描模式快闪间隔
 *   -DLED_WIFI_SLOW_BLINK_MS=2000  WiFi 配置模式慢闪间隔
 *   -DLED_ADV_DB_FLASH_MS=100      广播模式双闪快闪间隔
 *   -DLED_ADV_DB_GAP_MS=1500       广播模式双闪长灭间隔
 */
#ifndef BOOT_WINDOW_MS
#define BOOT_WINDOW_MS           2000
#endif
#ifndef LED_SCAN_BLINK_MS
#define LED_SCAN_BLINK_MS        200
#endif
#ifndef LED_WIFI_SLOW_BLINK_MS
#define LED_WIFI_SLOW_BLINK_MS   2000
#endif
#ifndef LED_ADV_DB_FLASH_MS
#define LED_ADV_DB_FLASH_MS      100
#endif
#ifndef LED_ADV_DB_GAP_MS
#define LED_ADV_DB_GAP_MS        1500
#endif

/**
 * LED 模式枚举
 */
enum class LedMode {
    OFF,           // 熄灭
    ON,            // 常亮
    BLINK,         // 等间隔闪烁（亮灭各一半周期）
    DOUBLE_BLINK   // 双闪：快速亮灭两次 → 长灭 → 循环
};

/**
 * LED 异步控制类
 * 使用 millis() 实现非阻塞控制，支持独立设置每个 LED 的模式和闪烁频率。
 */
class Led {
public:
    /**
     * @param pin        GPIO 引脚编号
     * @param activeHigh true=高电平点亮, false=低电平点亮
     */
    Led(uint8_t pin, bool activeHigh = true);

    /**
     * 设置 LED 模式
     * @param mode       LedMode::OFF / ON / BLINK / DOUBLE_BLINK
     * @param blinkMs    闪烁周期（毫秒）：
     *                   - BLINK 模式：亮灭各半，周期 = blinkMs
     *                   - DOUBLE_BLINK 模式：快速双闪中每个亮/灭的时长
     * @param gapMs      仅 DOUBLE_BLINK 模式：双闪结束后长灭的时长（毫秒）
     */
    void setMode(LedMode mode, uint32_t blinkMs = 500, uint32_t gapMs = 2000);

    /** 获取当前模式 */
    LedMode getMode() const;

    /**
     * 必须在主 loop() 中周期性调用，驱动异步状态更新
     */
    void update();

private:
    uint8_t  _pin;
    bool     _activeHigh;
    LedMode  _mode;
    uint32_t _blinkMs;      ///< BLINK 周期 / DOUBLE_BLINK 单次闪间隔
    uint32_t _gapMs;        ///< DOUBLE_BLINK 长灭时长
    uint32_t _lastToggle;
    uint8_t  _dbPhase;      ///< DOUBLE_BLINK 相位: 0=亮1,1=灭1,2=亮2,3=灭2,4=长灭
    int8_t   _dbCount;      ///< DOUBLE_BLINK 当前已完成的闪次数
    bool     _stateOn;       // 当前物理电平状态（已考虑 activeHigh）
};

#endif // LED_H

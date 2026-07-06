#ifndef LED_H
#define LED_H

#include <Arduino.h>

/**
 * LED 引脚定义
 * 板载两个 LED：GPIO12 和 GPIO13，高电平点亮
 */
#define LED_BUILTIN_1_PIN  12
#define LED_BUILTIN_2_PIN  13
#define LED_ACTIVE_HIGH    true

/**
 * LED 模式枚举
 */
enum class LedMode {
    OFF,        // 熄灭
    ON,         // 常亮
    BLINK       // 闪烁
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
     * @param mode       LedMode::OFF / ON / BLINK
     * @param blinkMs    闪烁周期（毫秒），仅在 BLINK 模式下有效。例如 500ms 表示亮 250ms 灭 250ms
     */
    void setMode(LedMode mode, uint32_t blinkMs = 500);

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
    uint32_t _blinkMs;
    uint32_t _lastToggle;
    bool     _stateOn;   // 当前物理电平状态（已考虑 activeHigh）
};

#endif // LED_H

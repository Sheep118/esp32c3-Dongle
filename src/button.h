#ifndef BUTTON_H
#define BUTTON_H

#include <Arduino.h>
#include <functional>

/**
 * 按键引脚定义 —— 自行修改为你实际的 GPIO
 * 默认使用 GPIO 9 (ESP32-C3 通用按键脚，请按实际接线调整)
 */
#define BUTTON_PIN         9
#define BUTTON_ACTIVE_LOW  true   // true = 按下为低电平

/**
 * 按键事件类型
 */
enum class ButtonEvent {
    PRESSED,    // 按下
    RELEASED,   // 释放
    LONG_PRESS  // 长按（超过 longPressMs）
};

/**
 * 按键非阻塞处理模块
 * - 软件消抖
 * - 支持短按 / 长按回调
 */
class Button {
public:
    using Callback = std::function<void(ButtonEvent)>;

    /**
     * @param pin          GPIO 引脚
     * @param activeLow    true=按下为低电平
     * @param debounceMs   消抖时间 (ms)
     * @param longPressMs  长按判定时间 (ms)
     */
    Button(uint8_t pin, bool activeLow = true,
           uint32_t debounceMs = 50, uint32_t longPressMs = 1500);

    /** 注册事件回调 */
    void onEvent(Callback cb);

    /** 必须在主 loop() 中周期性调用 */
    void update();

    /** 读取当前原始电平（true=按下） */
    bool isPressed() const;

private:
    uint8_t  _pin;
    bool     _activeLow;
    uint32_t _debounceMs;
    uint32_t _longPressMs;

    Callback _cb;

    int      _lastRaw;
    bool     _stablePressed;
    uint32_t _lastChangeMs;
    uint32_t _pressStartMs;
    bool     _longPressReported;
};

#endif // BUTTON_H

#ifndef BUTTON_H
#define BUTTON_H

#include <Arduino.h>
#include <functional>

/*
 * 按键引脚和极性定义在 platformio.ini 的 build_flags 中：
 *   -DPIN_BUTTON=9         按键 GPIO
 *   -DBUTTON_ACTIVE_LOW=true 按下为低电平
 *
 * 此处提供默认值以防 build_flags 未定义。
 */
#ifndef PIN_BUTTON
#define PIN_BUTTON         9
#endif
#ifndef BUTTON_ACTIVE_LOW
#define BUTTON_ACTIVE_LOW  true
#endif

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

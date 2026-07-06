#include "button.h"

Button::Button(uint8_t pin, bool activeLow,
               uint32_t debounceMs, uint32_t longPressMs)
    : _pin(pin)
    , _activeLow(activeLow)
    , _debounceMs(debounceMs)
    , _longPressMs(longPressMs)
    , _cb(nullptr)
    , _lastRaw(HIGH)
    , _stablePressed(false)
    , _lastChangeMs(0)
    , _pressStartMs(0)
    , _longPressReported(false)
{
    pinMode(_pin, _activeLow ? INPUT_PULLUP : INPUT_PULLDOWN);
    _lastRaw = digitalRead(_pin);
    _stablePressed = (_lastRaw == (_activeLow ? LOW : HIGH));
}

void Button::onEvent(Callback cb) {
    _cb = cb;
}

bool Button::isPressed() const {
    return _stablePressed;
}

void Button::update() {
    int raw = digitalRead(_pin);
    uint32_t now = millis();

    // 消抖
    if (raw != _lastRaw) {
        _lastChangeMs = now;
        _lastRaw = raw;
    }

    if (now - _lastChangeMs < _debounceMs) {
        return; // 仍在抖动
    }

    bool newPressed = (raw == (_activeLow ? LOW : HIGH));

    // 状态变化
    if (newPressed != _stablePressed) {
        _stablePressed = newPressed;
        if (_stablePressed) {
            _pressStartMs = now;
            _longPressReported = false;
            if (_cb) _cb(ButtonEvent::PRESSED);
        } else {
            // 释放时若未触发过长按，则视为短按
            if (!_longPressReported && _cb) {
                _cb(ButtonEvent::RELEASED);
            }
        }
    }

    // 长按检测
    if (_stablePressed && !_longPressReported) {
        if (now - _pressStartMs >= _longPressMs) {
            _longPressReported = true;
            if (_cb) _cb(ButtonEvent::LONG_PRESS);
        }
    }
}

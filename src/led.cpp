#include "led.h"

Led::Led(uint8_t pin, bool activeHigh)
    : _pin(pin)
    , _activeHigh(activeHigh)
    , _mode(LedMode::OFF)
    , _blinkMs(500)
    , _lastToggle(0)
    , _stateOn(false)
{
    pinMode(_pin, OUTPUT);
    digitalWrite(_pin, _activeHigh ? LOW : HIGH); // 初始熄灭
}

void Led::setMode(LedMode mode, uint32_t blinkMs) {
    _mode    = mode;
    _blinkMs = (blinkMs == 0) ? 500 : blinkMs;
    _lastToggle = millis();

    switch (_mode) {
    case LedMode::ON:
        _stateOn = true;
        digitalWrite(_pin, _activeHigh ? HIGH : LOW);
        break;
    case LedMode::OFF:
        _stateOn = false;
        digitalWrite(_pin, _activeHigh ? LOW : HIGH);
        break;
    case LedMode::BLINK:
        _stateOn = true;
        digitalWrite(_pin, _activeHigh ? HIGH : LOW);
        break;
    }
}

LedMode Led::getMode() const {
    return _mode;
}

void Led::update() {
    if (_mode != LedMode::BLINK) {
        return;
    }

    uint32_t now = millis();
    uint32_t halfCycle = _blinkMs / 2;
    if (halfCycle == 0) halfCycle = 1;

    if (now - _lastToggle >= halfCycle) {
        _lastToggle = now;
        _stateOn = !_stateOn;
        digitalWrite(_pin, _activeHigh ? (_stateOn ? HIGH : LOW) : (_stateOn ? LOW : HIGH));
    }
}

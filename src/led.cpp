#include "led.h"

Led::Led(uint8_t pin, bool activeHigh)
    : _pin(pin)
    , _activeHigh(activeHigh)
    , _mode(LedMode::OFF)
    , _blinkMs(500)
    , _gapMs(2000)
    , _lastToggle(0)
    , _dbPhase(0)
    , _dbCount(0)
    , _stateOn(false)
{
    pinMode(_pin, OUTPUT);
    digitalWrite(_pin, _activeHigh ? LOW : HIGH); // 初始熄灭
}

void Led::setMode(LedMode mode, uint32_t blinkMs, uint32_t gapMs) {
    _mode    = mode;
    _blinkMs = (blinkMs == 0) ? 500 : blinkMs;
    _gapMs   = (gapMs  == 0) ? 2000 : gapMs;
    _lastToggle = millis();
    _dbPhase    = 0;
    _dbCount    = 0;

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
    case LedMode::DOUBLE_BLINK:
        // 双闪序列：亮1→灭1→亮2→灭2→长灭 → 循环
        _dbPhase = 0;
        _dbCount = 0;
        _stateOn = true;
        digitalWrite(_pin, _activeHigh ? HIGH : LOW);
        break;
    }
}

LedMode Led::getMode() const {
    return _mode;
}

void Led::update() {
    if (_mode != LedMode::BLINK && _mode != LedMode::DOUBLE_BLINK) {
        return;
    }

    uint32_t now = millis();

    if (_mode == LedMode::BLINK) {
        uint32_t halfCycle = _blinkMs / 2;
        if (halfCycle == 0) halfCycle = 1;

        if (now - _lastToggle >= halfCycle) {
            _lastToggle = now;
            _stateOn = !_stateOn;
            digitalWrite(_pin, _activeHigh ? (_stateOn ? HIGH : LOW) : (_stateOn ? LOW : HIGH));
        }
        return;
    }

    // ====== DOUBLE_BLINK 模式 ======
    // 相位：0=亮1, 1=灭1, 2=亮2, 3=灭2, 4=长灭
    // 相位 0~3 用 _blinkMs 做间隔；相位 4 用 _gapMs 做间隔

    uint32_t interval = (_dbPhase == 4) ? _gapMs : _blinkMs;

    if (now - _lastToggle >= interval) {
        _lastToggle = now;

        switch (_dbPhase) {
        case 0: // 亮1 → 灭1
            _stateOn = false;
            _dbPhase = 1;
            break;
        case 1: // 灭1 → 亮2
            _stateOn = true;
            _dbPhase = 2;
            break;
        case 2: // 亮2 → 灭2
            _stateOn = false;
            _dbPhase = 3;
            break;
        case 3: // 灭2 → 长灭
            _stateOn = false;
            _dbPhase = 4;
            // 注意：_lastToggle 已更新为 now，下一个 tick 用 _gapMs 判断
            break;
        case 4: // 长灭 → 亮1（重新开始）
            _stateOn = true;
            _dbPhase = 0;
            _dbCount++;
            // _lastToggle 已更新，下一个 tick 用 _blinkMs 判断
            break;
        }

        digitalWrite(_pin, _activeHigh ? (_stateOn ? HIGH : LOW) : (_stateOn ? LOW : HIGH));
    }
}

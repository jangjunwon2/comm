// =========================================================================
// hardware.cpp
// =========================================================================

/**
 * @file hardware.cpp
 * @brief HardwareManager 클래스의 구현입니다.
 * @version 4.1.0
 * @date 2024-06-14
 */
#include "hardware.h"

HardwareManager::HardwareManager() :
    _idButtonState(false), _execButtonState(false),
    _lastIdDebounceTime(0), _lastExecDebounceTime(0),
    _idButtonPressTimestamp(0), _execButtonPressTimestamp(0), _bothButtonsPressTimestamp(0),
    _inBothPressSequence(false),
    _currentButtonEvent(ButtonEventType::NO_EVENT),
    _execButtonPressedDuration(0),
    _currentLedPattern(LedPatternType::LED_OFF),
    _ledTargetBlinkCount(0), _ledPatternStartTime(0),
    _ledState(false), _mosfetState(false)
{}

void HardwareManager::begin() {
    pinMode(ID_BUTTON_PIN, INPUT_PULLUP);
    pinMode(EXEC_BUTTON_PIN, INPUT_PULLUP);
    pinMode(LED_PIN, OUTPUT);
    pinMode(MOSFET_PIN_1, OUTPUT);
    pinMode(MOSFET_PIN_2, OUTPUT);
    setLed(false);
    setMosfets(false);

    xTaskCreatePinnedToCore(hardwareTask, "HardwareTask", 4096, this, 2, NULL, 0);
    Log::Info(PSTR("HW: Hardware monitoring task started on Core 0."));
}

void HardwareManager::hardwareTask(void* arg) {
    auto* self = static_cast<HardwareManager*>(arg);
    const TickType_t taskFrequency = pdMS_TO_TICKS(10);
    TickType_t lastWakeTime = xTaskGetTickCount();
    for (;;) {
        self->processButtonInput();
        self->updateLed();
        vTaskDelayUntil(&lastWakeTime, taskFrequency);
    }
}

ButtonEventType HardwareManager::getButtonEvent() {
    ButtonEventType event = _currentButtonEvent;
    if (event != ButtonEventType::NO_EVENT) {
        _currentButtonEvent = ButtonEventType::NO_EVENT;
    }
    return event;
}

unsigned long HardwareManager::getExecButtonPressedDuration() const {
    return _execButtonPressedDuration;
}

void HardwareManager::processButtonInput() {
    unsigned long currentTime = millis();
    
    // 1. Read raw button states with debounce
    bool idPressedNow = (digitalRead(ID_BUTTON_PIN) == LOW);
    if (idPressedNow != _idButtonState && (currentTime - _lastIdDebounceTime > DEBOUNCE_DELAY_MS)) {
        _idButtonState = idPressedNow;
        _lastIdDebounceTime = currentTime;
    }

    bool execPressedNow = (digitalRead(EXEC_BUTTON_PIN) == LOW);
    if (execPressedNow != _execButtonState && (currentTime - _lastExecDebounceTime > DEBOUNCE_DELAY_MS)) {
        _execButtonState = execPressedNow;
        _lastExecDebounceTime = currentTime;
    }

    // 2. [FIX] Check for the highest priority event first: both buttons long press
    bool bothPressed = _idButtonState && _execButtonState;

    if (bothPressed) {
        if (_bothButtonsPressTimestamp == 0) {
            _bothButtonsPressTimestamp = currentTime; // Start timing for both-press
        } else if (currentTime - _bothButtonsPressTimestamp >= LONG_PRESS_THRESHOLD_MS) {
            if (!_inBothPressSequence) { // Fire event only once
                _currentButtonEvent = ButtonEventType::BOTH_BUTTONS_LONG_PRESS;
                _inBothPressSequence = true; // Flag that we are in a sequence to prevent re-triggering
                // Reset other timers to prevent single press events
                _idButtonPressTimestamp = 0;
                _execButtonPressTimestamp = 0;
            }
        }
    } else {
        // Reset both-press timer and sequence flag if buttons are released
        _bothButtonsPressTimestamp = 0;
        _inBothPressSequence = false;
    }
    
    // If a both-press event just happened, ignore single button logic for this cycle
    if (_currentButtonEvent == ButtonEventType::BOTH_BUTTONS_LONG_PRESS) {
        return;
    }

    // 3. Process single button events only if not in a both-press sequence
    // ID Button Logic (Short/Long Press)
    if (_idButtonState && !_execButtonState) { // Pressed alone
        if (_idButtonPressTimestamp == 0) {
            _idButtonPressTimestamp = currentTime; // Start timing
        }
    } else if (!_idButtonState && _idButtonPressTimestamp > 0) { // Was pressed, now released
        if (currentTime - _idButtonPressTimestamp >= LONG_PRESS_THRESHOLD_MS) {
            _currentButtonEvent = ButtonEventType::ID_BUTTON_LONG_PRESS_END;
        } else {
            _currentButtonEvent = ButtonEventType::ID_BUTTON_SHORT_PRESS;
        }
        _idButtonPressTimestamp = 0; // Reset timer
    }

    // Exec Button Logic (Press/Release)
    if (_execButtonState && !_idButtonState) { // Pressed alone
        if (_execButtonPressTimestamp == 0) {
            _execButtonPressTimestamp = currentTime;
            _currentButtonEvent = ButtonEventType::EXEC_BUTTON_PRESS;
        }
    } else if (!_execButtonState && _execButtonPressTimestamp > 0) { // Was pressed, now released
        _execButtonPressedDuration = currentTime - _execButtonPressTimestamp;
        _currentButtonEvent = ButtonEventType::EXEC_BUTTON_RELEASE;
        _execButtonPressTimestamp = 0; // Reset timer
    }
}


void HardwareManager::setLedPattern(LedPatternType pattern, int repeatCount) {
    if (_currentLedPattern == pattern && _ledTargetBlinkCount == repeatCount && pattern != LedPatternType::LED_ID_SET_INCREMENT) {
        return;
    }
    _currentLedPattern = pattern;
    _ledTargetBlinkCount = repeatCount;
    _ledPatternStartTime = millis();
    Log::Debug(PSTR("HW: Setting LED pattern to %d, repeat: %d"), static_cast<int>(pattern), repeatCount);
}

void HardwareManager::updateLed() {
    if (_currentLedPattern == LedPatternType::LED_OFF) { if (_ledState) setLed(false); return; }
    if (_currentLedPattern == LedPatternType::LED_ON) { if (!_ledState) setLed(true); return; }

    unsigned long elapsedTime = millis() - _ledPatternStartTime;

    switch (_currentLedPattern) {
        case LedPatternType::LED_BOOT_SUCCESS:
            setLed(elapsedTime < LED_BOOT_SUCCESS_ON_MS);
            if (elapsedTime >= LED_BOOT_SUCCESS_ON_MS) _currentLedPattern = LedPatternType::LED_OFF;
            break;
        case LedPatternType::LED_ID_SET_ENTER:
            setLed(elapsedTime < LED_ID_SET_ENTER_ON_MS);
            // This pattern stays on until manually changed by ModeManager
            break;
        case LedPatternType::LED_ID_SET_CONFIRM:
            setLed(elapsedTime < LED_ID_SET_CONFIRM_ON_MS);
             if (elapsedTime >= LED_ID_SET_CONFIRM_ON_MS) {
                // Let ModeManager decide the next pattern
                 _currentLedPattern = LedPatternType::LED_OFF;
            }
            break;
        case LedPatternType::LED_ID_SET_INCREMENT:
            setLed(elapsedTime < LED_ID_SET_INCREMENT_BLINK_MS);
            if (elapsedTime >= LED_ID_SET_INCREMENT_BLINK_MS) _currentLedPattern = LedPatternType::LED_OFF;
            break;
        case LedPatternType::LED_ID_DISPLAY: {
            // LED_ID_DISPLAY 패턴 수정: repeatCount 만큼 정확히 깜빡이도록
            unsigned long blinkDuration = 2 * LED_ID_BLINK_INTERVAL_MS; // 한 번 깜빡이는 데 걸리는 시간 (켜짐 + 꺼짐)
            unsigned long totalDuration = (unsigned long)_ledTargetBlinkCount * blinkDuration; // 전체 점멸 시간

            if (elapsedTime >= totalDuration) {
                setLed(false); // 마지막엔 꺼짐
                _currentLedPattern = LedPatternType::LED_OFF;
            } else {
                // (elapsedTime / LED_ID_BLINK_INTERVAL_MS)는 현재 몇 번째 인터벌인지 나타냅니다.
                // 짝수 번째 인터벌에서 LED를 켜고, 홀수 번째 인터벌에서 LED를 끕니다.
                setLed((elapsedTime / LED_ID_BLINK_INTERVAL_MS) % 2 == 0);
            }
            break;
        }
        case LedPatternType::LED_WIFI_MODE_TOGGLE: {
            unsigned long totalDuration = (unsigned long)LED_WIFI_MODE_BLINK_COUNT * 2 * LED_WIFI_MODE_BLINK_INTERVAL_MS;
             if (elapsedTime >= totalDuration) {
                _currentLedPattern = LedPatternType::LED_OFF;
            } else {
                setLed((elapsedTime / LED_WIFI_MODE_BLINK_INTERVAL_MS) % 2 == 0);
            }
            break;
        }
        case LedPatternType::LED_ERROR:
            setLed((elapsedTime / 200) % 2 == 0);
            break;
        default: break;
    }
}

bool HardwareManager::isLedPatternActive() const { return _currentLedPattern != LedPatternType::LED_OFF; }
void HardwareManager::setLed(bool on) { if (_ledState != on) { _ledState = on; digitalWrite(LED_PIN, _ledState); } }
void HardwareManager::setMosfets(bool on) { if (_mosfetState != on) { _mosfetState = on; digitalWrite(MOSFET_PIN_1, on); digitalWrite(MOSFET_PIN_2, on); Log::Info(PSTR("HW: MOSFETs turned %s."), on ? "ON" : "OFF"); } }

LedPatternType HardwareManager::getCurrentLedPattern() const {
    return _currentLedPattern;
}

/**
 * @file hardware.h
 * @brief HardwareManager 클래스의 헤더 파일입니다.
 * @version 4.0.0
 * @date 2024-06-13
 */
#pragma once
#ifndef HARDWARE_H
#define HARDWARE_H

#include "config.h"
#include "utils.h"

class HardwareManager {
public:
    HardwareManager();
    void begin();
    ButtonEventType getButtonEvent();
    unsigned long getExecButtonPressedDuration() const;
    void setLedPattern(LedPatternType pattern, int repeatCount = 0);
    bool isLedPatternActive() const;
    void setMosfets(bool on);

private:
    void setLed(bool on);
    static void hardwareTask(void* arg);
    void processButtonInput();
    void updateLed();

    bool _idButtonState, _execButtonState;
    unsigned long _lastIdDebounceTime, _lastExecDebounceTime;
    unsigned long _idButtonPressTimestamp, _execButtonPressTimestamp, _bothButtonsPressTimestamp;
    bool _inBothPressSequence;

    volatile ButtonEventType _currentButtonEvent;
    volatile unsigned long _execButtonPressedDuration;
    
    volatile LedPatternType _currentLedPattern;
    volatile int _ledTargetBlinkCount;
    volatile unsigned long _ledPatternStartTime;
    volatile bool _ledState;
    
    volatile bool _mosfetState;
};

#endif // HARDWARE_H
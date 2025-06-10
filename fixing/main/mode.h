/**
 * @file mode.h
 * @brief 장치의 중앙 상태 머신인 ModeManager 클래스의 헤더 파일입니다.
 * @version 7.9.0
 * @date 2024-06-14
 */
#pragma once
#ifndef MODE_H
#define MODE_H

#include "config.h"
#include "utils.h"
#include "espnow_comm_shared.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// [FIX] 순환 참조를 피하기 위해 전방 선언 사용
class HardwareManager;
class CommManager;
class WebManager;

enum class IdSetState { IDLE, ENTERED, AWAITING_INPUT, CONFIRMING_ON, CONFIRMING_BLINK };

class ModeManager {
public:
    ModeManager(HardwareManager* hwManager, CommManager* commManager, WebManager* webManager);
    void begin();
    void update();

    void handleButtonEvent(ButtonEventType event);
    void handleEspNowCommand(const uint8_t* senderMac, const Comm::CommPacket* pkt); 
    void triggerManualRun(uint32_t delayMs, uint32_t playMs);
    void switchToMode(DeviceMode newMode, bool forceSwitch = false);
    
    DeviceMode getCurrentMode() const;
    const char* getCurrentModeName() const;

    void recordWebApiActivity();
    void exitWifiMode();
    void updateDeviceId(uint8_t newId, bool fromWeb = false);
    void setUpdateDownloaded(bool downloaded);
    void applyUpdateAndReboot();

private:
    HardwareManager* _hwManager;
    CommManager* _commManager;
    WebManager* _webManager;

    DeviceMode _currentMode;
    uint8_t _deviceId;
    
    SemaphoreHandle_t _modeSwitchMutex;
    uint32_t _currentCommandId;

    unsigned long _sequenceRxStartTimeUs; 

    IdSetState _idSetState;
    uint8_t _temporaryId;
    unsigned long _idSetLastInputTime;

    bool _isPlaySequenceActive;
    bool _isDelayPhase;
    unsigned long _delayPhaseEndTime;
    unsigned long _playPhaseEndTime;

    unsigned long _lastWebApiActivityTime;
    bool _updateDownloaded;
    bool _idBlinkPatternStarted;
    uint8_t _previousDeviceId;

    void enterModeLogic(DeviceMode mode);
    void exitModeLogic(DeviceMode mode);

    void updateModeNormal();
    void updateModeIdBlink();
    void updateModeIdSet();
    void updateModeWifi();
    void updatePlaySequence();

    void startPlaySequence(uint32_t delayMs, uint32_t playMs);
    void stopPlaySequence();
    void incrementTemporaryId();
    void finalizeIdSelection();
    const char* getModeName(DeviceMode mode) const;
};

#endif // MODE_H

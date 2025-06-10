/**
 * @file mode.h
 * @brief 장치의 중앙 상태 머신인 ModeManager 클래스의 헤더 파일입니다.
 * @version 7.6.0 // [MODIFIED] 버전 업데이트
 * @date 2024-06-13
 */
#pragma once
#ifndef MODE_H
#define MODE_H

#include "config.h"
#include "utils.h"
#include "espnow_comm_shared.h" // CommPacket 구조체 정의를 위해 필요
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

class HardwareManager;
class CommManager;
class WebManager;

// IdSetState 열거형 정의
enum class IdSetState { IDLE, ENTERED, AWAITING_INPUT, CONFIRMING_ON, CONFIRMING_BLINK };

class ModeManager {
public:
    ModeManager(HardwareManager* hwManager, CommManager* commManager, WebManager* webManager);
    void begin();
    void update();

    void handleButtonEvent(ButtonEventType event);
    // [MODIFIED] CommPacket을 const 참조 대신 const 포인터로 받음 (CommManager에서 이미 포인터 사용)
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

    // [MODIFIED] _sequenceRxStartTimeUs는 이제 최종 명령 패킷을 받은 시점의 타임스탬프를 의미
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

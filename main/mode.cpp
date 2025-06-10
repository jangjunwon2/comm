// =========================================================================
// mode.cpp
// =========================================================================

/**
 * @file mode.cpp
 * @brief ModeManager 클래스의 구현입니다.
 * @version 7.5.0
 * @date 2024-06-13
 */
#include "mode.h"
#include "hardware.h" 
#include "comm.h"     
#include "web.h"      
#include <algorithm> // std::max 사용을 위해

ModeManager::ModeManager(HardwareManager* hwManager, CommManager* commManager, WebManager* webManager)
    : _hwManager(hwManager), _commManager(commManager), _webManager(webManager),
      _currentMode(DeviceMode::MODE_BOOT),
      _deviceId(DEFAULT_DEVICE_ID), 
      _currentCommandId(0),
      _sequenceRxStartTimeUs(0), 
      _idSetState(IdSetState::IDLE), 
      _temporaryId(0),             
      _idSetLastInputTime(0),      
      _isPlaySequenceActive(false), _isDelayPhase(false), _delayPhaseEndTime(0), _playPhaseEndTime(0),
      _lastWebApiActivityTime(0), _updateDownloaded(false) 
{
    _modeSwitchMutex = xSemaphoreCreateMutex();
}

void ModeManager::begin() {
    _deviceId = NVS::loadDeviceId();
    if(_commManager) _commManager->updateMyDeviceId(_deviceId);
    Log::Info(PSTR("MODE: ModeManager initialized. Device ID is %d."), _deviceId);
    if (_hwManager) _hwManager->setLedPattern(LedPatternType::LED_BOOT_SUCCESS);
}

void ModeManager::switchToMode(DeviceMode newMode, bool forceSwitch) {
    if (xSemaphoreTake(_modeSwitchMutex, (TickType_t)0) != pdTRUE) {
        Log::Warn(PSTR("MODE: Switching already in progress. Request to switch to %s ignored."), getModeName(newMode));
        return;
    }

    if (!forceSwitch && _currentMode == newMode) {
        xSemaphoreGive(_modeSwitchMutex);
        return;
    }
    
    bool isStayingInWebUi = 
        (_currentMode == DeviceMode::MODE_WIFI && newMode == DeviceMode::MODE_TEST) ||
        (_currentMode == DeviceMode::MODE_TEST && newMode == DeviceMode::MODE_WIFI);

    if (!isStayingInWebUi) exitModeLogic(_currentMode);
    
    Log::Info(PSTR("MODE: Switching from %s to %s."), getModeName(_currentMode), getModeName(newMode));
    _currentMode = newMode;
    
    if (!isStayingInWebUi) enterModeLogic(_currentMode);

    xSemaphoreGive(_modeSwitchMutex);
}

void ModeManager::update() {
    if(_hwManager) handleButtonEvent(_hwManager->getButtonEvent());
    if (_isPlaySequenceActive) updatePlaySequence();

    switch (_currentMode) {
        case DeviceMode::MODE_NORMAL:   updateModeNormal();  break;
        case DeviceMode::MODE_ID_BLINK: updateModeIdBlink(); break;
        case DeviceMode::MODE_ID_SET:   updateModeIdSet();   break;
        case DeviceMode::MODE_WIFI:
        case DeviceMode::MODE_TEST:     updateModeWifi();    break;
        default: break;
    }
}

void ModeManager::handleButtonEvent(ButtonEventType event) {
    if (event == ButtonEventType::NO_EVENT) return;

    Log::Debug(PSTR("MODE: Handling button event %d in mode %s"), static_cast<int>(event), getModeName(_currentMode)); 

    if (event == ButtonEventType::BOTH_BUTTONS_LONG_PRESS) {
        if (_currentMode == DeviceMode::MODE_WIFI || _currentMode == DeviceMode::MODE_TEST) {
            exitWifiMode();
        } else {
            stopPlaySequence(); // 재생 중이었다면 중지
            switchToMode(DeviceMode::MODE_WIFI);
        }
        return;
    }
    
    if (_currentMode == DeviceMode::MODE_WIFI || _currentMode == DeviceMode::MODE_TEST) {
        if (event == ButtonEventType::ID_BUTTON_SHORT_PRESS) {
            Log::Info(PSTR("MODE: Displaying ID in Wi-Fi mode."));
            if (_hwManager) _hwManager->setLedPattern(LedPatternType::LED_ID_DISPLAY, _deviceId);
        } else if (event == ButtonEventType::ID_BUTTON_LONG_PRESS_END) {
            Log::Warn(PSTR("MODE: ID setting is disabled in Wi-Fi/Test mode."));
        }
        return;
    }

    if (_currentMode == DeviceMode::MODE_ID_SET) {
        _idSetLastInputTime = millis(); 
        if (event == ButtonEventType::ID_BUTTON_SHORT_PRESS) incrementTemporaryId();
        else if (event == ButtonEventType::ID_BUTTON_LONG_PRESS_END) finalizeIdSelection();
        return;
    }
    
    // 재생 시퀀스 중 다른 버튼 이벤트가 발생하면 중단
    if (_isPlaySequenceActive && event != ButtonEventType::NO_EVENT) {
        Log::Info(PSTR("MODE: Play sequence interrupted by button press."));
        stopPlaySequence();
        return;
    }

    switch (event) {
        case ButtonEventType::ID_BUTTON_SHORT_PRESS:
            switchToMode(DeviceMode::MODE_ID_BLINK);
            break;
        case ButtonEventType::ID_BUTTON_LONG_PRESS_END:
            switchToMode(DeviceMode::MODE_ID_SET);
            break;
        case ButtonEventType::EXEC_BUTTON_PRESS:
            // 수동 실행 중인 경우에만 MOSFET 켜기
            if (_currentMode == DeviceMode::MODE_NORMAL) { // 이 조건이 없으면, ID 설정 등 다른 모드에서 의도치 않게 모터가 켜질 수 있습니다.
                if (_hwManager) _hwManager->setMosfets(true);
                if (_hwManager) _hwManager->setLedPattern(LedPatternType::LED_ON);
            }
            break;
        case ButtonEventType::EXEC_BUTTON_RELEASE:
            // 수동 실행 중인 경우에만 MOSFET 끄기
            if (_currentMode == DeviceMode::MODE_NORMAL) { // 위와 동일한 이유
                if (_hwManager) _hwManager->setMosfets(false);
                if (_hwManager) _hwManager->setLedPattern(LedPatternType::LED_OFF);
                Log::Info(PSTR("MODE: Manual execution released after %lu ms."), _hwManager->getExecButtonPressedDuration());
            }
            break;
        default: break;
    }
}

// [MODIFIED] handleEspNowCommand 함수에 senderMac 인자 추가 (기존과 동일)
void ModeManager::handleEspNowCommand(const uint8_t* senderMac, const Comm::CommPacket& pkt) {
    if (_currentMode == DeviceMode::MODE_ID_SET) {
        Log::Warn(PSTR("MODE: ID_SET mode. ESP-NOW command ignored for timer logic."));
        // ID_SET 모드에서도 ACK는 보내야 송신부가 타임아웃되지 않음
        if (_commManager && senderMac) {
            _commManager->sendAck(senderMac, pkt.txMicros, micros()); // ACK 전송 (처리 시간 포함)
        }
        return;
    }

    unsigned long rxTime = micros(); // 패킷 수신 시각 (수신부 기준)

    // 이 패킷이 새 시퀀스의 첫 패킷인지, 아니면 기존 시퀀스의 재전송 패킷인지 확인
    bool isNewCommandSequence = (_currentCommandId != pkt.txButtonPressMicros);

    uint32_t originalDelayMs = pkt.delayMs; // 송신자가 보낸 원본 지연 시간
    uint32_t playMs = pkt.playMs; // 플레이 시간

    // [NEW] 통신 지연 보정값 계산 (현재 패킷의 실제 단방향 지연)
    long communicationLatencyUs = (long)rxTime - (long)pkt.txMicros;
    // 음수이거나 너무 큰 값(논리적으로 불가능한 값)이면 0으로 처리하여 안정성 확보
    if (communicationLatencyUs < 0 || communicationLatencyUs > 1000000) { // 1초 이상 지연이면 비정상으로 판단
        communicationLatencyUs = 0;
    }
    long currentCompensationMs = communicationLatencyUs / 1000L; // 마이크로초를 밀리초로 변환

    if (isNewCommandSequence) {
        if (_isPlaySequenceActive) {
            Log::Info(PSTR("COMM: New sequence %u received. Stopping previous sequence %u."), pkt.txButtonPressMicros, _currentCommandId);
            stopPlaySequence(); 
        }
        _currentCommandId = pkt.txButtonPressMicros;
        _sequenceRxStartTimeUs = rxTime; // 첫 패킷 수신 시각 기록

        long finalAdjustedDelayMs = originalDelayMs - currentCompensationMs;
        finalAdjustedDelayMs = std::max(0L, finalAdjustedDelayMs); // 지연 시간이 음수가 되지 않도록 (즉시 실행)

        // [MODIFIED] 요청하신 로그 형식으로 변경
        Log::Info(PSTR("COMM: Device ID %d"), pkt.targetId);
        Log::Info(PSTR("  - 원본 데이터 (지연): %u ms (플레이: %u ms)"), originalDelayMs, playMs);
        Log::Info(PSTR("  - 계산된 보정값 (통신 지연): %ld ms"), currentCompensationMs);
        Log::Info(PSTR("  - 최종 적용 딜레이: %ld ms"), finalAdjustedDelayMs);
        
        Log::Debug(PSTR("COMM: Device ID %d - TX Btn Micros (Sender): %u us, TX Pkt Micros (Sender): %u us, RX Pkt Micros (Receiver): %lu us"),
                  pkt.targetId, pkt.txButtonPressMicros, pkt.txMicros, rxTime);
        Log::Debug(PSTR("COMM: Device ID %d - RTT from last comm (from Sender): %u us, RxProc from last comm (from Sender): %u us"),
                  pkt.targetId, pkt.lastKnownRttUs, pkt.lastKnownRxProcessingTimeUs);
        
        startPlaySequence(finalAdjustedDelayMs, playMs); 
    } else { // 재전송 패킷
        // 재전송 패킷이 들어왔을 때 타이머를 조정할지 여부는 현재 구현에서는 고려하지 않음.
        // 첫 패킷의 보정값으로 타이머를 시작하며, 재전송은 신뢰성 확보에 초점을 맞춤.
        // 하지만 여기서도 통신 지연을 계산하고 로그로 남길 수 있습니다.
        Log::Debug(PSTR("COMM: Sequence %u 재전송 수신. 타이머는 이미 실행 중. 현재 통신 지연: %ld us"), 
                   _currentCommandId, communicationLatencyUs);
    }

    // ACK 패킷 전송 (송신부로의 확인 응답)
    if (_commManager && senderMac) {
        _commManager->sendAck(senderMac, pkt.txMicros, rxTime); 
    }
}


void ModeManager::triggerManualRun(uint32_t delayMs, uint32_t playMs) {
    if (_currentMode == DeviceMode::MODE_TEST || _currentMode == DeviceMode::MODE_WIFI) {
        if (_isPlaySequenceActive) {
            stopPlaySequence();
        }
        _currentCommandId = 0; // 수동 실행은 특정 Command ID에 묶이지 않습니다.
        Log::Info(PSTR("MODE: Manual test run started. Delay: %u ms, Play: %u ms."), delayMs, playMs);
        startPlaySequence(delayMs, playMs);
    }
}

void ModeManager::enterModeLogic(DeviceMode mode) {
    switch (mode) {
        case DeviceMode::MODE_ID_BLINK:
            _hwManager->setLedPattern(LedPatternType::LED_ID_DISPLAY, _deviceId);
            break;
        case DeviceMode::MODE_ID_SET:
            stopPlaySequence(); // ID 설정 모드 진입 시 재생 시퀀스 중지
            _temporaryId = 0; 
            _idSetState = IdSetState::ENTERED; 
            _idSetLastInputTime = millis(); 
            _hwManager->setLedPattern(LedPatternType::LED_ID_SET_ENTER);
            break;
        case DeviceMode::MODE_WIFI:
            stopPlaySequence(); // Wi-Fi 모드 진입 시 재생 시퀀스 중지
            _hwManager->setLedPattern(LedPatternType::LED_WIFI_MODE_TOGGLE);
            if (_webManager) _webManager->startServer();
            _lastWebApiActivityTime = millis();
            break;
        case DeviceMode::MODE_ERROR:
            _hwManager->setLedPattern(LedPatternType::LED_ERROR);
            break;
        default: break;
    }
}

void ModeManager::exitModeLogic(DeviceMode mode) {
    switch (mode) {
        case DeviceMode::MODE_ID_SET:
            _idSetState = IdSetState::IDLE; 
            break;
        case DeviceMode::MODE_WIFI:
        case DeviceMode::MODE_TEST:
            if (_webManager && _webManager->isServerRunning()) {
                if (_updateDownloaded) {
                    applyUpdateAndReboot();
                    return; 
                }
                _webManager->stopServer();
            }
            if (_commManager) _commManager->reinitForEspNow(); // ESP-NOW 재초기화 (Wi-Fi 모드 종료 후)
            _hwManager->setLedPattern(LedPatternType::LED_WIFI_MODE_TOGGLE); // 짧은 LED 깜빡임으로 모드 전환 표시
            break;
        default: break;
    }
}

void ModeManager::updateModeNormal() {
    // 시퀀스 타임아웃 체크
    if (_isPlaySequenceActive && _sequenceRxStartTimeUs > 0) {
        unsigned long currentTime = micros();
        // 시퀀스 시작 후 5초 이상 패킷이 오지 않으면 시퀀스 중단
        // 이는 통신이 끊겼을 때 장치가 무한정 대기하지 않도록 합니다.
        if (currentTime - _sequenceRxStartTimeUs > 5000000) { // 5초 = 5,000,000 마이크로초
            Log::Warn(PSTR("MODE: Sequence %u timeout. No packet received for 5 seconds."), _currentCommandId);
            stopPlaySequence();
            _sequenceRxStartTimeUs = 0; // 타임아웃 후 초기화
        }
    }
}

void ModeManager::updateModeIdBlink() {
    if (_hwManager && !_hwManager->isLedPatternActive()) {
        switchToMode(DeviceMode::MODE_NORMAL);
    }
}

void ModeManager::updateModeIdSet() {
    unsigned long currentTime = millis();
    switch (_idSetState) { 
        case IdSetState::ENTERED: 
            // LED가 켜져있는 동안 대기
            if (currentTime - _idSetLastInputTime > LED_ID_SET_ENTER_ON_MS) { 
                _idSetState = IdSetState::AWAITING_INPUT; 
                 _hwManager->setLedPattern(LedPatternType::LED_OFF); // LED 끄고 입력 대기
                Log::Info(PSTR("MODE: Ready to receive ID input."));
            }
            break;
        case IdSetState::AWAITING_INPUT: 
            // ID 설정 시간 초과 확인
            if (currentTime - _idSetLastInputTime > ID_SET_TIMEOUT_MS) { 
                Log::Info(PSTR("MODE: ID setting timed out."));
                finalizeIdSelection(); // 시간 초과 시 현재 임시 ID로 확정 또는 기본값 사용
            }
            break;
        case IdSetState::CONFIRMING_ON: 
            // LED 켜져있는 동안 확인
            if (currentTime - _idSetLastInputTime > LED_ID_SET_CONFIRM_ON_MS) { 
                _idSetState = IdSetState::CONFIRMING_BLINK; 
                _hwManager->setLedPattern(LedPatternType::LED_ID_DISPLAY, _deviceId); // 설정된 ID 깜빡임
            }
            break;
        case IdSetState::CONFIRMING_BLINK: 
            // LED 깜빡임 완료 대기
            if (!_hwManager->isLedPatternActive()) {
                switchToMode(DeviceMode::MODE_NORMAL); // 일반 모드로 돌아감
            }
            break;
        default: break;
    }
}

void ModeManager::updateModeWifi() {
    if (millis() - _lastWebApiActivityTime > WIFI_MODE_AUTO_EXIT_MS) {
        Log::Info(PSTR("MODE: Wi-Fi mode inactive for %d minutes. Exiting."), WIFI_MODE_AUTO_EXIT_MS / 60000);
        exitWifiMode();
    }
}

void ModeManager::updatePlaySequence() {
    unsigned long currentTime = millis();
    if (_isDelayPhase && currentTime >= _delayPhaseEndTime) {
        _isDelayPhase = false;
        // 딜레이 페이즈가 끝나면 모터 켜고 LED 켜기
        if (_hwManager) {
            _hwManager->setMosfets(true);
            _hwManager->setLedPattern(LedPatternType::LED_ON);
        }
        Log::Info(PSTR("MODE: Delay phase completed. Playing."));
    }
    // 플레이 페이즈가 끝나면 시퀀스 중지
    if (currentTime >= _playPhaseEndTime) {
        stopPlaySequence();
        Log::Info(PSTR("MODE: Play phase completed."));
    }
}

void ModeManager::startPlaySequence(uint32_t delayMs, uint32_t playMs) {
    unsigned long currentTime = millis();
    _isPlaySequenceActive = true;
    _delayPhaseEndTime = currentTime + delayMs;
    _playPhaseEndTime = _delayPhaseEndTime + playMs;

    Log::Info(PSTR("MODE: Play sequence started. Calculated delay: %u ms, Play duration: %u ms."), delayMs, playMs);

    if (delayMs > 0) {
        _isDelayPhase = true;
        if (_hwManager) {
             _hwManager->setMosfets(false); // 딜레이 중에는 모터 OFF
             _hwManager->setLedPattern(LedPatternType::LED_OFF); // 딜레이 중에는 LED OFF
        }
        Log::Info(PSTR("MODE: Delay phase active."));
    } else {
        _isDelayPhase = false;
        // 딜레이가 0ms면 즉시 모터 켜고 LED 켜기
        if (_hwManager) {
            _hwManager->setMosfets(true);
            _hwManager->setLedPattern(LedPatternType::LED_ON);
        }
        Log::Info(PSTR("MODE: No delay. Playing immediately."));
    }
}

void ModeManager::stopPlaySequence() {
    if (_isPlaySequenceActive) {
        _isPlaySequenceActive = false;
        if (_hwManager) _hwManager->setMosfets(false); // 모터 끄기
        if (_hwManager) _hwManager->setLedPattern(LedPatternType::LED_OFF); // LED 끄기

        if (_currentCommandId != 0) {
             Log::Info(PSTR("COMM: Sequence %u completed or stopped."), _currentCommandId);
        } else {
             Log::Debug(PSTR("MODE: Manual play sequence stopped."));
        }
        _currentCommandId = 0; // 시퀀스 ID 초기화

        if (_currentMode == DeviceMode::MODE_TEST) {
             if (_webManager) _webManager->broadcastTestComplete();
        }
    }
}

void ModeManager::incrementTemporaryId() {
    if (_idSetState == IdSetState::ENTERED) _idSetState = IdSetState::AWAITING_INPUT; 
    _temporaryId = (_temporaryId == 0) ? MIN_DEVICE_ID : _temporaryId + 1; 
    if (_temporaryId > MAX_DEVICE_ID) _temporaryId = MIN_DEVICE_ID; 
    _idSetLastInputTime = millis(); 
    _hwManager->setLedPattern(LedPatternType::LED_ID_SET_INCREMENT);
    Log::Info(PSTR("MODE: Temporary ID set to %d."), _temporaryId); 
}

void ModeManager::finalizeIdSelection() {
    if (_idSetState != IdSetState::AWAITING_INPUT) return; 
    _idSetState = IdSetState::CONFIRMING_ON; 
    _idSetLastInputTime = millis(); 
    
    uint8_t finalId = (_temporaryId == 0) ? _deviceId : _temporaryId; 
    updateDeviceId(finalId);
    
    _hwManager->setLedPattern(LedPatternType::LED_ID_SET_CONFIRM);
}

void ModeManager::updateDeviceId(uint8_t newId, bool fromWeb) {
    if (newId < MIN_DEVICE_ID || newId > MAX_DEVICE_ID) {
        Log::Warn(PSTR("MODE: Attempted to set invalid Device ID: %d. Keeping current ID: %d."), newId, _deviceId);
        return;
    }
    _deviceId = newId;
    NVS::saveDeviceId(_deviceId);
    if (_commManager) _commManager->updateMyDeviceId(_deviceId);
    Log::Info(PSTR("MODE: Device ID set to %d."), _deviceId);
    if (fromWeb && _hwManager) {
        _hwManager->setLedPattern(LedPatternType::LED_ID_DISPLAY, _deviceId);
    }
}

void ModeManager::recordWebApiActivity() { _lastWebApiActivityTime = millis(); }
void ModeManager::exitWifiMode() { switchToMode(DeviceMode::MODE_NORMAL); }

const char* ModeManager::getModeName(DeviceMode mode) const {
    switch (mode) {
        case DeviceMode::MODE_BOOT: return "BOOT"; case DeviceMode::MODE_NORMAL: return "NORMAL";
        case DeviceMode::MODE_ID_BLINK: return "ID_BLINK"; case DeviceMode::MODE_ID_SET: return "ID_SET";
        case DeviceMode::MODE_WIFI: return "WIFI"; case DeviceMode::MODE_TEST: return "TEST";
        case DeviceMode::MODE_ERROR: return "ERROR"; default: return "UNKNOWN";
    }
}
DeviceMode ModeManager::getCurrentMode() const { return _currentMode; }
const char* ModeManager::getCurrentModeName() const { return getModeName(_currentMode); }
void ModeManager::setUpdateDownloaded(bool downloaded) { _updateDownloaded = downloaded; }
void ModeManager::applyUpdateAndReboot() { if(_webManager) _webManager->performUpdate(); }

// jangjunwon2/comm/comm-6295635354ffa5ad160f5b3be2c0db2652b69d97/main/mode.cpp
// =========================================================================
// mode.cpp
// =========================================================================

/**
 * @file mode.cpp
 * @brief ModeManager 클래스의 구현입니다.
 * @version 7.6.0 // [MODIFIED] 버전 업데이트
 * @date 2024-06-13
 */
#include "mode.h" //
#include "hardware.h" //
#include "comm.h"     //
#include "web.h"      //
#include <algorithm> // std::max 사용을 위해 //

ModeManager::ModeManager(HardwareManager* hwManager, CommManager* commManager, WebManager* webManager)
    : _hwManager(hwManager), _commManager(commManager), _webManager(webManager),
      _currentMode(DeviceMode::MODE_BOOT),
      _deviceId(DEFAULT_DEVICE_ID), //
      _currentCommandId(0),
      _sequenceRxStartTimeUs(0), //
      _idSetState(IdSetState::IDLE), //
      _temporaryId(0),             //
      _idSetLastInputTime(0),      //
      _isPlaySequenceActive(false), _isDelayPhase(false), _delayPhaseEndTime(0), _playPhaseEndTime(0),
      _lastWebApiActivityTime(0), _updateDownloaded(false),
      _idBlinkPatternStarted(false),
      _previousDeviceId(DEFAULT_DEVICE_ID) //
{
    _modeSwitchMutex = xSemaphoreCreateMutex(); //
}

void ModeManager::begin() {
    _deviceId = NVS::loadDeviceId(); //
    if(_commManager) _commManager->updateMyDeviceId(_deviceId); //
    Log::Info(PSTR("MODE: ModeManager initialized. Device ID is %d."), _deviceId); //
    if (_hwManager) _hwManager->setLedPattern(LedPatternType::LED_BOOT_SUCCESS); //
}

void ModeManager::switchToMode(DeviceMode newMode, bool forceSwitch) {
    if (xSemaphoreTake(_modeSwitchMutex, (TickType_t)0) != pdTRUE) { //
        Log::Warn(PSTR("MODE: Switching already in progress. Request to switch to %s ignored."), getModeName(newMode)); //
        return; //
    }

    if (!forceSwitch && _currentMode == newMode) { //
        xSemaphoreGive(_modeSwitchMutex); //
        return; //
    }
    
    bool isStayingInWebUi = 
        (_currentMode == DeviceMode::MODE_WIFI && newMode == DeviceMode::MODE_TEST) ||
        (_currentMode == DeviceMode::MODE_TEST && newMode == DeviceMode::MODE_WIFI); //

    if (!isStayingInWebUi) exitModeLogic(_currentMode); //
    
    Log::Info(PSTR("MODE: Switching from %s to %s."), getModeName(_currentMode), getModeName(newMode)); //
    _currentMode = newMode; //
    
    if (!isStayingInWebUi) enterModeLogic(_currentMode); //

    xSemaphoreGive(_modeSwitchMutex); //
}

void ModeManager::update() {
    if(_hwManager) handleButtonEvent(_hwManager->getButtonEvent()); //
    if (_isPlaySequenceActive) updatePlaySequence(); //

    switch (_currentMode) { //
        case DeviceMode::MODE_NORMAL:   updateModeNormal();  break; //
        case DeviceMode::MODE_ID_BLINK: updateModeIdBlink(); break; //
        case DeviceMode::MODE_ID_SET:   updateModeIdSet();   break; //
        case DeviceMode::MODE_WIFI:
        case DeviceMode::MODE_TEST:     updateModeWifi();    break; //
        default: break; //
    }
}

void ModeManager::handleButtonEvent(ButtonEventType event) {
    if (event == ButtonEventType::NO_EVENT) return; //

    Log::Debug(PSTR("MODE: Handling button event %d in mode %s"), static_cast<int>(event), getModeName(_currentMode)); //

    // BOTH_BUTTONS_LONG_PRESS (Wi-Fi 모드 진입/종료)는 항상 최우선 처리
    if (event == ButtonEventType::BOTH_BUTTONS_LONG_PRESS) { //
        if (_currentMode == DeviceMode::MODE_WIFI || _currentMode == DeviceMode::MODE_TEST) { //
            exitWifiMode(); //
        } else { //
            stopPlaySequence(); // 재생 중이었다면 중지 //
            switchToMode(DeviceMode::MODE_WIFI); //
        }
        return; // 최우선 처리 후 종료 //
    }

    // Wi-Fi/Test 모드 중 ID 버튼 처리
    if (_currentMode == DeviceMode::MODE_WIFI || _currentMode == DeviceMode::MODE_TEST) { //
        if (event == ButtonEventType::ID_BUTTON_SHORT_PRESS) { //
            Log::Info(PSTR("MODE: Displaying ID in Wi-Fi mode.")); //
            if (_hwManager) _hwManager->setLedPattern(LedPatternType::LED_ID_DISPLAY, _deviceId); //
        } else if (event == ButtonEventType::ID_BUTTON_LONG_PRESS_END) { //
            Log::Warn(PSTR("MODE: ID setting is disabled in Wi-Fi/Test mode.")); //
        }
        return; // Wi-Fi/Test 모드의 버튼 처리 후 종료 //
    }

    // ID 설정 모드 중 버튼 처리
    if (_currentMode == DeviceMode::MODE_ID_SET) { //
        _idSetLastInputTime = millis(); //
        if (event == ButtonEventType::ID_BUTTON_SHORT_PRESS) incrementTemporaryId(); //
        else if (event == ButtonEventType::ID_BUTTON_LONG_PRESS_END) finalizeIdSelection(); //
        // ID 설정 모드 중에는 다른 버튼(실행 버튼)은 무시
        return; //
    }

    // ID 설정 모드가 아니면서, 재생 시퀀스 중 다른 버튼 이벤트가 발생하면 중단
    if (_isPlaySequenceActive && event != ButtonEventType::NO_EVENT) { //
        Log::Info(PSTR("MODE: Play sequence interrupted by button press.")); //
        stopPlaySequence(); //
    }

    // 일반 모드 및 ID_BLINK 모드에서의 버튼 처리
    switch (event) { //
        case ButtonEventType::ID_BUTTON_SHORT_PRESS: //
            switchToMode(DeviceMode::MODE_ID_BLINK); //
            break; //
        case ButtonEventType::ID_BUTTON_LONG_PRESS_END: //
            _previousDeviceId = _deviceId; // ID 설정 모드 진입 전 ID 저장 //
            switchToMode(DeviceMode::MODE_ID_SET); //
            break; //
        case ButtonEventType::EXEC_BUTTON_PRESS: //
            if (_currentMode == DeviceMode::MODE_NORMAL) { //
                if (_hwManager) _hwManager->setMosfets(true); //
                if (_hwManager) _hwManager->setLedPattern(LedPatternType::LED_ON); //
            }
            break; //
        case ButtonEventType::EXEC_BUTTON_RELEASE: //
            if (_currentMode == DeviceMode::MODE_NORMAL) { //
                if (_hwManager) _hwManager->setMosfets(false); //
                if (_hwManager) _hwManager->setLedPattern(LedPatternType::LED_OFF); //
                Log::Info(PSTR("MODE: Manual execution released after %lu ms."), _hwManager->getExecButtonPressedDuration()); //
            }
            break; //
        default: break; //
    }
}

// [MODIFIED] handleEspNowCommand 함수 변경
void ModeManager::handleEspNowCommand(const uint8_t* senderMac, const Comm::CommPacket* pkt) {
    if (_currentMode == DeviceMode::MODE_ID_SET) { //
        Log::Warn(PSTR("MODE: ID_SET mode. ESP-NOW command ignored for timer logic.")); //
        if (_commManager && senderMac) { //
            _commManager->sendAck(senderMac, pkt->txMicros, micros()); // ACK 전송 (처리 시간 포함) //
        }
        return; //
    }

    unsigned long rxTime = micros(); // 패킷 수신 시각 (수신부 기준) //

    Log::Info(PSTR("COMM: 패킷 수신 - ID: %u, 패킷 타입: %u, TX Btn: %lu us, TX Pkt: %lu us, RX: %lu us"),
              pkt->targetId, pkt->packetType, pkt->txButtonPressMicros, pkt->txMicros, rxTime); //

    // [NEW] 패킷 타입에 따른 분기 처리
    if (pkt->packetType == Comm::RTT_REQUEST) { //
        // RTT 요청 패킷 수신 시, ACK만 보내고 타이머 시작하지 않음
        Log::Info(PSTR("COMM: RTT_REQUEST 패킷 수신. ACK 전송 후 최종 명령 대기.")); //
        if (_commManager && senderMac) { //
            _commManager->sendAck(senderMac, pkt->txMicros, rxTime); //
        }
    } else if (pkt->packetType == Comm::FINAL_COMMAND) { //
        // 최종 명령 패킷 수신 시, 보정값 계산 후 타이머 시작
        bool isNewCommandSequence = (_currentCommandId != pkt->txButtonPressMicros); //
        
        uint32_t originalDelayMs = pkt->delayMs; //
        uint32_t playMs = pkt->playMs; //

        // [MODIFIED] 보정값은 FINAL_COMMAND 패킷에 포함된 값을 사용
        long estimatedOneWayLatencyUs = pkt->lastKnownRttUs / 2; //
        long estimatedProcessingTimeUs = pkt->lastKnownRxProcessingTimeUs; //

        long totalCompensationUs = estimatedOneWayLatencyUs + estimatedProcessingTimeUs; //
        long totalCompensationMs = totalCompensationUs / 1000L; //

        if (isNewCommandSequence) { //
            if (_isPlaySequenceActive) { //
                Log::Info(PSTR("COMM: New sequence %lu received. Stopping previous sequence %lu."), pkt->txButtonPressMicros, _currentCommandId); //
                stopPlaySequence(); //
            }
            _currentCommandId = pkt->txButtonPressMicros; //
            _sequenceRxStartTimeUs = rxTime; // 첫 (최종 명령) 패킷 수신 시각 기록 //

            long finalAdjustedDelayMs = originalDelayMs - totalCompensationMs; //
            finalAdjustedDelayMs = std::max(0L, finalAdjustedDelayMs); //

            Log::Info(PSTR("COMM: FINAL_COMMAND 패킷 수신 - ID: %u, 버튼 눌림 시간: %lu ms, 딜레이: %lu ms, 플레이: %lu ms"),
                      pkt->targetId, pkt->txButtonPressMicros / 1000UL, originalDelayMs, playMs); //
            Log::Info(PSTR("COMM: 보정값 관련 내용: 포함된 RTT: %lu us, 포함된 Rx 처리: %lu us"),
                      pkt->lastKnownRttUs, pkt->lastKnownRxProcessingTimeUs); //
            Log::Info(PSTR("COMM: 계산된 보정값 (예상 통신 지연): %ld ms"), estimatedOneWayLatencyUs / 1000L); //
            Log::Info(PSTR("COMM: 계산된 보정값 (예상 수신기 처리): %ld ms"), estimatedProcessingTimeUs / 1000L); //
            Log::Info(PSTR("COMM: 최종 통신 지연값 (총 보정값): %ld ms"), totalCompensationMs); //
            
            Log::Info(PSTR("MODE: 딜레이 타이머 시작. (원본: %lu ms, 보정 후: %ld ms)"), originalDelayMs, finalAdjustedDelayMs); //
            
            startPlaySequence(finalAdjustedDelayMs, playMs); //
            Log::TestLog(PSTR("Receiver %u: Wait %.1f s, Execute %.1f s"), _deviceId, (float)finalAdjustedDelayMs / 1000.0f, (float)playMs / 1000.0f); // [NEW] Simplified log
        } else { // 재전송 패킷 //
            Log::Debug(PSTR("COMM: 시퀀스 %lu FINAL_COMMAND 재전송 수신. 타이머는 이미 실행 중. 현재 총 예상 보정값: %ld ms"), 
                       _currentCommandId, totalCompensationMs); //
        }

        // ACK 패킷 전송 (송신부로의 확인 응답)
        if (_commManager && senderMac) { //
            _commManager->sendAck(senderMac, pkt->txMicros, rxTime); //
        }
    } else { //
        Log::Warn(PSTR("COMM: 알 수 없는 패킷 타입 %u 수신. 무시됨."), pkt->packetType); //
        // 알 수 없는 패킷 타입에 대한 ACK는 보내지 않거나, 특정 오류 ACK를 보낼 수 있음
    }
}


void ModeManager::triggerManualRun(uint32_t delayMs, uint32_t playMs) {
    if (_currentMode == DeviceMode::MODE_TEST || _currentMode == DeviceMode::MODE_WIFI) { //
        if (_isPlaySequenceActive) { //
            stopPlaySequence(); //
        }
        _currentCommandId = 0; // 수동 실행은 특정 Command ID에 묶이지 않습니다. //
        Log::Info(PSTR("MODE: Manual test run started. Delay: %u ms, Play: %u ms."), delayMs, playMs); //
        // [NEW] 수동 실행은 보정값이 없으므로 0, 0으로 전달
        startPlaySequence(delayMs, playMs); //
        Log::TestLog(PSTR("Manual Run: Wait %.1f s, Execute %.1f s"), (float)delayMs / 1000.0f, (float)playMs / 1000.0f); // [NEW] Simplified log
    }
}

void ModeManager::enterModeLogic(DeviceMode mode) {
    switch (mode) { //
        case DeviceMode::MODE_ID_BLINK: //
            _hwManager->setLedPattern(LedPatternType::LED_ID_DISPLAY, _deviceId); //
            break; //
        case DeviceMode::MODE_ID_SET: //
            stopPlaySequence(); // ID 설정 모드 진입 시 재생 시퀀스 중지 //
            _temporaryId = 0; // 지침에 따라 임시 ID를 0으로 설정 //
            _idSetState = IdSetState::ENTERED; //
            _idSetLastInputTime = millis(); //
            _hwManager->setLedPattern(LedPatternType::LED_ID_SET_ENTER); //
            _idBlinkPatternStarted = false; // 새로운 시퀀스를 위해 플래그 초기화 //
            Log::Info(PSTR("MODE: ID 설정 모드 진입. 임시 ID: %d."), _temporaryId); //
            break; //
        case DeviceMode::MODE_WIFI: //
            stopPlaySequence(); // Wi-Fi 모드 진입 시 재생 시퀀스 중지 //
            _hwManager->setLedPattern(LedPatternType::LED_WIFI_MODE_TOGGLE); //
            if (_webManager) _webManager->startServer(); //
            _lastWebApiActivityTime = millis(); //
            break; //
        case DeviceMode::MODE_ERROR: //
            _hwManager->setLedPattern(LedPatternType::LED_ERROR); //
            break; //
        default: break; //
    }
}

void ModeManager::exitModeLogic(DeviceMode mode) {
    switch (mode) { //
        case DeviceMode::MODE_ID_SET: //
            _idSetState = IdSetState::IDLE; //
            break; //
        case DeviceMode::MODE_WIFI:
        case DeviceMode::MODE_TEST:
            if (_webManager && _webManager->isServerRunning()) { //
                if (_updateDownloaded) { //
                    applyUpdateAndReboot(); //
                    return; //
                }
                _webManager->stopServer(); //
            }
            if (_commManager) _commManager->reinitForEspNow(); // ESP-NOW 재초기화 (Wi-Fi 모드 종료 후) //
            _hwManager->setLedPattern(LedPatternType::LED_WIFI_MODE_TOGGLE); // 짧은 LED 깜빡임으로 모드 전환 표시 //
            break; //
        default: break; //
    }
}

void ModeManager::updateModeNormal() {
    // 현재는 이 함수에서 시퀀스 타임아웃을 직접 관리하지 않습니다.
    // 시퀀스 타임아웃은 송신부의 manageCommunication에서 담당합니다.
    // 수신부는 FINAL_COMMAND 패킷을 받기 전까지는 타이머를 시작하지 않습니다.
    // 만약 RTT_REQUEST를 받지 못하고 장치에 문제가 생긴다면, 송신부에서 타임아웃 처리 후 실패로 간주합니다.
}

void ModeManager::updateModeIdBlink() {
    if (_hwManager && !_hwManager->isLedPatternActive()) { //
        switchToMode(DeviceMode::MODE_NORMAL); //
    }
}

void ModeManager::updateModeIdSet() {
    unsigned long currentTime = millis(); //
    switch (_idSetState) { //
        case IdSetState::ENTERED: //
            if (currentTime - _idSetLastInputTime > LED_ID_SET_ENTER_ON_MS) { //
                _idSetState = IdSetState::AWAITING_INPUT; //
                _hwManager->setLedPattern(LedPatternType::LED_OFF); //
                Log::Info(PSTR("MODE: Ready to receive ID input.")); //
            }
            break; //
        case IdSetState::AWAITING_INPUT: //
            if (currentTime - _idSetLastInputTime > ID_SET_TIMEOUT_MS) { //
                Log::Info(PSTR("MODE: ID 설정 모드 타임아웃.")); //
                finalizeIdSelection(); //
            }
            break; //
        case IdSetState::CONFIRMING_ON: //
            if (currentTime - _idSetLastInputTime > LED_ID_SET_CONFIRM_ON_MS) { //
                _idSetState = IdSetState::CONFIRMING_BLINK; //
                _hwManager->setLedPattern(LedPatternType::LED_OFF); // 1초 점등 후 LED를 끕니다. //
                _idSetLastInputTime = currentTime; // 200ms 대기 시간 카운트를 위해 시간 갱신 //
                Log::Info(PSTR("MODE: 1초 점등 완료, 200ms 대기 후 ID %d 깜빡임 시작"), _deviceId); //
            }
            break; //
        case IdSetState::CONFIRMING_BLINK: //
            // 200ms 대기 후 ID 깜빡임을 시작합니다. (단, 아직 깜빡임 패턴이 시작되지 않았을 경우에만)
            if (!_idBlinkPatternStarted && (currentTime - _idSetLastInputTime >= LED_ID_BLINK_INTERVAL_MS)) { //
                _hwManager->setLedPattern(LedPatternType::LED_ID_DISPLAY, _deviceId); //
                _idBlinkPatternStarted = true; // 패턴 시작됨 플래그 설정 //
                Log::Debug(PSTR("MODE: ID 깜빡임 시작 (ID: %d)"), _deviceId); //
            }

            // ID 깜빡임 패턴이 시작되었고 (즉, _idBlinkPatternStarted가 true),
            // 하드웨어 매니저에서 현재 실행 중인 패턴이 더 이상 활성화되지 않았을 때 (즉, 패턴이 종료되었을 때)
            if (_idBlinkPatternStarted && !_hwManager->isLedPatternActive()) { //
                Log::Info(PSTR("MODE: ID 깜빡임 완료. 일반 모드로 전환.")); //
                _idBlinkPatternStarted = false; // 플래그 초기화 //
                switchToMode(DeviceMode::MODE_NORMAL); //
            }
            break; //
        default: break; //
    }
}

void ModeManager::updateModeWifi() {
    if (millis() - _lastWebApiActivityTime > WIFI_MODE_AUTO_EXIT_MS) { //
        Log::Info(PSTR("MODE: Wi-Fi mode inactive for %d minutes. Exiting."), WIFI_MODE_AUTO_EXIT_MS / 60000); //
        exitWifiMode(); //
    }
}

void ModeManager::updatePlaySequence() {
    unsigned long currentTime = millis(); //
    if (_isDelayPhase && currentTime >= _delayPhaseEndTime) { //
        _isDelayPhase = false; //
        if (_hwManager) { //
            _hwManager->setMosfets(true); //
            _hwManager->setLedPattern(LedPatternType::LED_ON); //
        }
        Log::Info(PSTR("MODE: Delay phase completed. Playing.")); //
    }
    if (currentTime >= _playPhaseEndTime) { //
        stopPlaySequence(); //
        Log::Info(PSTR("MODE: Play phase completed.")); //
    }
}

void ModeManager::startPlaySequence(uint32_t delayMs, uint32_t playMs) {
    unsigned long currentTime = millis(); //
    _isPlaySequenceActive = true; //
    _delayPhaseEndTime = currentTime + delayMs; //
    _playPhaseEndTime = _delayPhaseEndTime + playMs; //

    Log::Info(PSTR("MODE: Play sequence started. Calculated delay: %lu ms, Play duration: %lu ms."), delayMs, playMs); //

    if (delayMs > 0) { //
        _isDelayPhase = true; //
        if (_hwManager) { //
             _hwManager->setMosfets(false); //
             _hwManager->setLedPattern(LedPatternType::LED_OFF); //
        }
        Log::Info(PSTR("MODE: Delay phase active.")); //
    } else { //
        _isDelayPhase = false; //
        if (_hwManager) { //
            _hwManager->setMosfets(true); //
            _hwManager->setLedPattern(LedPatternType::LED_ON); //
        }
        Log::Info(PSTR("MODE: No delay. Playing immediately.")); //
    }
}

void ModeManager::stopPlaySequence() {
    if (_isPlaySequenceActive) { //
        _isPlaySequenceActive = false; //
        if (_hwManager) _hwManager->setMosfets(false); //
        if (_hwManager) _hwManager->setLedPattern(LedPatternType::LED_OFF); //

        if (_currentCommandId != 0) { //
             Log::Info(PSTR("COMM: Sequence %lu completed or stopped."), _currentCommandId); //
        } else { //
             Log::Debug(PSTR("MODE: Manual play sequence stopped.")); //
        }
        _currentCommandId = 0; //

        if (_currentMode == DeviceMode::MODE_TEST) { //
             if (_webManager) _webManager->broadcastTestComplete(); //
        }
    }
}

void ModeManager::incrementTemporaryId() {
    if (_idSetState == IdSetState::ENTERED) _idSetState = IdSetState::AWAITING_INPUT; //
    _temporaryId = (_temporaryId == 0) ? MIN_DEVICE_ID : _temporaryId + 1; //
    if (_temporaryId > MAX_DEVICE_ID) _temporaryId = MIN_DEVICE_ID; //
    _idSetLastInputTime = millis(); //
    _hwManager->setLedPattern(LedPatternType::LED_ID_SET_INCREMENT); //
    Log::Info(PSTR("MODE: Temporary ID set to %d."), _temporaryId); //
}

void ModeManager::finalizeIdSelection() {
    if (_idSetState != IdSetState::AWAITING_INPUT) return; //
    _idSetState = IdSetState::CONFIRMING_ON; //
    _idSetLastInputTime = millis(); //

    uint8_t finalId;
    if (_temporaryId == 0) { //
        finalId = _previousDeviceId; // ID가 0이라면 진입 전 ID로 치환 //
        Log::Info(PSTR("MODE: 임시 ID가 0이므로, 이전 ID %d로 확정합니다."), finalId); //
    } else { //
        finalId = _temporaryId; //
    }

    updateDeviceId(finalId); // 확정 ID로 업데이트 //

    Log::Info(PSTR("MODE: ID 설정 확정 - ID: %d, 1초 점등 시작"), finalId); //
    _hwManager->setLedPattern(LedPatternType::LED_ID_SET_CONFIRM); //
    _idBlinkPatternStarted = false; // 패턴 시작 플래그 초기화 //
}

void ModeManager::updateDeviceId(uint8_t newId, bool fromWeb) {
    if (newId < MIN_DEVICE_ID || newId > MAX_DEVICE_ID) { //
        Log::Warn(PSTR("MODE: Attempted to set invalid Device ID: %d. Keeping current ID: %d."), newId, _deviceId); //
        return; //
    }
    _deviceId = newId; //
    NVS::saveDeviceId(_deviceId); //
    if (_commManager) _commManager->updateMyDeviceId(_deviceId); //
    Log::Info(PSTR("MODE: Device ID set to %d."), _deviceId); //
    if (fromWeb && _hwManager) { //
        _hwManager->setLedPattern(LedPatternType::LED_ID_DISPLAY, _deviceId); //
        Log::TestLog(PSTR("ID changed: %d"), _deviceId); // [NEW] Simplified log
    }
}

void ModeManager::recordWebApiActivity() { _lastWebApiActivityTime = millis(); } //
void ModeManager::exitWifiMode() { switchToMode(DeviceMode::MODE_NORMAL); } //

const char* ModeManager::getModeName(DeviceMode mode) const {
    switch (mode) { //
        case DeviceMode::MODE_BOOT: return "BOOT"; case DeviceMode::MODE_NORMAL: return "NORMAL"; //
        case DeviceMode::MODE_ID_BLINK: return "ID_BLINK"; case DeviceMode::MODE_ID_SET: return "ID_SET"; //
        case DeviceMode::MODE_WIFI: return "WIFI"; case DeviceMode::MODE_TEST: return "TEST"; //
        case DeviceMode::MODE_ERROR: return "ERROR"; default: return "UNKNOWN"; //
    }
}
DeviceMode ModeManager::getCurrentMode() const { return _currentMode; } //
const char* ModeManager::getCurrentModeName() const { return getModeName(_currentMode); } //
void ModeManager::setUpdateDownloaded(bool downloaded) { _updateDownloaded = downloaded; } //
void ModeManager::applyUpdateAndReboot() { if(_webManager) _webManager->performUpdate(); } //
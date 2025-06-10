// =========================================================================
// mode.cpp
// =========================================================================

/**
 * @file mode.cpp
 * @brief ModeManager 클래스의 구현입니다.
 * @version 8.1.0
 * @date 2024-06-14
 */
#include "mode.h"
#include "hardware.h"
#include "comm.h" 
#include "web.h"
#include <algorithm>

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
      _lastWebApiActivityTime(0), _updateDownloaded(false),
      _idBlinkPatternStarted(false),
      _previousDeviceId(DEFAULT_DEVICE_ID)
{
    _modeSwitchMutex = xSemaphoreCreateMutex();
}

void ModeManager::begin() {
    _deviceId = NVS::loadDeviceId();
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

    if (_currentMode == DeviceMode::MODE_BOOT) {
        Log::Debug(PSTR("MODE: Button event ignored during BOOT mode."));
        return;
    }

    Log::Debug(PSTR("MODE: Handling button event %d in mode %s"), static_cast<int>(event), getModeName(_currentMode));

    if (event == ButtonEventType::BOTH_BUTTONS_LONG_PRESS) {
        if (_currentMode == DeviceMode::MODE_WIFI || _currentMode == DeviceMode::MODE_TEST) {
            exitWifiMode();
        } else {
            stopPlaySequence();
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

    if (_isPlaySequenceActive && event != ButtonEventType::NO_EVENT) {
        Log::Info(PSTR("MODE: Play sequence interrupted by button press."));
        stopPlaySequence();
    }

    switch (event) {
        case ButtonEventType::ID_BUTTON_SHORT_PRESS:
            switchToMode(DeviceMode::MODE_ID_BLINK);
            break;
        case ButtonEventType::ID_BUTTON_LONG_PRESS_END:
            _previousDeviceId = _deviceId;
            switchToMode(DeviceMode::MODE_ID_SET);
            break;
        case ButtonEventType::EXEC_BUTTON_PRESS:
            if (_currentMode == DeviceMode::MODE_NORMAL) {
                if (_hwManager) _hwManager->setMosfets(true);
                if (_hwManager) _hwManager->setLedPattern(LedPatternType::LED_ON);
            }
            break;
        case ButtonEventType::EXEC_BUTTON_RELEASE:
            if (_currentMode == DeviceMode::MODE_NORMAL) {
                if (_hwManager) _hwManager->setMosfets(false);
                if (_hwManager) _hwManager->setLedPattern(LedPatternType::LED_OFF);
                Log::Info(PSTR("MODE: Manual execution released after %lu ms."), _hwManager->getExecButtonPressedDuration());
            }
            break;
        default: break;
    }
}

void ModeManager::handleEspNowCommand(const uint8_t* senderMac, const Comm::CommPacket* pkt) {
    if (_currentMode == DeviceMode::MODE_ID_SET) {
        Log::Warn(PSTR("MODE: ID_SET mode. ESP-NOW command ignored for timer logic."));
        if (_commManager && senderMac) {
            _commManager->sendAck(senderMac, pkt->txMicros, micros());
        }
        return;
    }

    unsigned long rxTime = micros();

    Log::Info(PSTR("COMM: Packet received - TargetID: %u, Type: %u, TX_Btn: %lu us, TX_Pkt: %lu us, RX: %lu us"),
              pkt->targetId, pkt->packetType, pkt->txButtonPressMicros, pkt->txMicros, rxTime);

    if (pkt->packetType == Comm::RTT_REQUEST) {
        Log::Info(PSTR("COMM: RTT_REQUEST received. Sending ACK and waiting for FINAL_COMMAND."));
        if (_commManager && senderMac) {
            _commManager->sendAck(senderMac, pkt->txMicros, rxTime);
        }
    } else if (pkt->packetType == Comm::FINAL_COMMAND) {
        bool isNewCommandSequence = (_currentCommandId != pkt->txButtonPressMicros);
        
        uint32_t originalDelayMs = pkt->delayMs;
        uint32_t playMs = pkt->playMs;

        long estimatedOneWayLatencyUs = pkt->lastKnownRttUs / 2;
        long estimatedProcessingTimeUs = pkt->lastKnownRxProcessingTimeUs;

        long totalCompensationUs = estimatedOneWayLatencyUs + estimatedProcessingTimeUs;
        long totalCompensationMs = totalCompensationUs / 1000L;

        if (isNewCommandSequence) {
            if (_isPlaySequenceActive) {
                Log::Info(PSTR("COMM: New sequence %lu received. Stopping previous sequence %lu."), pkt->txButtonPressMicros, _currentCommandId);
                stopPlaySequence();
            }
            _currentCommandId = pkt->txButtonPressMicros;
            _sequenceRxStartTimeUs = rxTime;

            long finalAdjustedDelayMs = originalDelayMs - totalCompensationMs;
            finalAdjustedDelayMs = std::max(0L, finalAdjustedDelayMs);

            Log::TestLog("Communication data: Wait %.2fs, Execute %.2fs", (float)finalAdjustedDelayMs / 1000.0f, (float)playMs / 1000.0f);
            Log::TestLog("Receiver action: Wait %.2fs, Execute %.2fs started", (float)finalAdjustedDelayMs / 1000.0f, (float)playMs / 1000.0f);

            startPlaySequence(finalAdjustedDelayMs, playMs);
            
        } else {
            Log::Debug(PSTR("COMM: Sequence %lu FINAL_COMMAND re-transmission received. Timer already running."), _currentCommandId);
        }

        if (_commManager && senderMac) {
            _commManager->sendAck(senderMac, pkt->txMicros, rxTime);
        }
    } else {
        Log::Warn(PSTR("COMM: Unknown packet type %u received. Ignored."), pkt->packetType);
    }
}


void ModeManager::triggerManualRun(uint32_t delayMs, uint32_t playMs) {
    if (_currentMode == DeviceMode::MODE_TEST || _currentMode == DeviceMode::MODE_WIFI) {
        if (_isPlaySequenceActive) {
            stopPlaySequence();
        }
        _currentCommandId = 0;
        
        Log::TestLog("Input: Wait %.2f s, Execute %.2f s", (float)delayMs / 1000.0f, (float)playMs / 1000.0f);
        
        startPlaySequence(delayMs, playMs);
    }
}

void ModeManager::enterModeLogic(DeviceMode mode) {
    switch (mode) {
        case DeviceMode::MODE_ID_BLINK:
            _hwManager->setLedPattern(LedPatternType::LED_ID_DISPLAY, _deviceId);
            break;
        case DeviceMode::MODE_ID_SET:
            stopPlaySequence();
            _temporaryId = 0;
            _idSetState = IdSetState::ENTERED;
            _idSetLastInputTime = millis();
            _hwManager->setLedPattern(LedPatternType::LED_ID_SET_ENTER);
            _idBlinkPatternStarted = false;
            Log::Info(PSTR("MODE: Entering ID Set mode. Temporary ID: %d."), _temporaryId);
            break;
        case DeviceMode::MODE_WIFI:
            stopPlaySequence();
            _hwManager->setLedPattern(LedPatternType::LED_WIFI_MODE_TOGGLE);
            if (_webManager) _webManager->startServer();
            _lastWebApiActivityTime = millis();
            break;
        case DeviceMode::MODE_ERROR:
            _hwManager->setLedPattern(LedPatternType::LED_ERROR);
            break;
        case DeviceMode::MODE_NORMAL:
            // [FIX] Removed ESP-NOW re-initialization from here to prevent unnecessary calls.
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
            _hwManager->setLedPattern(LedPatternType::LED_WIFI_MODE_TOGGLE);
            // [FIX] ESP-NOW re-initialization is now done here, only when exiting Wi-Fi mode.
            if (_commManager) _commManager->reinitForEspNow();
            break;
        default: break;
    }
}

void ModeManager::updateModeNormal() {}

void ModeManager::updateModeIdBlink() {
    if (_hwManager && !_hwManager->isLedPatternActive()) {
        switchToMode(DeviceMode::MODE_NORMAL);
    }
}

void ModeManager::updateModeIdSet() {
    unsigned long currentTime = millis();
    switch (_idSetState) {
        case IdSetState::ENTERED:
            if (currentTime - _idSetLastInputTime > LED_ID_SET_ENTER_ON_MS) {
                _idSetState = IdSetState::AWAITING_INPUT;
                _hwManager->setLedPattern(LedPatternType::LED_OFF);
                Log::Info(PSTR("MODE: Ready to receive ID input."));
            }
            break;
        case IdSetState::AWAITING_INPUT:
            if (currentTime - _idSetLastInputTime > ID_SET_TIMEOUT_MS) {
                Log::Info(PSTR("MODE: ID set mode timed out."));
                finalizeIdSelection();
            }
            break;
        case IdSetState::CONFIRMING_ON:
            if (currentTime - _idSetLastInputTime > LED_ID_SET_CONFIRM_ON_MS) {
                _idSetState = IdSetState::CONFIRMING_BLINK;
                _hwManager->setLedPattern(LedPatternType::LED_OFF);
                _idSetLastInputTime = currentTime; 
                Log::Info(PSTR("MODE: 1s ON finished, wait 200ms before blinking ID %d"), _deviceId);
            }
            break;
        case IdSetState::CONFIRMING_BLINK:
            if (!_idBlinkPatternStarted && (currentTime - _idSetLastInputTime >= LED_ID_BLINK_INTERVAL_MS)) {
                _hwManager->setLedPattern(LedPatternType::LED_ID_DISPLAY, _deviceId);
                _idBlinkPatternStarted = true;
                Log::Debug(PSTR("MODE: Blinking ID: %d"), _deviceId);
            }

            if (_idBlinkPatternStarted && !_hwManager->isLedPatternActive()) {
                Log::Info(PSTR("MODE: ID blink finished. Returning to NORMAL mode."));
                _idBlinkPatternStarted = false;
                switchToMode(DeviceMode::MODE_NORMAL);
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
        if (_hwManager) {
            _hwManager->setMosfets(true);
            _hwManager->setLedPattern(LedPatternType::LED_ON);
        }
        Log::Info(PSTR("MODE: Delay phase completed. Playing."));
    }
    if (currentTime >= _playPhaseEndTime) {
        float delayS = (float)(_delayPhaseEndTime - (_playPhaseEndTime - (_playPhaseEndTime - _delayPhaseEndTime))) / 1000.0f;
        float playS = (float)(_playPhaseEndTime - _delayPhaseEndTime) / 1000.0f;
        Log::TestLog("Receiver action: Wait %.2fs, Execute %.2fs completed", delayS, playS);
        
        stopPlaySequence();
        Log::Info(PSTR("MODE: Play phase completed."));
    }
}

void ModeManager::startPlaySequence(uint32_t delayMs, uint32_t playMs) {
    unsigned long currentTime = millis();
    _isPlaySequenceActive = true;
    _delayPhaseEndTime = currentTime + delayMs;
    _playPhaseEndTime = _delayPhaseEndTime + playMs;

    Log::Info(PSTR("MODE: Play sequence started. Delay: %lu ms, Play: %lu ms."), delayMs, playMs);

    if (delayMs > 0) {
        _isDelayPhase = true;
        if (_hwManager) {
             _hwManager->setMosfets(false);
             _hwManager->setLedPattern(LedPatternType::LED_OFF);
        }
        Log::Info(PSTR("MODE: Delay phase active."));
    } else {
        _isDelayPhase = false;
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
        if (_hwManager) _hwManager->setMosfets(false);
        if (_hwManager) _hwManager->setLedPattern(LedPatternType::LED_OFF);

        if (_currentCommandId != 0) {
             Log::Info(PSTR("COMM: Sequence %lu completed or stopped."), _currentCommandId);
        } else {
             Log::Debug(PSTR("MODE: Manual play sequence stopped."));
        }
        _currentCommandId = 0;

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

    uint8_t finalId;
    if (_temporaryId == 0) {
        finalId = _previousDeviceId; 
        Log::Info(PSTR("MODE: Temporary ID is 0, reverting to previous ID %d."), finalId);
    } else {
        finalId = _temporaryId;
    }

    updateDeviceId(finalId);

    Log::Info(PSTR("MODE: ID selection confirmed: %d. Starting 1s ON pattern."), finalId);
    _hwManager->setLedPattern(LedPatternType::LED_ID_SET_CONFIRM);
    _idBlinkPatternStarted = false;
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
        Log::TestLog("ID changed: %d", _deviceId);
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

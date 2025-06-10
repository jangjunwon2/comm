#include "hardware_t.h"
#include "utils_t.h" // logPrintf 사용을 위해
#include <algorithm> // std::max 사용을 위해 (이전 보정 로직 흔적이지만 유지)

//────────────────────────────────────────────────────────────────────────
// Global Variables and Objects
//────────────────────────────────────────────────────────────────────────
Adafruit_SSD1306 display(DISPLAY_WIDTH, DISPLAY_HEIGHT, &Wire, -1);
Button button1(BUTTON1_PIN), button2(BUTTON2_PIN), button3(BUTTON3_PIN), button4(BUTTON4_PIN);
bool viewingGroup = true;
unsigned long completionStartTime = 0;

// [MODIFIED] Helper to sort devices by delay time, then by ID.
static void sortRunningDevicesByDelay(RunningDevice arr[], uint8_t count) {
    if (count < 2) return;
    std::sort(arr, arr + count, [](const RunningDevice& a, const RunningDevice& b) {
        if (a.delayTime != b.delayTime) {
            return a.delayTime < b.delayTime;
        }
        return a.deviceID < b.deviceID; // Secondary sort by ID
    });
}

//────────────────────────────────────────────────────────────────────────
// Execution Start Logic
//────────────────────────────────────────────────────────────────────────
void prepareForExecution() {
    groupDeviceCount = 0;
    isProcessing = true;
    executionComplete = false;
    currentMode = EXECUTION_MODE;
    updateDisplay(); 
    delay(100);
}

void startSingleExecution(uint8_t deviceID, unsigned long buttonPressTime) {
    prepareForExecution();
    previousSelectedDevice = deviceID;

    if (deviceID < 1 || deviceID > MAX_DEVICES || !deviceSettings[deviceID].isValid()) {
        logPrintf(LogLevel::LOG_ERROR, "Cannot start: Invalid settings for ID %d", deviceID);
        isProcessing = false;
        currentMode = GENERAL_MODE;
        return;
    }

    RunningDevice& rd = runningDevices[0];
    rd.deviceID = deviceID;
    rd.delayTime = getTimerMs(deviceID, true);
    rd.playTime = getTimerMs(deviceID, false);
    rd.txButtonPressSequenceMicros = buttonPressTime;
    rd.commStatus = COMM_PENDING_RTT_REQUEST; // [MODIFIED] 초기 상태 변경
    rd.sendAttempts = 0;
    rd.successfulAcks = 0;
    rd.lastPacketSendTime = 0;
    rd.lastTxTimestamp = 0;
    // [REMOVED] rd.lastRttUs = g_lastKnownGlobalRttUs;
    // [REMOVED] rd.lastRxProcessingTimeUs = g_lastKnownGlobalRxProcessingTimeUs;
    rd.currentSequenceRttUs = 0; // [NEW] 현재 시퀀스 RTT 초기화
    rd.currentSequenceRxProcessingTimeUs = 0; // [NEW] 현재 시퀀스 Rx 처리 시간 초기화
    rd.isDelayCompleted = false;
    rd.isCompleted = false;
    rd.delayEndTime = 0;
    rd.playEndTime = 0;
    groupDeviceCount = 1;

    logPrintf(LogLevel::LOG_INFO, "COMM: Prepared single execution for ID %d. (RTT/RxProc는 현재 시퀀스에서 측정됨)", 
              deviceID);
}

void startGroupExecution(unsigned long buttonPressTime) {
    prepareForExecution();
    previousSelectedDevice = 0;

    for (uint8_t id = 1; id <= MAX_DEVICES; id++) {
        if (deviceSettings[id].inGroup && deviceSettings[id].isValid()) {
            if (groupDeviceCount >= MAX_GROUP_DEVICES) {
                logPrintf(LogLevel::LOG_WARN, "COMM: Group full. Cannot add ID %d", id);
                break;
            }
            RunningDevice& rd = runningDevices[groupDeviceCount];
            rd.deviceID = id;
            rd.delayTime = getTimerMs(id, true);
            rd.playTime = getTimerMs(id, false);
            rd.txButtonPressSequenceMicros = buttonPressTime;
            rd.commStatus = COMM_PENDING_RTT_REQUEST; // [MODIFIED] 초기 상태 변경
            rd.sendAttempts = 0;
            rd.successfulAcks = 0;
            rd.lastPacketSendTime = 0;
            rd.lastTxTimestamp = 0;
            // [REMOVED] rd.lastRttUs = g_lastKnownGlobalRttUs;
            // [REMOVED] rd.lastRxProcessingTimeUs = g_lastKnownGlobalRxProcessingTimeUs;
            rd.currentSequenceRttUs = 0; // [NEW] 현재 시퀀스 RTT 초기화
            rd.currentSequenceRxProcessingTimeUs = 0; // [NEW] 현재 시퀀스 Rx 처리 시간 초기화
            rd.isDelayCompleted = false;
            rd.isCompleted = false;
            rd.delayEndTime = 0;
            rd.playEndTime = 0;
            groupDeviceCount++;

            logPrintf(LogLevel::LOG_INFO, "COMM: Added device %d to group execution. (RTT/RxProc는 현재 시퀀스에서 측정됨)", 
                      id);
        }
    }

    if (groupDeviceCount == 0) {
        logPrintf(LogLevel::LOG_INFO, "COMM: No valid devices in group. Aborting.");
        isProcessing = false;
        currentMode = GENERAL_MODE;
        return;
    }
    
    sortRunningDevicesByDelay(runningDevices, groupDeviceCount); // 여전히 딜레이 시간 순으로 정렬 (RTT 요청은 동시에 보내고, 최종 명령은 딜레이가 짧은 순서로 보내면 더 효율적일 수 있으나, 현재는 RTT 요청-응답-최종 명령이 한 디바이스씩 진행되므로 순차 정렬은 의미가 없어짐)
    logPrintf(LogLevel::LOG_INFO, "COMM: Prepared group execution for %d devices.", groupDeviceCount);
}


//────────────────────────────────────────────────────────────────────────
// Main Execution and Mode Transition Logic
//────────────────────────────────────────────────────────────────────────
void checkExecutionAndMode() {
    unsigned long now = millis();

    if (currentMode == EXECUTION_MODE && isProcessing) {
        manageCommunication(); // Manages sending packets in the background
        bool allLocalTimersDone = true;

        for (uint8_t i = 0; i < groupDeviceCount; i++) {
            RunningDevice& rd = runningDevices[i];
            
            // [MODIFIED] 최종 명령 ACK를 받은 후에만 타이머 시작
            if (rd.commStatus == COMM_ACK_RECEIVED_SUCCESS && rd.delayEndTime == 0) {
                // RTT 및 RxProc 값을 사용하여 최종 지연 시간 계산 (이 부분은 수신부에서 진행되므로, 여기서는 단순히 실행 시작으로만 표시)
                // 송신부의 로컬 타이머는 이미 전체 지연 시간을 기준으로 동작하고 있음.
                // 그러나 사용자 요구사항에 따라 송신부 로컬 타이머는 보정값과 관계없이 설정된 딜레이/플레이 시간을 따름.
                // (이 부분이 실제 수신부 동작과 일치하도록 변경하려면 송신부도 수신부처럼 보정된 시간으로 타이머를 시작해야 함.
                //  하지만 현재는 송신부는 관리, 수신부는 실제 실행이므로, 송신부는 설정된 시간을 따르고 수신부는 보정된 시간을 따르는 것이 맞음.)
                rd.delayEndTime = now + rd.delayTime; // 송신부는 설정된 딜레이 시간 기준으로 로컬 타이머 시작
                rd.playEndTime = rd.delayEndTime + rd.playTime;
                logPrintf(LogLevel::LOG_INFO, "ID %d: 송신부 로컬 타이머 시작. (설정된 지연: %lu ms)", rd.deviceID, rd.delayTime);
            }

            if (!rd.isDelayCompleted && rd.delayEndTime > 0 && now >= rd.delayEndTime) {
                rd.isDelayCompleted = true;
                startMotorVibration(500, false);
                logPrintf(LogLevel::LOG_DEBUG, "ID %d: 송신부 로컬 딜레이 타이머 종료. 모터 ON.", rd.deviceID);
            }

            if (!rd.isCompleted && rd.playEndTime > 0 && now >= rd.playEndTime) {
                rd.isCompleted = true;
                logPrintf(LogLevel::LOG_DEBUG, "ID %d: 송신부 로컬 플레이 타이머 종료.", rd.deviceID);
            }
            
            if (!rd.isCompleted) {
                allLocalTimersDone = false;
            }
        }
        
        if (allLocalTimersDone) {
            logPrintf(LogLevel::LOG_INFO, "모든 송신부 로컬 타이머 종료. 완료 화면으로 전환.");
            currentMode = COMPLETION_MODE;
            completionStartTime = now;
            
            int successCount = 0;
            for (int k = 0; k < groupDeviceCount; ++k) {
                if (runningDevices[k].commStatus == COMM_ACK_RECEIVED_SUCCESS) successCount++;
            }
            startMotorVibration(successCount > 0 ? 300 : 600, successCount == 0);
        }
    }
    else if (currentMode == COMPLETION_MODE) {
        if (now - completionStartTime >= 500) { 
            logPrintf(LogLevel::LOG_INFO, "완료 화면 종료. GENERAL_MODE로 복귀.");
            currentMode = GENERAL_MODE;
            isProcessing = false;
            executionComplete = true; 
            
            viewingGroup = (previousSelectedDevice == 0);
            selectedDevice = previousSelectedDevice == 0 ? 1 : previousSelectedDevice;
            groupDeviceCount = 0;
        }
    }
}


//────────────────────────────────────────────────────────────────────────
// Display Functions
//────────────────────────────────────────────────────────────────────────
void displayExecutionMode() {
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    displayCenteredModeName("RUNNING");
    
    unsigned long now = millis();
    int linesDrawn = 0;

    RunningDevice displayDevices[MAX_GROUP_DEVICES];
    uint8_t displayCount = 0;

    for (uint8_t i = 0; i < groupDeviceCount; i++) {
        // [MODIFIED] COMM_FAILED_NO_ACK 상태인 장치도 표시 (실패 정보)
        if (runningDevices[i].commStatus != COMM_ACK_RECEIVED_SUCCESS && runningDevices[i].commStatus != COMM_FAILED_NO_ACK) {
             // 아직 진행 중인 장치
             displayDevices[displayCount++] = runningDevices[i];
        } else if (runningDevices[i].commStatus == COMM_FAILED_NO_ACK) {
            // 실패한 장치도 목록에 추가하여 표시
            displayDevices[displayCount++] = runningDevices[i];
        }
    }

    // [MODIFIED] Sorting logic updated to sort by remaining delay, then by ID.
    // [NEW] 실패한 장치는 목록의 마지막에 오도록 정렬
    if (displayCount > 1) {
        std::sort(displayDevices, displayDevices + displayCount, 
            [now](const RunningDevice& a, const RunningDevice& b) {
                // 실패한 장치를 가장 마지막에 배치
                if (a.commStatus == COMM_FAILED_NO_ACK && b.commStatus != COMM_FAILED_NO_ACK) return false;
                if (a.commStatus != COMM_FAILED_NO_ACK && b.commStatus == COMM_FAILED_NO_ACK) return true;
                if (a.commStatus == COMM_FAILED_NO_ACK && b.commStatus == COMM_FAILED_NO_ACK) return a.deviceID < b.deviceID; // 실패한 장치끼리는 ID 순

                // 진행 중인 장치들은 기존 로직대로 남은 시간으로 정렬
                unsigned long remainingA = 0;
                if (!a.isDelayCompleted && a.delayEndTime > 0 && a.delayEndTime > now) {
                    remainingA = a.delayEndTime - now;
                }
                unsigned long remainingB = 0;
                if (!b.isDelayCompleted && b.delayEndTime > 0 && b.delayEndTime > now) {
                    remainingB = b.delayEndTime - now;
                }

                if ((remainingA > 0) && (remainingB == 0)) return true;
                if ((remainingA == 0) && (remainingB > 0)) return false;

                if (remainingA != remainingB) {
                    return remainingA < remainingB;
                }
                
                return a.deviceID < b.deviceID;
            }
        );
    }
    
    for (uint8_t i = 0; i < displayCount; i++) {
        RunningDevice& rd = displayDevices[i];

        display.setCursor(0, 10 + (linesDrawn * LINE_HEIGHT));
        linesDrawn++;

        char lineBuffer[MAX_CHARS_PER_LINE + 2];
        
        // [MODIFIED] 통신 상태에 따라 표시 변경
        if (rd.commStatus == COMM_ACK_RECEIVED_SUCCESS) {
            // 성공적으로 통신 완료된 장치는 타이머가 종료되었는지 여부를 표시
            if (rd.isCompleted) {
                snprintf(lineBuffer, sizeof(lineBuffer), "ID:%02d/COMPLETE", rd.deviceID);
            } else if (rd.isDelayCompleted) {
                 snprintf(lineBuffer, sizeof(lineBuffer), "ID:%02d/PLAYING", rd.deviceID);
            } else {
                 snprintf(lineBuffer, sizeof(lineBuffer), "ID:%02d/ACK_OK", rd.deviceID);
            }
        } else if (rd.commStatus == COMM_FAILED_NO_ACK) {
            snprintf(lineBuffer, sizeof(lineBuffer), "ID:%02d/FAILED", rd.deviceID);
        } else {
            // 통신 진행 중인 장치 (딜레이 또는 플레이 시간 표시)
            unsigned long remainingDelayMs = 0;
            if (!rd.isDelayCompleted && rd.delayEndTime > 0 && rd.delayEndTime > now) {
                remainingDelayMs = rd.delayEndTime - now;
            }

            if (remainingDelayMs > 0) {
                unsigned long delayMinutes = remainingDelayMs / MS_PER_MIN;
                unsigned long delaySeconds = (remainingDelayMs % MS_PER_MIN) / MS_PER_SEC;
                snprintf(lineBuffer, sizeof(lineBuffer), "ID:%02d/D:%lum%02lus/P:%lus",
                         rd.deviceID,
                         delayMinutes,
                         delaySeconds,
                         rd.playTime / MS_PER_SEC);
            } else {
                unsigned long remainingPlayMs = 0;
                if (rd.delayEndTime > 0 && rd.playEndTime > 0 && rd.playEndTime > now) {
                    remainingPlayMs = rd.playEndTime - now;
                }
                unsigned long currentPlaySeconds = remainingPlayMs / MS_PER_SEC;
                snprintf(lineBuffer, sizeof(lineBuffer), "ID:%02d/P:%lus",
                         rd.deviceID,
                         currentPlaySeconds);
            }
            
            // 통신 상태 추가 표시 (예: RTT_REQ, WAIT_RTT, FINAL_CMD, WAIT_FINAL)
            switch (rd.commStatus) {
                case COMM_PENDING_RTT_REQUEST: snprintf(lineBuffer + strlen(lineBuffer), sizeof(lineBuffer) - strlen(lineBuffer), "/REQ_RTT"); break;
                case COMM_AWAITING_RTT_ACK: snprintf(lineBuffer + strlen(lineBuffer), sizeof(lineBuffer) - strlen(lineBuffer), "/WAIT_RTT"); break;
                case COMM_PENDING_FINAL_COMMAND: snprintf(lineBuffer + strlen(lineBuffer), sizeof(lineBuffer) - strlen(lineBuffer), "/REQ_CMD"); break;
                case COMM_AWAITING_FINAL_ACK: snprintf(lineBuffer + strlen(lineBuffer), sizeof(lineBuffer) - strlen(lineBuffer), "/WAIT_CMD"); break;
                default: break; // 그 외 상태는 표시 안 함
            }
        }
        display.println(lineBuffer);

        if (display.getCursorY() > DISPLAY_HEIGHT - LINE_HEIGHT) break;
    }
}


void displayCompletionMode() {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    
    int successCount = 0;
    for (int i = 0; i < groupDeviceCount; ++i) {
        if (runningDevices[i].commStatus == COMM_ACK_RECEIVED_SUCCESS) successCount++;
    }
    
    char primaryMsg[20];
    if (groupDeviceCount == 0)      snprintf(primaryMsg, sizeof(primaryMsg), "NO DEV");
    else if (successCount == groupDeviceCount) snprintf(primaryMsg, sizeof(primaryMsg), "COMPLETE");
    else if (successCount > 0)      snprintf(primaryMsg, sizeof(primaryMsg), "PARTIAL");
    else                            snprintf(primaryMsg, sizeof(primaryMsg), "FAILED");

    display.setTextSize(2);
    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds(primaryMsg, 0, 0, &x1, &y1, &w, &h);
    display.setCursor((DISPLAY_WIDTH - w) / 2, (DISPLAY_HEIGHT - h) / 2);
    display.println(primaryMsg);
}

void displayCenteredModeName(const char* modeName) {
  String name = String(modeName);
  int nameLen = name.length();
  int totalPad = MAX_CHARS_PER_LINE - nameLen;
  if (totalPad < 0) totalPad = 0;
  int padSide = totalPad / 2;
  int extra = totalPad % 2;
  String header = "";
  for (int i = 0; i < padSide; i++) header += '=';
  header += name;
  for (int i = 0; i < padSide + extra; i++) header += '=';
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print(header);
}

void updateDisplay() {
  if (!oledInitialized) return;
  display.clearDisplay();
  switch (currentMode) {
    case GENERAL_MODE:          displayGeneralMode(); break;
    case GROUP_SETTING_MODE:    displayGroupSettingMode(); break;
    case TIMER_SETTING_MODE:    displayTimerSettingMode(selectedDevice, deviceSettings[selectedDevice].delayMinutes, deviceSettings[selectedDevice].delaySeconds, deviceSettings[selectedDevice].playSeconds, adjustingDelayTimer ? 0 : 1); break;
    case DETAILED_SETTING_MODE: displayDetailedSettingMode(selectedDevice, deviceSettings[selectedDevice].delayMinutes, deviceSettings[selectedDevice].delaySeconds, adjustingDelayTimer, adjustingUnit == UNIT_MINUTES ? 0 : 1); break;
    case ADJUSTING_VALUE_MODE:  displayAdjustingValueMode(); break;
    case EXECUTION_MODE:        displayExecutionMode(); break;
    case COMPLETION_MODE:       displayCompletionMode(); break;
  }
  display.display();
}

// Button Class implementation
Button::Button(uint8_t p) : pin(p), pressed(false), state(HIGH), lastState(HIGH), lastDebounceTime(0), lastPressTime(0), debounceDelay(BUTTON_DEBOUNCE_TIME), lastActionTime(0), isHolding(false), countInterval(BUTTON_INITIAL_INTERVAL), pressCount(0) {}
void Button::begin() { pinMode(pin, INPUT_PULLUP); }
void Button::update() {
  bool currentState = digitalRead(pin);
  unsigned long now = millis();
  if (currentState != lastState) { lastDebounceTime = now; }
  if (now - lastDebounceTime >= debounceDelay) {
    if (currentState != state) {
      state = currentState;
      if (state == LOW) {
        lastPressTime = now;
        pressed = true;
        isHolding = false;
        pressCount = 0;
        countInterval = BUTTON_INITIAL_INTERVAL;
        lastActionTime = now;
      } else { isHolding = false; }
    }
  }
  if (state == LOW && !isHolding && (now - lastPressTime >= BUTTON_HOLD_TIME)) {
    isHolding = true;
    lastActionTime = now;
  }
  if (isHolding) {
    if (now - lastActionTime >= countInterval) {
      lastActionTime = now;
      if(shouldCount()) {
      }
    }
  }
  lastState = currentState;
}
bool Button::isPressed() { if(pressed){ pressed = false; return true; } return false; }
bool Button::checkHold() { return isHolding; }
bool Button::shouldCount() {
  if (!isHolding) return false;
  unsigned long now = millis();
  if (now - lastActionTime >= countInterval) {
      lastActionTime = now;
      pressCount++;
      if (pressCount >= 5) { countInterval = BUTTON_FAST_INTERVAL; }
      else if (pressCount >= 3) { countInterval = BUTTON_SLOW_INTERVAL; }
      return true;
  }
  return false;
}
void Button::resetPressCount() { pressCount = 0; countInterval = BUTTON_INITIAL_INTERVAL; }

// Hardware initialization
bool initDisplay() {
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) { return false; }
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(10, 20);
  display.println(F("MysticLab"));
  display.display();
  delay(1500);
  oledInitialized = true;
  return true;
}
static struct { unsigned long startTime; unsigned long duration; bool isError; } motorState = { 0, 0, false };
void initVibrationMotor() { pinMode(VIB_MOTOR_PIN, OUTPUT); digitalWrite(VIB_MOTOR_PIN, LOW); }
void startMotorVibration(uint16_t duration, bool isError) {
  motorState.startTime = millis();
  motorState.duration = duration;
  motorState.isError = isError;
  digitalWrite(VIB_MOTOR_PIN, HIGH);
}
void updateVibrationMotor() {
  if (motorState.startTime > 0) {
    if (millis() - motorState.startTime >= motorState.duration) {
      digitalWrite(VIB_MOTOR_PIN, LOW);
      motorState.startTime = 0;
    }
  }
}
bool initHardware() {
  bool displayOK = initDisplay();
  button1.begin();
  button2.begin();
  button3.begin();
  button4.begin();
  initVibrationMotor();
  initEEPROM();
  loadSettings();
  selectedDevice = 1;
  viewingGroup = true;
  return displayOK;
}
void updateButtons() { button1.update(); button2.update(); button3.update(); button4.update(); }

// Button handlers
void handleButtons() {
    if (currentMode == GENERAL_MODE) {
        handleGeneralModeButtons();
    } else if (currentMode == GROUP_SETTING_MODE) {
        handleGroupSettingModeButtons();
    } else if (currentMode == TIMER_SETTING_MODE) {
        handleTimerSettingModeButtons();
    } else if (currentMode == DETAILED_SETTING_MODE) {
        handleDetailedSettingModeButtons();
    } else if (currentMode == ADJUSTING_VALUE_MODE) {
        handleAdjustingValueModeButtons();
    }

    // PLAY 버튼 (BUTTON4) 처리 - 모든 모드에서 공통
    if (button4.isPressed() && !isProcessing) {
        if (currentMode == GENERAL_MODE) {
            // 버튼이 눌린 시점의 micros() 값을 캡처
            unsigned long buttonPressTime = micros();
            
            if (viewingGroup) {
                logPrintf(LogLevel::LOG_INFO, "COMM: Starting group execution at %lu us", buttonPressTime);
                startGroupExecution(buttonPressTime);
            } else {
                logPrintf(LogLevel::LOG_INFO, "COMM: Starting single execution for device %d at %lu us", 
                         selectedDevice, buttonPressTime);
                startSingleExecution(selectedDevice, buttonPressTime);
            }
        }
    }
}
void handleGeneralModeButtons() {
    // UP 버튼 (BUTTON1) 처리
    if (button1.isPressed()) {
        if (viewingGroup) {
            // 그룹 모드에서는 그룹 멤버십만 변경
            for (uint8_t id = 1; id <= MAX_DEVICES; id++) {
                if (deviceSettings[id].inGroup) {
                    deviceSettings[id].inGroup = false;
                    saveSettings(true); // 그룹 설정만 저장
                    logPrintf(LogLevel::LOG_INFO, "Device %d removed from group", id);
                }
            }
            viewingGroup = false;
        } else {
            // 단일 장치 모드에서는 다음 장치로 이동
            if (selectedDevice < MAX_DEVICES) {
                selectedDevice++;
            }
        }
        updateDisplay();
    }

    // DOWN 버튼 (BUTTON2) 처리
    if (button2.isPressed()) {
        if (viewingGroup) {
            // 그룹 모드에서는 그룹 멤버십만 변경
            for (uint8_t id = 1; id <= MAX_DEVICES; id++) {
                if (!deviceSettings[id].inGroup && deviceSettings[id].isValid()) {
                    deviceSettings[id].inGroup = true;
                    saveSettings(true); // 그룹 설정만 저장
                    logPrintf(LogLevel::LOG_INFO, "Device %d added to group", id);
                }
            }
            viewingGroup = false;
        } else {
            // 단일 장치 모드에서는 이전 장치로 이동
            if (selectedDevice > 1) {
                selectedDevice--;
            }
        }
        updateDisplay();
    }

    // MODE 버튼 (BUTTON3) 처리
    if (button3.isPressed()) {
        viewingGroup = !viewingGroup;
        updateDisplay();
    }

    // PLAY 버튼 (BUTTON4) 처리는 handleButtons()로 이동
}

void handleGroupSettingModeButtons() {
    if (button1.isPressed()) { 
        currentMode = GENERAL_MODE; 
        viewingGroup = true;
    }
    else if (button2.isPressed()) { selectedDevice = (selectedDevice == MAX_DEVICES) ? 1 : selectedDevice + 1; }
    else if (button3.isPressed()) { selectedDevice = (selectedDevice == 1) ? MAX_DEVICES : selectedDevice - 1; }
    else if (button4.isPressed()) { 
        deviceSettings[selectedDevice].inGroup = !deviceSettings[selectedDevice].inGroup; 
        saveSettings(true);
    }
}

void handleTimerSettingModeButtons() {
    if (button1.isPressed()) { currentMode = GENERAL_MODE; }
    else if (button2.isPressed() || button3.isPressed()) { adjustingDelayTimer = !adjustingDelayTimer; }
    else if (button4.isPressed()) {
        currentMode = adjustingDelayTimer ? DETAILED_SETTING_MODE : ADJUSTING_VALUE_MODE;
        if(adjustingDelayTimer) adjustingUnit = UNIT_MINUTES;
        else adjustingUnit = UNIT_SECONDS;
    }
}

void handleDetailedSettingModeButtons() {
    if (button1.isPressed()) { currentMode = TIMER_SETTING_MODE; }
    else if (button2.isPressed() || button3.isPressed()) { adjustingUnit = (adjustingUnit == UNIT_MINUTES) ? UNIT_SECONDS : UNIT_MINUTES; }
    else if (button4.isPressed()) { currentMode = ADJUSTING_VALUE_MODE; }
}

void handleAdjustingValueModeButtons() {
    if (button1.isPressed()) {
        saveSettings(false);
        currentMode = adjustingDelayTimer ? DETAILED_SETTING_MODE : TIMER_SETTING_MODE;
        button2.resetPressCount(); button3.resetPressCount();
    } 
    else if (button2.isPressed() || button2.shouldCount()) {
        increaseTimerValue();
    }
    else if (button3.isPressed() || button3.shouldCount()) {
        decreaseTimerValue();
    }
}

// Display functions
void displayGeneralMode() {
    if(viewingGroup) {
        displayCenteredModeName("GROUP MODE");
        display.setCursor(0, 10);
        RunningDevice tempDevices[MAX_GROUP_DEVICES];
        uint8_t tempCount = 0;
        for (uint8_t i = 1; i <= MAX_DEVICES; i++) {
            if (deviceSettings[i].inGroup) {
                if (tempCount < MAX_GROUP_DEVICES) {
                    tempDevices[tempCount].deviceID = i;
                    tempDevices[tempCount].delayTime = getTimerMs(i, true);
                    tempCount++;
                }
            }
        }
        if (tempCount > 1) sortRunningDevicesByDelay(tempDevices, tempCount);
        for (uint8_t i = 0; i < tempCount; i++) {
            uint8_t id = tempDevices[i].deviceID;
            display.printf("ID:%02d/D:%dm%02ds/P:%ds\n", id, deviceSettings[id].delayMinutes, deviceSettings[id].delaySeconds, deviceSettings[id].playSeconds);
            if (display.getCursorY() > DISPLAY_HEIGHT - LINE_HEIGHT) break;
        }

    } else { 
        displayCenteredModeName("GENERAL MODE");
        display.setCursor(0, 16);
        display.printf("ID   : %d\n", selectedDevice);
        display.printf("Delay: %dm %ds\n", deviceSettings[selectedDevice].delayMinutes, deviceSettings[selectedDevice].delaySeconds);
        display.printf("Play : %ds\n", deviceSettings[selectedDevice].playSeconds);
        display.printf("Group: %s\n", deviceSettings[selectedDevice].inGroup ? "YES" : "NO");
    }
}

void displayGroupSettingMode() {
    displayCenteredModeName("GROUP SETTING");
    display.setCursor(0, 16);
    display.printf("ID   : %d\n", selectedDevice);
    display.printf("Delay: %dm %ds\n", deviceSettings[selectedDevice].delayMinutes, deviceSettings[selectedDevice].delaySeconds);
    display.printf("Play : %ds\n", deviceSettings[selectedDevice].playSeconds);
    display.printf("Group: %s <\n", deviceSettings[selectedDevice].inGroup ? "YES" : "NO");
}

void displayTimerSettingMode(uint8_t id, uint8_t dMin, uint8_t dSec, uint8_t pSec, uint8_t field) {
    displayCenteredModeName("TIMER SETTING");
    display.setCursor(0, 16);
    display.printf("Device ID: %d", id);
    display.setCursor(TEXT_X, 32);
    display.printf("Delay: %02dm %02ds", dMin, dSec);
    display.setCursor(TEXT_X, 42);
    display.printf("Play : %02ds", pSec);
    display.setCursor(CURSOR_X, field == 0 ? 32 : 42);
    display.print(">");
}

void displayDetailedSettingMode(uint8_t id, uint8_t dMin, uint8_t dSec, bool isDelayMode, uint8_t subField) {
    displayCenteredModeName("DELAY SETTING");
    display.setCursor(0, 16);
    display.printf("Device ID: %d", id);
    display.setCursor(TEXT_X, 32);
    display.printf("Minutes: %02d", dMin);
    display.setCursor(TEXT_X, 42);
    display.printf("Seconds: %02d", dSec);
    display.setCursor(CURSOR_X, subField == 0 ? 32 : 42);
    display.print(">");
}
void displayAdjustingValueMode() {
    displayCenteredModeName("ADJUST VALUE");
    display.setCursor(0, 16);
    display.printf("ID:%2d ", selectedDevice);
    
    if(adjustingDelayTimer){
        if(adjustingUnit == UNIT_MINUTES) {
            display.printf("Delay Min: %02dm", deviceSettings[selectedDevice].delayMinutes);
        } else {
            display.printf("Delay Sec: %02ds", deviceSettings[selectedDevice].delaySeconds);
        }
    } else {
        display.printf("Play Sec: %02ds", deviceSettings[selectedDevice].playSeconds);
    }
    display.setCursor(DISPLAY_WIDTH/2 - 10, 40);
    display.setTextSize(2);
    display.print("UP/DOWN");
    display.setTextSize(1);
}

// Timer value manipulation
void increaseTimerValue() {
    if (adjustingDelayTimer) {
        if (adjustingUnit == UNIT_MINUTES) {
            deviceSettings[selectedDevice].delayMinutes = (deviceSettings[selectedDevice].delayMinutes + 1) % (MAX_DELAY_MINUTES + 1);
        } else {
             deviceSettings[selectedDevice].delaySeconds = (deviceSettings[selectedDevice].delaySeconds + 1) % (MAX_DELAY_SECONDS + 1);
        }
    } else {
        deviceSettings[selectedDevice].playSeconds++;
        if (deviceSettings[selectedDevice].playSeconds > MAX_PLAY_SECONDS) deviceSettings[selectedDevice].playSeconds = MIN_PLAY_SECONDS;
    }
    startMotorVibration(50, false);
}

void decreaseTimerValue() {
    if (adjustingDelayTimer) {
        if (adjustingUnit == UNIT_MINUTES) {
            if(deviceSettings[selectedDevice].delayMinutes == 0) deviceSettings[selectedDevice].delayMinutes = MAX_DELAY_MINUTES;
            else deviceSettings[selectedDevice].delayMinutes--;
        } else {
            if(deviceSettings[selectedDevice].delaySeconds == 0) deviceSettings[selectedDevice].delaySeconds = MAX_DELAY_SECONDS;
            else deviceSettings[selectedDevice].delaySeconds--;
        }
    } else {
        if (deviceSettings[selectedDevice].playSeconds <= MIN_PLAY_SECONDS) deviceSettings[selectedDevice].playSeconds = MAX_PLAY_SECONDS;
        else deviceSettings[selectedDevice].playSeconds--;
    }
    startMotorVibration(50, false);
}

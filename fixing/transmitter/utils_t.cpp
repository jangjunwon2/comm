#include "utils_t.h"
#include "config_t.h"
#include "espnow_t.h"
#include <Arduino.h>
#include <cstdarg>
#include <algorithm>

// =============================================================================
//                              로그 구현
// =============================================================================
static LogLevel currentLogLevel = LogLevel::LOG_DEBUG;

void logPrintf(LogLevel level, const char *format, ...) {
    if (level > currentLogLevel) return;
    char buf[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    Serial.println(buf);
}

// =============================================================================
//                          모드 관리 구현
// =============================================================================

static SystemMode currentMode;
static NormalView normalView;
static SettingStep settingStep;
static int selectedId;
static int cursorPos;
static int subCursorPos;
static unsigned long completeMessageStartTime;

void saveCurrentSettings() {
    Config::getInstance().save();
    logPrintf(LogLevel::LOG_INFO, "Settings saved to EEPROM.");
}

void startIndividualRun(int id) {
    logPrintf(LogLevel::LOG_INFO, "Requesting Individual Run for ID: %d", id);
    currentMode = SystemMode::RUNNING;
    
    std::vector<DeviceSettings> devicesToRun;
    devicesToRun.push_back(Config::getInstance().getDeviceSettings()[id - 1]);

    EspNow::getInstance().startCommunication(devicesToRun, micros());
}

void startGroupRun() {
    logPrintf(LogLevel::LOG_INFO, "Requesting Group Run.");
    currentMode = SystemMode::RUNNING;

    std::vector<DeviceSettings> devicesToRun;
    DeviceSettings* allSettings = Config::getInstance().getDeviceSettings();
    for (int i = 0; i < MAX_DEVICES; ++i) {
        if (allSettings[i].inGroup) {
            devicesToRun.push_back(allSettings[i]);
        }
    }

    if (devicesToRun.empty()) {
        logPrintf(LogLevel::LOG_WARN, "No devices in group. Returning.");
        currentMode = SystemMode::NORMAL;
        return;
    }
    
    EspNow::getInstance().startCommunication(devicesToRun, micros());
}

void completeRun() {
    logPrintf(LogLevel::LOG_INFO, "Run complete.");
    currentMode = SystemMode::SHOW_COMPLETE;
    completeMessageStartTime = millis();
}

void handleNormalInput(ButtonId button) {
    switch (button) {
        case ButtonId::BTN_UP:
            selectedId = (selectedId > 0) ? selectedId - 1 : MAX_DEVICES;
            normalView = (selectedId == 0) ? NormalView::GROUP : NormalView::INDIVIDUAL;
            break;
        case ButtonId::BTN_DOWN:
            selectedId = (selectedId < MAX_DEVICES) ? selectedId + 1 : 0;
            normalView = (selectedId == 0) ? NormalView::GROUP : NormalView::INDIVIDUAL;
            break;
        case ButtonId::BTN_SET:
            currentMode = SystemMode::SETTING;
            if (selectedId == 0) {
                settingStep = SettingStep::GROUP_CONFIG;
                selectedId = 1;
            } else {
                settingStep = SettingStep::TIME_SELECT;
                cursorPos = 0;
            }
            break;
        case ButtonId::BTN_PLAY:
            if (selectedId == 0) startGroupRun();
            else startIndividualRun(selectedId);
            break;
        default: break;
    }
}

void handleSettingInput(ButtonId button) {
    DeviceSettings* settings = Config::getInstance().getDeviceSettings();
    if (selectedId < 1 || selectedId > MAX_DEVICES) return;

    switch (settingStep) {
        case SettingStep::GROUP_CONFIG:
            if (button == ButtonId::BTN_UP) selectedId = (selectedId == MAX_DEVICES) ? 1 : selectedId + 1;
            if (button == ButtonId::BTN_DOWN) selectedId = (selectedId == 1) ? MAX_DEVICES : selectedId - 1;
            if (button == ButtonId::BTN_PLAY) settings[selectedId - 1].inGroup = !settings[selectedId - 1].inGroup;
            if (button == ButtonId::BTN_SET) {
                saveCurrentSettings();
                currentMode = SystemMode::NORMAL;
                normalView = NormalView::GROUP;
                selectedId = 0;
            }
            break;

        case SettingStep::TIME_SELECT:
            if (button == ButtonId::BTN_UP || button == ButtonId::BTN_DOWN) cursorPos = 1 - cursorPos;
            if (button == ButtonId::BTN_PLAY) {
                if (cursorPos == 0) {
                    settingStep = SettingStep::TIME_DETAIL_SELECT;
                    subCursorPos = 0;
                } else {
                    settingStep = SettingStep::TIME_VALUE_ADJUST;
                    subCursorPos = 2;
                }
            }
            if (button == ButtonId::BTN_SET) {
                saveCurrentSettings();
                currentMode = SystemMode::NORMAL;
            }
            break;

        case SettingStep::TIME_DETAIL_SELECT:
            if (button == ButtonId::BTN_UP || button == ButtonId::BTN_DOWN) subCursorPos = 1 - subCursorPos;
            if (button == ButtonId::BTN_PLAY) settingStep = SettingStep::TIME_VALUE_ADJUST;
            if (button == ButtonId::BTN_SET) settingStep = SettingStep::TIME_SELECT;
            break;

        case SettingStep::TIME_VALUE_ADJUST: {
            DeviceSettings& device = settings[selectedId - 1];
            int* value_ptr = nullptr;
            int max_val = 0, min_val = 0;

            if (subCursorPos == 0) { value_ptr = &device.delay_m; max_val = 59; }
            else if (subCursorPos == 1) { value_ptr = &device.delay_s; max_val = 59; }
            else { value_ptr = &device.play_s; max_val = 60; min_val = 1; }

            if (button == ButtonId::BTN_UP) *value_ptr = (*value_ptr >= max_val) ? min_val : *value_ptr + 1;
            if (button == ButtonId::BTN_DOWN) *value_ptr = (*value_ptr <= min_val) ? max_val : *value_ptr - 1;
            if (button == ButtonId::BTN_SET) {
                if (subCursorPos == 2) settingStep = SettingStep::TIME_SELECT;
                else settingStep = SettingStep::TIME_DETAIL_SELECT;
            }
            break;
        }
    }
}

void initModes() {
    currentMode = SystemMode::NORMAL;
    normalView = NormalView::GROUP;
    selectedId = 0;
    cursorPos = 0;
    subCursorPos = 0;
    logPrintf(LogLevel::LOG_INFO, "Mode Logic Initialized.");
}

void handleModeInput(ButtonId button) {
    if (button == ButtonId::BTN_NONE) return;

    switch (currentMode) {
        case SystemMode::NORMAL: 
            handleNormalInput(button); 
            break;
        case SystemMode::SETTING: // [오류 수정] 'SettingStep::SETTING'을 'SystemMode::SETTING'으로 변경
            handleSettingInput(button); 
            break;
        default: 
            break;
    }
}

void updateModes() {
    if (currentMode == SystemMode::RUNNING) {
        if (EspNow::getInstance().isCommunicationDone()) {
            completeRun();
        }
    } else if (currentMode == SystemMode::SHOW_COMPLETE) {
        if (millis() - completeMessageStartTime > 500) {
            currentMode = SystemMode::NORMAL;
        }
    }
}

SystemMode getSystemMode() { return currentMode; }
NormalView getNormalView() { return normalView; }
SettingStep getSettingStep() { return settingStep; }
int getSelectedId() { return selectedId; }
int getCursorPos() { return cursorPos; }
int getSubCursorPos() { return subCursorPos; }
const std::vector<RunningDevice>& getRunningDevices() { 
    return EspNow::getInstance().getRunningDeviceStates();
}

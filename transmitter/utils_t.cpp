#include "utils_t.h"
#include "config_t.h"
#include <EEPROM.h>
#include <stdarg.h>

//────────────────────────────────────────────────────────────────────────────
// 1) Logging Functions
//────────────────────────────────────────────────────────────────────────────
void initLog() {
    Serial.begin(115200);
    delay(100);
    Serial.println("\n\nLogging initialized");
}

void logPrintf(LogLevel level, const char* format, ...) {
    if (!Serial) return;
    
    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    const char* levelStr;
    switch (level) {
        case LogLevel::LOG_DEBUG: levelStr = "DEBUG"; break;
        case LogLevel::LOG_INFO:  levelStr = "INFO";  break;
        case LogLevel::LOG_WARN:  levelStr = "WARN";  break;
        case LogLevel::LOG_ERROR: levelStr = "ERROR"; break;
        default: levelStr = "UNKNOWN";
    }
    
    Serial.printf("[%s] %s\n", levelStr, buffer);
}

//────────────────────────────────────────────────────────────────────────────
// 2) Timer Calculation Functions
//────────────────────────────────────────────────────────────────────────────
uint32_t getTimerMs(uint8_t deviceID, bool isDelay) {
    if (deviceID > MAX_DEVICES) return 0;
    
    DeviceSettings& settings = deviceSettings[deviceID];
    if (isDelay) {
        return (uint32_t)(settings.delayMinutes) * MS_PER_MIN + (uint32_t)(settings.delaySeconds) * MS_PER_SEC;
    } else {
        return (uint32_t)settings.playSeconds * MS_PER_SEC;
    }
}

// [REMOVED] getCorrectedDelay 함수는 더 이상 사용되지 않으므로 정의를 제거합니다.
/*
// 응답 시간(RTT)의 절반을 보정값으로 사용
uint32_t getCorrectedDelay(uint8_t deviceID) {
    if (deviceID > MAX_DEVICES) return 0;
    
    uint32_t baseDelay = getTimerMs(deviceID, true);
    uint32_t rtt_us = responseTimes[deviceID]; 

    if (rtt_us > 0) {
        int32_t correction_ms = rtt_us / 2000; 
        logPrintf(LogLevel::LOG_DEBUG, "ID %d Correction: RTT=%uus, Correction=%dms", deviceID, rtt_us, correction_ms);
        if (baseDelay > correction_ms) {
            return baseDelay - correction_ms;
        }
    }
    return baseDelay;
}
*/

//────────────────────────────────────────────────────────────────────────────
// 3) EEPROM Initialization and ID/Group ID Management
//────────────────────────────────────────────────────────────────────────────
void initEEPROM() {
    EEPROM.begin(EEPROM_SIZE);
    delay(100);
    logPrintf(LogLevel::LOG_INFO, "EEPROM initialized");
}

uint8_t loadID() {
    return EEPROM.read(DEVICE_ID_ADDR);
}

void saveID(uint8_t id) {
    EEPROM.write(DEVICE_ID_ADDR, id);
    EEPROM.commit();
}

uint8_t loadGroupID() {
    return EEPROM.read(GROUP_ID_ADDR);
}

void saveGroupID(uint8_t groupId) {
    EEPROM.write(GROUP_ID_ADDR, groupId);
    EEPROM.commit();
}

//────────────────────────────────────────────────────────────────────────────
// 4) Settings (Load/Save) Functions
//────────────────────────────────────────────────────────────────────────────
void loadSettings() {
    bool anyDefaultWasSet = false;
    
    for (uint8_t i = 1; i <= MAX_DEVICES; i++) {
        uint16_t baseAddr = SETTINGS_START_ADDR + (i * sizeof(DeviceSettings));
        
        deviceSettings[i].delayMinutes = EEPROM.read(baseAddr);
        deviceSettings[i].delaySeconds = EEPROM.read(baseAddr + 1);
        deviceSettings[i].playSeconds = EEPROM.read(baseAddr + 2);
        deviceSettings[i].inGroup = EEPROM.read(baseAddr + 3);

        bool needsDefault = false;
        
        if (deviceSettings[i].delayMinutes == 255 && 
            deviceSettings[i].delaySeconds == 255 && 
            deviceSettings[i].playSeconds == 255 &&
            deviceSettings[i].inGroup == 255) {
            needsDefault = true;
        }
        else if (deviceSettings[i].delayMinutes > MAX_DELAY_MINUTES ||
                 deviceSettings[i].delaySeconds > MAX_DELAY_SECONDS ||
                 deviceSettings[i].playSeconds > MAX_PLAY_SECONDS ||
                 (deviceSettings[i].playSeconds < MIN_PLAY_SECONDS && deviceSettings[i].playSeconds != 0) ||
                 deviceSettings[i].inGroup > 1) {
            needsDefault = true;
        }

        if (needsDefault) {
            deviceSettings[i].delayMinutes = 0;
            deviceSettings[i].delaySeconds = 0;
            deviceSettings[i].playSeconds = MIN_PLAY_SECONDS; 
            deviceSettings[i].inGroup = false;
            
            EEPROM.write(baseAddr, deviceSettings[i].delayMinutes);
            EEPROM.write(baseAddr + 1, deviceSettings[i].delaySeconds);
            EEPROM.write(baseAddr + 2, deviceSettings[i].playSeconds);
            EEPROM.write(baseAddr + 3, deviceSettings[i].inGroup);
            
            anyDefaultWasSet = true;
            logPrintf(LogLevel::LOG_INFO, "Device %d: Default settings applied and saved.", i);
        }
    }
    
    if (anyDefaultWasSet) {
        EEPROM.commit();
    }
    
    logPrintf(LogLevel::LOG_INFO, "Settings loaded from EEPROM");
}

void saveSettings(bool saveGroupOnly) {
    // saveGroupOnly가 true이면 그룹 멤버십만 저장하고, false이면 현재 선택된 장치의 모든 설정을 저장
    if (saveGroupOnly) {
        uint16_t baseAddr = SETTINGS_START_ADDR + (selectedDevice * sizeof(DeviceSettings));
        EEPROM.write(baseAddr + 3, deviceSettings[selectedDevice].inGroup);
        logPrintf(LogLevel::LOG_INFO, "Group setting for ID %d saved.", selectedDevice);

    } else { 
        uint16_t baseAddr = SETTINGS_START_ADDR + (selectedDevice * sizeof(DeviceSettings));
        EEPROM.write(baseAddr, deviceSettings[selectedDevice].delayMinutes);
        EEPROM.write(baseAddr + 1, deviceSettings[selectedDevice].delaySeconds);
        EEPROM.write(baseAddr + 2, deviceSettings[selectedDevice].playSeconds);
        logPrintf(LogLevel::LOG_INFO, "Timer settings for ID %d saved.", selectedDevice);
    }
    EEPROM.commit();
}

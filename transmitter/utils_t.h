#pragma once
#ifndef UTILS_T_H
#define UTILS_T_H

#include <Arduino.h>
#include "config_t.h" // LogLevel 열거형을 가져오기 위해 포함

// [FIXED] The definition for LogLevel has been removed from this file 
// to prevent "multiple definition" errors. It's now only defined in config_t.h.

//────────────────────────────────────────────────────────────────────────────
// 1) Logging Functions
//────────────────────────────────────────────────────────────────────────────
void initLog();
// This function uses LogLevel, which is correctly defined in the included config_t.h
void logPrintf(LogLevel level, const char* format, ...);

//────────────────────────────────────────────────────────────────────────────
// 2) Timer Calculation Functions
//────────────────────────────────────────────────────────────────────────────
uint32_t getTimerMs(uint8_t deviceID, bool isDelay);
// [REMOVED] getCorrectedDelay 함수는 더 이상 사용되지 않으므로 선언을 제거합니다.
// uint32_t getCorrectedDelay(uint8_t deviceID);

//────────────────────────────────────────────────────────────────────────────
// 3) EEPROM Initialization and ID/Group ID Management
//────────────────────────────────────────────────────────────────────────────
void initEEPROM();
uint8_t loadID();
void saveID(uint8_t id);
uint8_t loadGroupID();
void saveGroupID(uint8_t groupId);

//────────────────────────────────────────────────────────────────────────────
// 4) Settings (Load/Save) Functions
//────────────────────────────────────────────────────────────────────────────
void loadSettings();
void saveSettings(bool saveGroupOnly);

#endif // UTILS_T_H

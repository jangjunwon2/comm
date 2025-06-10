#ifndef UTILS_T_H
#define UTILS_T_H

#include <Arduino.h>
#include <vector>
#include "hardware_t.h" // ButtonId enum을 사용하기 위함 (오류 수정)

// 로그 레벨 (중복 정의 해결)
enum class LogLevel {
    LOG_NONE,
    LOG_ERROR,
    LOG_WARN,
    LOG_INFO,
    LOG_DEBUG
};

// 시스템 모드 관련 정의 (중복 정의 해결)
enum class SystemMode {
    NORMAL,
    SETTING,
    RUNNING,
    SHOW_COMPLETE
};

enum class NormalView {
    GROUP,
    INDIVIDUAL
};

enum class SettingStep {
    GROUP_CONFIG,
    TIME_SELECT,
    TIME_DETAIL_SELECT,
    TIME_VALUE_ADJUST
};

struct RunningDevice {
    int id;
    unsigned long delayEndTime;
    unsigned long playEndTime;
    bool isDelayDone;
};


// --- 함수 선언 ---

// 로그 유틸리티
void logPrintf(LogLevel level, const char *format, ...);

// 모드 관리
void initModes();
void handleModeInput(ButtonId button); // 오류 수정: 'ButtonId' 타입을 명시
void updateModes();

// 상태 Getter (다른 파일에서 현재 상태를 읽기 위함)
SystemMode getSystemMode();
NormalView getNormalView();
SettingStep getSettingStep();
int getSelectedId();
int getCursorPos();
int getSubCursorPos();
const std::vector<RunningDevice>& getRunningDevices();

#endif // UTILS_T_H

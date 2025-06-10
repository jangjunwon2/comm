#ifndef CONFIG_T_H
#define CONFIG_T_H

#include <Arduino.h>
#include <EEPROM.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h> 
#include "espnow_comm_shared.h"

//────────────────────────────────────────────────────────────────────────────
// 1) 핀 정의
//────────────────────────────────────────────────────────────────────────────
#define VIB_MOTOR_PIN         5
#define I2C_SDA_PIN           8
#define I2C_SCL_PIN           9
#define OLED_ADDRESS          0x3C
#define BUTTON1_PIN           2 // SET
#define BUTTON2_PIN           4 // UP
#define BUTTON3_PIN           3 // DOWN
#define BUTTON4_PIN           1 // PLAY


//────────────────────────────────────────────────────────────────────────────
// 2) 상수 정의
//────────────────────────────────────────────────────────────────────────────
#define MAX_DEVICES           10 // [수정됨] 향후 확장성을 위해 증가
#define MAX_GROUP_DEVICES     10 // [수정됨] 향후 확장성을 위해 증가
#define EEPROM_SIZE           512
#define MAX_DELAY_MINUTES     59
#define MAX_DELAY_SECONDS     59
#define MIN_PLAY_SECONDS      1
#define MAX_PLAY_SECONDS      60
#define DEVICE_ID_ADDR        400
#define GROUP_ID_ADDR         401
#define SETTINGS_START_ADDR   100
#define MS_PER_SEC            1000UL
#define MS_PER_MIN            (60 * MS_PER_SEC)
#define BUTTON_DEBOUNCE_TIME    50
#define BUTTON_HOLD_TIME        800
#define BUTTON_INITIAL_INTERVAL 500 
#define BUTTON_SLOW_INTERVAL    200
#define BUTTON_FAST_INTERVAL    50
#define DISPLAY_WIDTH         128
#define DISPLAY_HEIGHT        64
#define MAX_CHARS_PER_LINE    21


//────────────────────────────────────────────────────────────────────────────
// 3) ESP-NOW 관련 상수
//────────────────────────────────────────────────────────────────────────────
#define WIFI_CHANNEL            1
#define ACK_TIMEOUT_MS          200
#define RETRY_INTERVAL_MS       300
#define MAX_SEND_ATTEMPTS       5

static const uint8_t broadcastAddress[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

//────────────────────────────────────────────────────────────────────────────
// 4) 열거형
//────────────────────────────────────────────────────────────────────────────
// [수정됨] LOG_WARN의 명시적인 값 할당을 제거하여 LOG_INFO와 중복되지 않도록 함
enum class LogLevel { LOG_DEBUG = 0, LOG_INFO, LOG_WARN, LOG_ERROR }; 
enum ErrorCode { ERROR_NONE = 0, ERROR_INIT_FAILED, ERROR_INVALID_SETTINGS, ERROR_EXECUTION_FAILED };
enum Mode { GENERAL_MODE = 0, GROUP_SETTING_MODE, TIMER_SETTING_MODE, DETAILED_SETTING_MODE, ADJUSTING_VALUE_MODE, EXECUTION_MODE, COMPLETION_MODE };
enum TimerUnit { UNIT_MINUTES = 0, UNIT_SECONDS };

enum CommStatus {
    COMM_IDLE,
    COMM_PENDING_INITIAL_SEND,
    COMM_AWAITING_ACK,
    COMM_ACK_RECEIVED_SUCCESS,
    COMM_ACK_FAILED_RETRYING,
    COMM_FAILED_NO_ACK
};

//────────────────────────────────────────────────────────────────────────────
// 5) 구조체
//────────────────────────────────────────────────────────────────────────────
struct DeviceSettings {
    uint8_t delayMinutes;
    uint8_t delaySeconds;
    uint8_t playSeconds;
    bool    inGroup;
    bool isValid() const { return playSeconds >= MIN_PLAY_SECONDS && playSeconds <= MAX_PLAY_SECONDS; }
};

struct RunningDevice {
    uint8_t deviceID;
    uint32_t delayTime; // 원본 지연 시간 (ms)
    uint32_t playTime;  // 플레이 시간 (ms)
    unsigned long delayEndTime; // 로컬 지연 종료 시간 (millis())
    unsigned long playEndTime;  // 로컬 플레이 종료 시간 (millis())
    bool isDelayCompleted;
    bool isCompleted;
    CommStatus commStatus;
    uint8_t sendAttempts;
    uint8_t successfulAcks;
    unsigned long lastPacketSendTime; // 마지막 패킷 전송 시점 (millis())
    unsigned long ackTimeoutDeadline; // ACK 타임아웃 기한 (millis())
    uint32_t lastTxTimestamp; // 마지막 패킷의 txMicros 값 (micros())
    uint32_t lastRttUs; // 마지막으로 측정된 RTT (micros())
    uint32_t lastRxProcessingTimeUs; // 마지막으로 측정된 수신부 처리 시간 (micros())
    uint32_t txButtonPressSequenceMicros; // [NEW] 이 실행 시퀀스가 시작된 버튼 누름 시점 (micros())
};

//────────────────────────────────────────────────────────────────────────────
// 6) 전역 변수 외부 선언
//────────────────────────────────────────────────────────────────────────────
extern DeviceSettings deviceSettings[MAX_DEVICES + 1];
extern RunningDevice  runningDevices[MAX_GROUP_DEVICES + 1];
extern uint8_t        groupDeviceCount;
extern Mode           currentMode;
extern uint8_t        selectedDevice;
extern uint8_t        previousSelectedDevice;
extern bool           adjustingDelayTimer;
extern TimerUnit      adjustingUnit;
extern bool           isProcessing;
extern unsigned long  executionCompleteTime;
extern bool           oledInitialized;
extern bool           espNowInitialized;
extern bool           executionComplete; 

// [NEW] 전역적으로 마지막으로 알려진 RTT 및 RxProcessingTime 저장
extern uint32_t       g_lastKnownGlobalRttUs;
extern uint32_t       g_lastKnownGlobalRxProcessingTimeUs;

#endif // CONFIG_T_H

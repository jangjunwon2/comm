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
enum class LogLevel { LOG_DEBUG = 0, LOG_INFO, LOG_WARN, LOG_ERROR }; 
enum ErrorCode { ERROR_NONE = 0, ERROR_INIT_FAILED, ERROR_INVALID_SETTINGS, ERROR_EXECUTION_FAILED };
enum Mode { GENERAL_MODE = 0, GROUP_SETTING_MODE, TIMER_SETTING_MODE, DETAILED_SETTING_MODE, ADJUSTING_VALUE_MODE, EXECUTION_MODE, COMPLETION_MODE };
enum TimerUnit { UNIT_MINUTES = 0, UNIT_SECONDS };

// [MODIFIED] 통신 상태 열거형 업데이트
enum CommStatus {
    COMM_IDLE,                     // 유휴 상태
    COMM_PENDING_RTT_REQUEST,      // RTT 요청 패킷 전송 대기 중
    COMM_AWAITING_RTT_ACK,         // RTT 요청 ACK 대기 중
    COMM_PENDING_FINAL_COMMAND,    // 최종 명령 패킷 전송 대기 중
    COMM_AWAITING_FINAL_ACK,       // 최종 명령 ACK 대기 중
    COMM_ACK_RECEIVED_SUCCESS,     // 모든 ACK 성공적으로 수신
    COMM_FAILED_NO_ACK             // 모든 재시도 후 ACK 수신 실패
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
    uint32_t lastTxTimestamp;         // 마지막 전송 패킷의 txMicros 값 (micros())
    uint32_t txButtonPressSequenceMicros; // 이 실행 시퀀스가 시작된 버튼 누름 시점 (micros())
    
    // [NEW] 현재 시퀀스 내에서 측정된 RTT 및 Rx 처리 시간 (최종 명령 패킷에 포함될 값)
    uint32_t currentSequenceRttUs; 
    uint32_t currentSequenceRxProcessingTimeUs;
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

// [REMOVED] g_lastKnownGlobalRttUs 및 g_lastKnownGlobalRxProcessingTimeUs 전역 변수 제거

#endif // CONFIG_T_H

/**
 * @file config.h
 * @brief 모든 프로젝트 상수 및 설정을 위한 중앙 설정 파일입니다.
 *
 * @version 1.0.0
 * @date 2025-06-10
 */
#pragma once
#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include <IPAddress.h>

// --- 펌웨어 버전 ---
#define FIRMWARE_VERSION "1.0.0"

// --- 디버깅 ---
#define DEBUG_MODE true 

// --- 워치독 타이머 ---
#define WDT_TIMEOUT_S 30 

// --- GPIO 핀 정의 ---
#define ID_BUTTON_PIN       4
#define EXEC_BUTTON_PIN     5
#define MOSFET_PIN_1        6
#define MOSFET_PIN_2        7
#define LED_PIN             8

// --- 버튼 타이밍 및 설정 ---
#define DEBOUNCE_DELAY_MS           50
#define SHORT_PRESS_THRESHOLD_MS    2000
#define LONG_PRESS_THRESHOLD_MS     2000
#define ID_SET_TIMEOUT_MS           5000

// --- LED 타이밍 및 패턴 ---
#define LED_ID_BLINK_INTERVAL_MS         200
#define LED_ID_SET_ENTER_ON_MS           1000
#define LED_ID_SET_INCREMENT_BLINK_MS    100
#define LED_ID_SET_CONFIRM_ON_MS         1000
#define LED_WIFI_MODE_BLINK_INTERVAL_MS  500
#define LED_WIFI_MODE_BLINK_COUNT        3
#define LED_BOOT_SUCCESS_ON_MS           1000 // 부팅 성공 시 1초 점등

// --- 장치 ID 설정 ---
#define DEFAULT_DEVICE_ID   1
#define MIN_DEVICE_ID       1
#define MAX_DEVICE_ID       10 

// --- ESP-NOW 설정 ---
#define ESP_NOW_CHANNEL     1
static const uint8_t BROADCAST_ADDRESS[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

// --- WI-FI 및 WEB UI 설정 ---
#define AP_SSID                 "Mystic_Lab"
#define AP_PASSWORD             nullptr
#define AP_IP                   IPAddress(192, 168, 4, 1)
#define WIFI_MODE_AUTO_EXIT_MS  (5 * 60 * 1000)

// --- OTA 업데이트 URL ---
#define OTA_VERSION_URL     "https://jangjunwon2.github.io/update/version.json"
#define OTA_FIRMWARE_URL    "https://jangjunwon2.github.io/update/firmware.bin"
#define OTA_HTTP_TIMEOUT_MS 10000

// --- 테스트 모드 설정 ---
#define DEFAULT_TEST_DELAY_MS 0
#define DEFAULT_TEST_PLAY_MS  1000

// ====================================================================================
//                            전역 열거형 및 구조체
// ====================================================================================

enum class DeviceMode {
    MODE_BOOT,
    MODE_NORMAL,
    MODE_ID_BLINK,
    MODE_ID_SET,
    MODE_WIFI,
    MODE_TEST,
    MODE_ERROR
};

enum class ButtonEventType {
    NO_EVENT,
    ID_BUTTON_SHORT_PRESS,
    ID_BUTTON_LONG_PRESS_END,
    EXEC_BUTTON_PRESS,
    EXEC_BUTTON_RELEASE,
    BOTH_BUTTONS_LONG_PRESS
};

enum class LedPatternType {
    LED_OFF,
    LED_ON,
    LED_BOOT_SUCCESS,
    LED_ID_DISPLAY,
    LED_ID_SET_ENTER,
    LED_ID_SET_INCREMENT,
    LED_ID_SET_CONFIRM,
    LED_WIFI_MODE_TOGGLE,
    LED_ERROR
};

#endif // CONFIG_H

/**
 * @file utils.h
 * @brief 유틸리티 클래스(Log, NVS)의 헤더 파일입니다.
 * @version 7.0.0
 * @date 2024-06-13
 */
#pragma once
#ifndef UTILS_H
#define UTILS_H

#include <Arduino.h>
#include <Preferences.h>
#include <functional>
#include <esp_task_wdt.h>
#include "config.h"

class WebManager;

// --- Log 클래스 ---
class Log {
public:
    using WsLogSender = std::function<void(const String&)>;
    static void begin(); // [NEW] Mutex 초기화를 위한 함수
    static void Info(const char* format, ...);
    static void Warn(const char* format, ...);
    static void Error(const char* format, ...);
    static void Debug(const char* format, ...);

private:
    friend class WebManager;
    static void setWebSocketLogSender(const WsLogSender& sender);
    static void printLog(const char* level, const char* format, va_list args);
    static WsLogSender _wsLogSender;
    static SemaphoreHandle_t _logMutex; // [NEW] 로그 출력을 위한 Mutex
};

// --- 워치독 타이머 헬퍼 ---
inline void enableWatchdog(uint32_t timeoutS = WDT_TIMEOUT_S) {
    esp_task_wdt_config_t wdt_config = {
        .timeout_ms = timeoutS * 1000,
        .idle_core_mask = (1 << 0) | (1 << 1),
        .trigger_panic = true,
    };
    
    esp_err_t init_result = esp_task_wdt_init(&wdt_config);
    if (init_result == ESP_ERR_INVALID_STATE) {
        if (esp_task_wdt_reconfigure(&wdt_config) != ESP_OK) {
             // 시리얼이 아직 준비되지 않았을 수 있으므로 직접 출력
            ets_printf("ERROR: Failed to reconfigure WDT\n");
        }
    } else if (init_result != ESP_OK) {
        ets_printf("ERROR: Failed to init WDT: %s\n", esp_err_to_name(init_result));
    }

    if (esp_task_wdt_add(NULL) != ESP_OK) {
        ets_printf("ERROR: Failed to add loop task to WDT\n");
    }
}

// --- NVS 클래스 ---
class NVS {
public:
    static bool initNvs();
    static uint8_t loadDeviceId();
    static void saveDeviceId(uint8_t id);
    static String loadWifiSsid();
    static void saveWifiSsid(const String& ssid);
    static String loadWifiPassword();
    static void saveWifiPassword(const String& password);
    static uint32_t loadTestDelay();
    static void saveTestDelay(uint32_t delayMs);
    static uint32_t loadTestPlay();
    static void saveTestPlay(uint32_t playMs);
    
private:
    static Preferences preferences;
    static const char* PREFS_NAMESPACE;
    static const char* KEY_DEVICE_ID;
    static const char* KEY_WIFI_SSID;
    static const char* KEY_WIFI_PASS;
    static const char* KEY_TEST_DELAY;
    static const char* KEY_TEST_PLAY;
};

// --- JSON 문서 크기 ---
constexpr size_t JSON_DOC_SIZE_WS_LOG = 384;
constexpr size_t JSON_DOC_SIZE_STATUS = 768;
constexpr size_t JSON_DOC_SIZE_API_RESP = 256;
constexpr size_t JSON_DOC_SIZE_WIFI_SCAN = 2048;

// --- 유틸리티 함수 ---
inline bool isVersionNewer(const String& latest, const String& current) {
    if (latest.isEmpty() || current.isEmpty() || latest == "N/A" || latest == current) {
        return false;
    }
    int current_idx = 0, latest_idx = 0;
    while (current_idx < current.length() || latest_idx < latest.length()) {
        long current_val = 0;
        int current_part_end = current.indexOf('.', current_idx);
        if (current_part_end == -1) current_part_end = current.length();
        if (current_idx < current.length()) {
           current_val = current.substring(current_idx, current_part_end).toInt();
        }
        long latest_val = 0;
        int latest_part_end = latest.indexOf('.', latest_idx);
        if (latest_part_end == -1) latest_part_end = latest.length();
        if (latest_idx < latest.length()) {
            latest_val = latest.substring(latest_idx, latest_part_end).toInt();
        }
        if (latest_val > current_val) return true;
        if (latest_val < current_val) return false;
        current_idx = current_part_end + 1;
        latest_idx = latest_part_end + 1;
    }
    return false;
}

#endif // UTILS_H
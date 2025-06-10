// =========================================================================
// utils.cpp
// =========================================================================

#include "utils.h"
#include <ArduinoJson.h>

// 정적 멤버 초기화
Log::WsLogSender Log::_wsLogSender = nullptr;
SemaphoreHandle_t Log::_logMutex = NULL; // [NEW] Mutex 핸들

const char* NVS::PREFS_NAMESPACE = "mystic_lab";
const char* NVS::KEY_DEVICE_ID = "dev_id";
const char* NVS::KEY_WIFI_SSID = "wifi_ssid";
const char* NVS::KEY_WIFI_PASS = "wifi_pass";
const char* NVS::KEY_TEST_DELAY = "test_delay";
const char* NVS::KEY_TEST_PLAY = "test_play";
Preferences NVS::preferences;

// --- Log 구현 ---

void Log::begin() {
    // [NEW] 로그 시스템을 위한 Mutex 생성
    _logMutex = xSemaphoreCreateMutex();
}

void Log::setWebSocketLogSender(const WsLogSender& sender) { _wsLogSender = sender; }

// [FIXED] Added Mutex to prevent race conditions
void Log::printLog(const char* level, const char* format, va_list args) {
    if (xSemaphoreTake(_logMutex, portMAX_DELAY) == pdTRUE) {
        char buffer[256];
        vsnprintf(buffer, sizeof(buffer), format, args);
        
        // 시리얼이 사용 가능할 때만 출력
        if (Serial) {
            Serial.printf("[%lu ms][%s] %s\n", millis(), level, buffer);
        }

        if (_wsLogSender) {
            StaticJsonDocument<JSON_DOC_SIZE_WS_LOG> doc;
            doc["type"] = "log"; doc["ts"] = millis(); doc["level"] = level; doc["msg"] = buffer;
            String wsOutput;
            serializeJson(doc, wsOutput);
            _wsLogSender(wsOutput);
        }
        xSemaphoreGive(_logMutex);
    }
}
void Log::Info(const char* format, ...) { va_list args; va_start(args, format); printLog("INFO", format, args); va_end(args); }
void Log::Warn(const char* format, ...) { va_list args; va_start(args, format); printLog("WARN", format, args); va_end(args); }
void Log::Error(const char* format, ...) { va_list args; va_start(args, format); printLog("ERROR", format, args); va_end(args); }
void Log::Debug(const char* format, ...) {
#if DEBUG_MODE
    va_list args; va_start(args, format); printLog("DEBUG", format, args); va_end(args);
#endif
}

// --- NVS 구현 ---
bool NVS::initNvs() { return preferences.begin(PREFS_NAMESPACE, false); }
uint8_t NVS::loadDeviceId() { return preferences.getUChar(KEY_DEVICE_ID, DEFAULT_DEVICE_ID); }
void NVS::saveDeviceId(uint8_t id) { if(loadDeviceId() != id) preferences.putUChar(KEY_DEVICE_ID, id); }
String NVS::loadWifiSsid() { return preferences.getString(KEY_WIFI_SSID, ""); }
void NVS::saveWifiSsid(const String& ssid) { if(loadWifiSsid() != ssid) preferences.putString(KEY_WIFI_SSID, ssid); }
String NVS::loadWifiPassword() { return preferences.getString(KEY_WIFI_PASS, ""); }
void NVS::saveWifiPassword(const String& password) { preferences.putString(KEY_WIFI_PASS, password); }
uint32_t NVS::loadTestDelay() { return preferences.getUInt(KEY_TEST_DELAY, DEFAULT_TEST_DELAY_MS); }
void NVS::saveTestDelay(uint32_t delayMs) { if(loadTestDelay() != delayMs) preferences.putUInt(KEY_TEST_DELAY, delayMs); }
uint32_t NVS::loadTestPlay() { return preferences.getUInt(KEY_TEST_PLAY, DEFAULT_TEST_PLAY_MS); }
void NVS::saveTestPlay(uint32_t playMs) { if(loadTestPlay() != playMs) preferences.putUInt(KEY_TEST_PLAY, playMs); }

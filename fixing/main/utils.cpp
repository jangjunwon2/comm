// =========================================================================
// utils.cpp
// =========================================================================

#include "utils.h"
#include <ArduinoJson.h>

// 정적 멤버 초기화
Log::WsLogSender Log::_wsLogSender = nullptr;
SemaphoreHandle_t Log::_logMutex = NULL;

const char* NVS::PREFS_NAMESPACE = "mystic_lab";
const char* NVS::KEY_DEVICE_ID = "dev_id";
const char* NVS::KEY_WIFI_SSID = "wifi_ssid";
const char* NVS::KEY_WIFI_PASS = "wifi_pass";
const char* NVS::KEY_TEST_DELAY = "test_delay";
const char* NVS::KEY_TEST_PLAY = "test_play";
Preferences NVS::preferences;

// --- Log 구현 ---

void Log::begin() {
    _logMutex = xSemaphoreCreateMutex();
}

void Log::setWebSocketLogSender(const WsLogSender& sender) { _wsLogSender = sender; }

void Log::printLog(const char* level, const char* format, va_list args) {
    if (xSemaphoreTake(_logMutex, portMAX_DELAY) == pdTRUE) {
        char buffer[256];
        vsnprintf(buffer, sizeof(buffer), format, args);
        
        // 시리얼이 사용 가능할 때만 출력
        if (Serial) {
            Serial.printf("[%lu ms][%s] %s\n", millis(), level, buffer);
        }

        // [MODIFIED] WebSocket sender now also receives the level
        if (_wsLogSender) {
            _wsLogSender(String(buffer), level);
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
// [NEW] Added TestLog for simplified output
void Log::TestLog(const char* format, ...) { 
    va_list args; va_start(args, format); printLog("TEST", format, args); va_end(args);
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

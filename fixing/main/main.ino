/**
 * @file main.ino
 * @brief 수신기 펌웨어의 메인 파일
 * @version 1.1.0
 * @date 2024-06-14
 */

#include "config.h"
#include "utils.h"
#include "hardware.h"
#include "web.h"
#include "comm.h"
#include "mode.h"

// 전역 객체 선언
HardwareManager hwManager;
WebManager webManager;
CommManager commManager;
ModeManager modeManager(&hwManager, &commManager, &webManager);

void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 1000); 
    
    Log::begin();
    Log::Info(PSTR("========================================="));
    Log::Info(PSTR("    Firmware Version: %s"), FIRMWARE_VERSION);
    Log::Info(PSTR("========================================="));

    enableWatchdog();

    if (!NVS::initNvs()) {
        Log::Error(PSTR("NVS: Failed to initialize NVS."));
    }

    hwManager.begin();
    
    // [FIX] 초기화 순서 수정: CommManager를 먼저 초기화합니다.
    uint8_t deviceId = NVS::loadDeviceId();
    commManager.begin(deviceId, &modeManager);
    
    webManager.begin(&modeManager); 
    modeManager.begin(); 

    Log::Info(PSTR("SYSTEM: Setup complete. Starting main loop."));
    
    // 부팅이 완료되면 자동으로 일반 모드로 전환
    modeManager.switchToMode(DeviceMode::MODE_NORMAL);
}

void loop() {
    modeManager.update();
    esp_task_wdt_reset();
    vTaskDelay(pdMS_TO_TICKS(10));
}

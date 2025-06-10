/**
 * @file main.ino
 * @brief Mystic Lab 장치의 메인 애플리케이션 진입점입니다.
 *
 * 모든 관리자(Manager)를 초기화하고 메인 루프를 실행합니다.
 * 또한 워치독 타이머를 주기적으로 리셋하여 시스템 안정성을 보장합니다.
 *
 * @version 1.0.0
 * @date 2025-06-10
 */

#include "config.h"
#include "utils.h"
#include "hardware.h"
#include "comm.h"
#include "web.h"
#include "mode.h"

// --- 전역 관리자 인스턴스 ---
HardwareManager hardwareManager;
CommManager commManager;
WebManager webManager;
ModeManager modeManager(&hardwareManager, &commManager, &webManager);

/**
 * @brief 시스템 초기 설정을 수행합니다.
 */
void setup() {
    Serial.begin(115200);
    delay(100); 

    Log::begin(); // [FIXED] Initialize log mutex

    Log::Info(PSTR("========================================="));
    Log::Info(PSTR("    Mystic Lab Device Booting Up"));
    Log::Info(PSTR("    Firmware Version: %s"), FIRMWARE_VERSION);
    Log::Info(PSTR("========================================="));
    
    enableWatchdog();
    Log::Info(PSTR("Watchdog enabled."));

    if (!NVS::initNvs()) {
        Log::Error(PSTR("NVS initialization failed. Halting."));
        while(1);
    }
    
    hardwareManager.begin();
    commManager.begin(NVS::loadDeviceId(), &modeManager);
    webManager.begin(&modeManager);
    modeManager.begin();
    
    Log::Info(PSTR("System setup complete. Switching to NORMAL mode."));
    modeManager.switchToMode(DeviceMode::MODE_NORMAL);
}

/**
 * @brief 메인 루프. 주기적으로 ModeManager를 업데이트하고 워치독을 리셋합니다.
 */
void loop() {
    modeManager.update();
    esp_task_wdt_reset();
    delay(5); 
}

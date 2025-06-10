#include <Arduino.h>
#include "config_t.h"
#include "utils_t.h"
#include "hardware_t.h"
#include "espnow_t.h" // espNow 인스턴스를 위해 포함

// 각 모듈의 인스턴스를 가져옵니다.
Config& config = Config::getInstance();
Hardware& hardware = Hardware::getInstance();
EspNow& espNow = EspNow::getInstance();

void setup() {
    Serial.begin(115200);
    logPrintf(LogLevel::LOG_INFO, "Transmitter Booting...");

    // 각 모듈 초기화
    config.init();
    hardware.init();
    espNow.init();
    initModes(); // utils.h 에 선언된 모드 초기화 함수

    logPrintf(LogLevel::LOG_INFO, "Transmitter Setup Complete.");
}

void loop() {
    // 1. 버튼 입력 등 하드웨어 상태 업데이트
    hardware.update();

    // 2. 버튼 입력에 따라 현재 모드의 로직 처리
    handleModeInput(hardware.getPressedButton());

    // 3. 현재 모드의 상태 업데이트 (타이머 등)
    updateModes();

    // 4. 현재 상태를 OLED 화면에 표시
    hardware.updateDisplay();

    // 시스템 부하 감소
    delay(10);

    // [수정] 실행 중일 때만 통신 상태를 관리하도록 호출
    if (getSystemMode() == SystemMode::RUNNING) {
        espNow.manageCommunication();
    }
}

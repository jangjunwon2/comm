#include "config_t.h"
#include "utils_t.h"
#include "hardware_t.h"
#include "espnow_t.h"

//========================================================================
// SETUP
//========================================================================
void setup() {
    // 로그 시스템 초기화
    initLog();
    logPrintf(LogLevel::LOG_INFO, "시스템 부팅 중...");
    
    // 하드웨어 초기화 시도
    if (!initHardware()) {
        logPrintf(LogLevel::LOG_ERROR, "하드웨어 초기화 실패!");
        while (true); // 시스템 정지
    }

    // ESP-NOW 초기화 시도
    if (!initEspNow()) {
        logPrintf(LogLevel::LOG_ERROR, "ESP-NOW 초기화 실패!");
        display.clearDisplay();
        display.setCursor(0,0);
        display.println("ESP-NOW Init FAILED");
        display.display();
        while(true); // 시스템 정지
    }

    logPrintf(LogLevel::LOG_INFO, "시스템 설정 완료.");
    updateDisplay(); // 디스플레이 업데이트
}

//========================================================================
// LOOP
//========================================================================
void loop() {
    unsigned long loopStartTime = millis(); // 현재 루프 시작 시간 기록

    // 1. 하드웨어 상태 업데이트
    updateButtons();         // 버튼 상태 업데이트
    updateVibrationMotor();  // 진동 모터 상태 업데이트

    // 2. 사용자 입력 및 모드 로직 처리
    handleButtons(); // 버튼 이벤트 처리

    // 3. 실행 상태 변경 및 타이머 확인
    checkExecutionAndMode(); // 실행 모드 및 타이머 관리

    // 4. 변경 사항이 있으면 디스플레이 업데이트
    // 효율성을 위해 각 함수 내에서 처리되지만, 필요한 경우 주기적인 업데이트를 강제할 수 있습니다.
    static unsigned long lastDisplayUpdateTime = 0;
    if (loopStartTime - lastDisplayUpdateTime > 50) { // 대략 초당 20회 디스플레이 업데이트
        updateDisplay();
        lastDisplayUpdateTime = loopStartTime;
    }
}

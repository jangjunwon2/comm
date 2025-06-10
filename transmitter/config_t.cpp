#include "config_t.h"

//────────────────────────────────────────────────────────────────────────────
// Global Variable Definitions
//────────────────────────────────────────────────────────────────────────────

// DeviceSettings, RunningDevice arrays (index 0 unused for ID 1-based indexing)
DeviceSettings deviceSettings[MAX_DEVICES + 1];
RunningDevice  runningDevices[MAX_DEVICES + 1];
uint8_t        groupDeviceCount = 0;

// Mode and State variables
Mode           currentMode = GENERAL_MODE;
uint8_t        selectedDevice = 1;
uint8_t        previousSelectedDevice = 0;
bool           adjustingDelayTimer = false;
TimerUnit      adjustingUnit = UNIT_MINUTES;
bool           isProcessing = false;
bool           executionComplete = false;
bool           oledInitialized = false;
unsigned long  executionCompleteTime = 0;

// ESP-NOW related global variables
bool           espNowInitialized = false;

// [NEW] 전역적으로 마지막으로 알려진 RTT 및 RxProcessingTime 초기화
uint32_t g_lastKnownGlobalRttUs = 0;
uint32_t g_lastKnownGlobalRxProcessingTimeUs = 0;

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

// [REMOVED] g_lastKnownGlobalRttUs 및 g_lastKnownGlobalRxProcessingTimeUs 정의 제거


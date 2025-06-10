#ifndef HARDWARE_T_H
#define HARDWARE_T_H

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "config_t.h"
#include "utils_t.h"
#include "espnow_t.h" // [FIXED] Added include for manageCommunication() declaration

//────────────────────────────────────────────────────────────────────────
// Button Class Declaration
//────────────────────────────────────────────────────────────────────────
class Button {
private:
    uint8_t pin;
    bool pressed;
    bool state;
    bool lastState;
    unsigned long lastDebounceTime;
    unsigned long lastPressTime;
    unsigned long debounceDelay;
    unsigned long lastActionTime;
    bool isHolding;
    unsigned long countInterval;
    uint8_t pressCount;

public:
    Button(uint8_t p);
    void begin();
    void update();
    bool isPressed();
    bool checkHold();
    bool shouldCount();
    void resetPressCount();
};

//────────────────────────────────────────────────────────────────────────
// Extern Object Declarations
//────────────────────────────────────────────────────────────────────────
extern Button button1, button2, button3, button4;
extern Adafruit_SSD1306 display;
extern bool viewingGroup;
extern bool executionComplete; // 실행 완료 상태

//────────────────────────────────────────────────────────────────────────
// Initialization Functions
//────────────────────────────────────────────────────────────────────────
bool initHardware();
bool initDisplay();
void initVibrationMotor();

//────────────────────────────────────────────────────────────────────────
// Update Functions (called in main loop)
//────────────────────────────────────────────────────────────────────────
void updateButtons();
void updateDisplay();
void updateVibrationMotor();
void checkExecutionAndMode();

//────────────────────────────────────────────────────────────────────────
// Button Handling Functions
//────────────────────────────────────────────────────────────────────────
void handleButtons();
void handleGeneralModeButtons();
void handleGroupSettingModeButtons();
void handleTimerSettingModeButtons();
void handleDetailedSettingModeButtons();
void handleAdjustingValueModeButtons();
void handleExecutionModeButtons();
void handleCompletionModeButtons();

//────────────────────────────────────────────────────────────────────────
// Display Functions
//────────────────────────────────────────────────────────────────────────
void displayGeneralMode();
void displayGroupSettingMode();
void displayTimerSettingMode(uint8_t deviceId, uint8_t dMin, uint8_t dSec, uint8_t pSec, uint8_t field);
void displayDetailedSettingMode(uint8_t id, uint8_t dMin, uint8_t dSec, bool isDelayMode, uint8_t subField);
void displayAdjustingValueMode();
void displayExecutionMode();
void displayCompletionMode();
void displayCenteredModeName(const char* modeName);

//────────────────────────────────────────────────────────────────────────
// Other Hardware Control
//────────────────────────────────────────────────────────────────────────
void increaseTimerValue();
void decreaseTimerValue();
void startMotorVibration(uint16_t duration, bool isError);

//────────────────────────────────────────────────────────────────────────
// Execution Logic
//────────────────────────────────────────────────────────────────────────
void startGroupExecution();
void startSingleExecution(uint8_t deviceID);
void startSingleExecution(uint8_t deviceID, unsigned long buttonPressTime);
void startGroupExecution(unsigned long buttonPressTime);
void prepareForExecution();

// Display coordinate constants
static const int CURSOR_X = 0;
static const int TEXT_X = 12;
static const int LINE_HEIGHT = 10;

#endif // HARDWARE_T_H

#ifndef HARDWARE_T_H
#define HARDWARE_T_H

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
// #include "utils_t.h" // 순환 참조 방지를 위해 이 줄은 주석 처리 또는 삭제 상태를 유지합니다.

// 버튼 ID
enum class ButtonId {
  BTN_NONE,
  BTN_UP,
  BTN_DOWN,
  BTN_SET,
  BTN_PLAY
};

// 버튼 핀 정의
#define BTN_UP_PIN    3
#define BTN_DOWN_PIN  4
#define BTN_SET_PIN   5
#define BTN_PLAY_PIN  6
#define VIBRATOR_PIN  7

// OLED 설정
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1


class Hardware {
public:
  static Hardware& getInstance();
  void init();
  void update();
  void updateDisplay();
  ButtonId getPressedButton();
  void vibrate(int duration_ms);

private:
  Hardware() : display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET) {}
  Hardware(const Hardware&) = delete;
  void operator=(const Hardware&) = delete;

  void initOLED();
  void initButtons();
  void initVibrator();
  void readButtons(); // 이 함수의 내부 로직이 변경됩니다.

  // 디스플레이 그리기 함수들
  void displayNormalIndividual();
  void displayNormalGroup();
  void displaySettingGroup();
  void displaySettingTimeSelect();
  void displaySettingTimeDetail();
  void displaySettingTimeValue();
  void displayRunning();
  void displayComplete();

  Adafruit_SSD1306 display;

  // [수정] 더 명확하고 표준적인 디바운싱을 위한 변수
  uint32_t last_raw_state[4] = {HIGH, HIGH, HIGH, HIGH}; // 이전 루프의 실제 핀 상태
  uint32_t stable_state[4] = {HIGH, HIGH, HIGH, HIGH};   // 디바운싱 후 안정된 버튼 상태
  uint32_t button_last_debounce_time[4] = {0, 0, 0, 0};
  ButtonId last_pressed_button = ButtonId::BTN_NONE;
  const int debounce_delay = 50;
};

#endif // HARDWARE_T_H

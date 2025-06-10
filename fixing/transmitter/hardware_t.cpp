#include "hardware_t.h"
#include "config_t.h"
#include "utils_t.h"
#include <Wire.h>
#include <vector>
#include <algorithm>

Hardware& Hardware::getInstance() {
  static Hardware instance;
  return instance;
}

void Hardware::init() {
  initOLED();
  initButtons();
  initVibrator();
}

void Hardware::update() {
  readButtons();
}

ButtonId Hardware::getPressedButton() {
    ButtonId temp = last_pressed_button;
    if (temp != ButtonId::BTN_NONE) {
      last_pressed_button = ButtonId::BTN_NONE;
    }
    return temp;
}

void Hardware::vibrate(int duration_ms) {
    digitalWrite(VIBRATOR_PIN, HIGH);
    delay(duration_ms);
    digitalWrite(VIBRATOR_PIN, LOW);
}

void Hardware::readButtons() {
    int pins[] = {BTN_UP_PIN, BTN_DOWN_PIN, BTN_SET_PIN, BTN_PLAY_PIN};
    ButtonId ids[] = {ButtonId::BTN_UP, ButtonId::BTN_DOWN, ButtonId::BTN_SET, ButtonId::BTN_PLAY};

    for (int i = 0; i < 4; i++) {
        if (last_pressed_button != ButtonId::BTN_NONE) {
            break; 
        }
        int reading = digitalRead(pins[i]);
        if (reading != last_raw_state[i]) {
            button_last_debounce_time[i] = millis();
        }
        if ((millis() - button_last_debounce_time[i]) > debounce_delay) {
            if (reading != stable_state[i]) {
                stable_state[i] = reading; 
                if (stable_state[i] == LOW) {
                    last_pressed_button = ids[i];
                    logPrintf(LogLevel::LOG_DEBUG, "Button %d Pressed (Stable)", (int)ids[i]);
                }
            }
        }
        last_raw_state[i] = reading;
    }
}

void Hardware::initOLED() {
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;);
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Booting...");
  display.display();
  delay(500);
}

void Hardware::initButtons() {
  pinMode(BTN_UP_PIN, INPUT_PULLUP);
  pinMode(BTN_DOWN_PIN, INPUT_PULLUP);
  pinMode(BTN_SET_PIN, INPUT_PULLUP);
  pinMode(BTN_PLAY_PIN, INPUT_PULLUP);
}

void Hardware::initVibrator() {
    pinMode(VIBRATOR_PIN, OUTPUT);
    digitalWrite(VIBRATOR_PIN, LOW);
}

void Hardware::updateDisplay() {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextSize(1); // [수정] 모든 화면의 기본 글자 크기를 1로 통일

  switch (getSystemMode()) {
    case SystemMode::NORMAL:
      if (getNormalView() == NormalView::GROUP) displayNormalGroup();
      else displayNormalIndividual();
      break;
    case SystemMode::SETTING:
      switch (getSettingStep()) {
        case SettingStep::GROUP_CONFIG:         displaySettingGroup(); break;
        case SettingStep::TIME_SELECT:          displaySettingTimeSelect(); break;
        case SettingStep::TIME_DETAIL_SELECT:   displaySettingTimeDetail(); break;
        case SettingStep::TIME_VALUE_ADJUST:    displaySettingTimeValue(); break;
      }
      break;
    case SystemMode::RUNNING:
      displayRunning();
      break;
    case SystemMode::SHOW_COMPLETE:
      displayComplete();
      break;
  }
  display.display();
}

void Hardware::displayNormalIndividual() {
    int id = getSelectedId();
    if (id < 1 || id > MAX_DEVICES) return;
    DeviceSettings& device = Config::getInstance().getDeviceSettings()[id - 1];

    // [수정] 타이틀 추가 및 레이아웃 조정
    display.println("--- NORMAL MODE ---");
    display.println("");

    char buf[32];
    sprintf(buf, "ID    : %02d", id);
    display.println(buf);
    
    sprintf(buf, "Delay : %02dm %02ds", device.delay_m, device.delay_s);
    display.println(buf);
    
    sprintf(buf, "Play  : %02ds", device.play_s);
    display.println(buf);

    sprintf(buf, "Group : %s", device.inGroup ? "YES" : "NO");
    display.println(buf);
}

void Hardware::displayNormalGroup() {
    DeviceSettings* devices = Config::getInstance().getDeviceSettings();
    display.println("--- GROUP MODE ---");

    std::vector<DeviceSettings> group_devices;
    for(int i=0; i<MAX_DEVICES; ++i) {
        if(devices[i].inGroup) group_devices.push_back(devices[i]);
    }
    std::sort(group_devices.begin(), group_devices.end(), [](const DeviceSettings& a, const DeviceSettings& b){
        return (a.delay_m * 60 + a.delay_s) < (b.delay_m * 60 + b.delay_s);
    });

    char buf[32];
    int line = 1;
    for(const auto& dev : group_devices) {
        if (line > 5) break;
        display.setCursor(0, line * 10);
        sprintf(buf, "ID%02d/D:%02d:%02d/P:%02d", dev.id, dev.delay_m, dev.delay_s, dev.play_s);
        display.println(buf);
        line++;
    }
}

void Hardware::displaySettingGroup() {
    int id = getSelectedId();
    if (id < 1 || id > MAX_DEVICES) return;
    DeviceSettings& device = Config::getInstance().getDeviceSettings()[id - 1];
    
    display.println("--- GROUP SETUP ---");
    display.println("");
    
    char buf[32];
    sprintf(buf, "ID : %02d", id);
    display.println(buf);
    
    display.println("");
    sprintf(buf, "-> Group : [ %s ]", device.inGroup ? "YES" : "NO");
    display.println(buf);
}

void Hardware::displaySettingTimeSelect() {
    int id = getSelectedId();
    int cursor = getCursorPos();
    if (id < 1 || id > MAX_DEVICES) return;
    DeviceSettings& device = Config::getInstance().getDeviceSettings()[id-1];

    char buf[32];
    sprintf(buf, "ID %d - Time Setup", id);
    display.println(buf);
    display.println("");

    sprintf(buf, "%s Delay: %02dm %02ds", (cursor == 0 ? "-> " : "   "), device.delay_m, device.delay_s);
    display.println(buf);
    sprintf(buf, "%s Play : %02ds", (cursor == 1 ? "-> " : "   "), device.play_s);
    display.println(buf);
}

void Hardware::displaySettingTimeDetail() {
    int id = getSelectedId();
    int cursor = getSubCursorPos();
    if (id < 1 || id > MAX_DEVICES) return;
    DeviceSettings& device = Config::getInstance().getDeviceSettings()[id-1];

    char buf[32];
    sprintf(buf, "ID %d - Set Delay", id);
    display.println(buf);
    sprintf(buf, "D: %02dm %02ds", device.delay_m, device.delay_s);
    display.println("");

    sprintf(buf, "%s Minute", (cursor == 0 ? "-> " : "   "));
    display.println(buf);
    sprintf(buf, "%s Second", (cursor == 1 ? "-> " : "   "));
    display.println(buf);
}

void Hardware::displaySettingTimeValue() {
    int id = getSelectedId();
    int cursor = getSubCursorPos();
    if (id < 1 || id > MAX_DEVICES) return;
    DeviceSettings& device = Config::getInstance().getDeviceSettings()[id-1];

    char buf[32], title[32];
    if (cursor == 2) sprintf(title, "ID %d - Set Play", id);
    else sprintf(title, "ID %d - Set Delay", id);
    display.println(title);
    
    display.println("");

    if (cursor == 0) sprintf(buf, "-> %02d m", device.delay_m);
    else if (cursor == 1) sprintf(buf, "-> %02d s", device.delay_s);
    else sprintf(buf, "-> %02d s", device.play_s);

    // [수정] 큰 글씨 대신 다른 표시로 강조
    display.setCursor(10, 25);
    display.setTextSize(2);
    display.println(buf);
}

void Hardware::displayRunning() {
    display.println("=== RUNNING ===");
    
    // [수정] 표시하기 직전에 항상 정렬된 복사본을 만들어 사용
    auto running_devices = getRunningDevices();
    std::sort(running_devices.begin(), running_devices.end(), [](const RunningDevice& a, const RunningDevice& b) {
        if (a.delayEndTime != b.delayEndTime) {
            return a.delayEndTime < b.delayEndTime;
        }
        return a.id < b.id;
    });

    unsigned long now = millis();
    char buf[32];
    int line = 1;

    for(const auto& dev : running_devices) {
        if (line > 5) break;
        display.setCursor(0, line * 10);
        
        if (dev.isDelayDone) {
            long p_rem = (dev.playEndTime - now) / 1000;
            sprintf(buf, "ID %02d / P: %02lds", dev.id, p_rem > 0 ? p_rem : 0);
        } else {
            long d_rem_sec_total = (dev.delayEndTime - now) / 1000;
            if (d_rem_sec_total < 0) d_rem_sec_total = 0;
            long d_min = d_rem_sec_total / 60;
            long d_sec = d_rem_sec_total % 60;
            sprintf(buf, "ID %02d / D: %02ldm%02lds", dev.id, d_min, d_sec);
        }
        display.println(buf);
        line++;
    }
}

void Hardware::displayComplete() {
    display.setTextSize(2);
    display.setCursor(15, 25);
    display.println("COMPLETE");
}

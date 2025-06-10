#ifndef CONFIG_T_H
#define CONFIG_T_H

#include <Arduino.h>
#include <EEPROM.h>

#define MAX_DEVICES 10
#define EEPROM_SIZE 512

// 각 장치의 설정값을 저장하는 구조체
struct DeviceSettings {
  int id;
  int delay_m;
  int delay_s;
  int play_s;
  bool inGroup;
};

// Config 클래스 선언
class Config {
public:
  static Config& getInstance();
  void init();
  void load();
  void save();

  DeviceSettings* getDeviceSettings() { return deviceSettings; }

private:
  Config() {} // Singleton
  Config(const Config&) = delete;
  void operator=(const Config&) = delete;

  DeviceSettings deviceSettings[MAX_DEVICES];
};

#endif // CONFIG_T_H

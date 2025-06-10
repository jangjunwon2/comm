#include "config_t.h"
#include "utils_t.h" // logPrintf를 사용하기 위함

Config& Config::getInstance() {
  static Config instance;
  return instance;
}

void Config::init() {
  if (!EEPROM.begin(EEPROM_SIZE)) {
    // Serial은 initUtils 전에 호출될 수 있으므로 직접 사용
    Serial.println("ERROR: Failed to initialise EEPROM");
    return;
  }
  load();
}

void Config::load() {
  // EEPROM의 첫 바이트를 확인하여 데이터 유효성 검사
  if (EEPROM.read(0) == 'V' && EEPROM.read(1) == '1') {
    for (int i = 0; i < MAX_DEVICES; ++i) {
      EEPROM.get(sizeof(char)*2 + i * sizeof(DeviceSettings), deviceSettings[i]);
    }
  } else {
    // 유효한 데이터가 없으면 기본값으로 초기화
    for (int i = 0; i < MAX_DEVICES; ++i) {
      deviceSettings[i] = {i + 1, 0, 10, 5, false}; // id, delay(0m10s), play(5s), not in group
    }
    save(); // 기본값을 EEPROM에 저장
  }
}

void Config::save() {
  logPrintf(LogLevel::LOG_INFO, "Saving settings to EEPROM...");
  EEPROM.write(0, 'V');
  EEPROM.write(1, '1');
  for (int i = 0; i < MAX_DEVICES; ++i) {
    EEPROM.put(sizeof(char)*2 + i * sizeof(DeviceSettings), deviceSettings[i]);
  }
  if(!EEPROM.commit()){
    logPrintf(LogLevel::LOG_ERROR, "EEPROM commit failed");
  }
}

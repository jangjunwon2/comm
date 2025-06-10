/**
 * @file comm.h
 * @brief ESP-NOW 통신을 위한 CommManager 클래스의 헤더 파일입니다.
 * @version 4.3.0
 * @date 2024-06-14
 */
#pragma once
#ifndef COMM_H
#define COMM_H

#include <WiFi.h>
#include <esp_now.h> // [FIX] ESP-NOW 타입을 위해 명시적으로 포함
#include "config.h"
#include "espnow_comm_shared.h"

// [FIX] 순환 참조를 피하기 위해 전방 선언 사용
class ModeManager;

// 전역 콜백 함수 선언
extern void onDataRecv(const esp_now_recv_info_t* info, const uint8_t* incomingData, int len);
extern void onDataSent(const uint8_t* mac_addr, esp_now_send_status_t status);


class CommManager {
public:
    CommManager();
    bool begin(uint8_t deviceId, ModeManager* modeMgr);
    void updateMyDeviceId(uint8_t newId);
    void reinitForEspNow();
    void handleEspNowRecv(const esp_now_recv_info_t* recv_info, const uint8_t* incomingData, int len);
    void handleEspNowSendStatus(const uint8_t* mac_addr, esp_now_send_status_t status);
    void sendAck(const uint8_t* targetMac, uint32_t original_packet_tx_timestamp, uint32_t rx_time);
    
private:
    ModeManager* _modeManager;
    uint8_t _myDeviceId;
    bool initEspNowStack();
    void registerCallbacks();
};

#endif // COMM_H

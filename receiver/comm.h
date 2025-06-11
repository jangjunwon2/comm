/**
 * @file comm.h
 * @brief ESP-NOW 통신을 위한 CommManager 클래스의 헤더 파일입니다.
 * @version 4.1.0 // [MODIFIED] 버전 업데이트
 * @date 2024-06-13
 */
#pragma once
#ifndef COMM_H
#define COMM_H

#include <esp_now.h>
#include <WiFi.h>
#include "config.h"
#include "espnow_comm_shared.h"

class ModeManager;

// ESP-NOW 콜백 함수를 전역으로 선언하여 esp_now_register_send_cb, esp_now_register_recv_cb에 등록할 수 있도록 합니다.
extern void onDataRecv(const esp_now_recv_info_t* info, const uint8_t* incomingData, int len);
extern void onDataSent(const uint8_t* mac_addr, esp_now_send_status_t status);


// CommManager는 수신부의 ESP-NOW 통신을 관리합니다.
// 송신부의 RunningDevice나 CommStatus는 이 클래스에서 직접 관리하지 않습니다.
class CommManager {
public:
    CommManager();
    // 초기화 함수: 장치 ID와 모드 매니저 포인터 필요
    bool begin(uint8_t deviceId, ModeManager* modeMgr);
    // 장치 ID 업데이트
    void updateMyDeviceId(uint8_t newId);
    // Wi-Fi 모드 종료 후 ESP-NOW 재초기화
    void reinitForEspNow();
    // ESP-NOW 데이터 수신 처리 (콜백에서 호출됨)
    // [MODIFIED] CommPacket을 const 참조 대신 const 포인터로 받음
    void handleEspNowRecv(const esp_now_recv_info_t* recv_info, const uint8_t* incomingData, int len);
    // ESP-NOW 데이터 송신 상태 처리 (콜백에서 호출됨)
    void handleEspNowSendStatus(const uint8_t* mac_addr, esp_now_send_status_t status);
    
    // [수정] sendAck 함수를 public으로 변경
    void sendAck(const uint8_t* targetMac, uint32_t original_packet_tx_timestamp, uint32_t rx_time);
    
private:
    ModeManager* _modeManager;
    uint8_t _myDeviceId;
    // ESP-NOW 스택 초기화
    bool initEspNowStack();
    // 콜백 함수 등록
    void registerCallbacks();
    
    // 송신부 관련 상수는 여기서는 사용하지 않습니다. (MAX_DEVICES, MAX_SEND_ATTEMPTS 등)
    // 수신부는 단순히 수신하고 응답하는 역할에 집중합니다.
};

#endif // COMM_H

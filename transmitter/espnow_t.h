#ifndef ESPNOW_T_H
#define ESPNOW_T_H

#include "config_t.h"
#include "espnow_comm_shared.h"

// 전역 변수 선언
extern bool executionComplete; // 실행 완료 상태

//────────────────────────────────────────────────────────────────────────────
// ESP-NOW 함수 선언 (송신부)
//────────────────────────────────────────────────────────────────────────────

// ESP-NOW 초기화
bool initEspNow();

// ESP-NOW 송신 콜백 함수
void espNowSendCb(const uint8_t* mac_addr, esp_now_send_status_t status);

// ESP-NOW 수신 콜백 함수 (ACK 패킷을 수신하기 위해 필요)
void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len);

// [핵심] 통신 상태 관리 함수 (송신부에서 재전송 및 타임아웃 관리)
bool manageCommunication();

// 실행 명령 전송 함수
bool sendExecutionCommand(uint8_t targetId, uint32_t txButtonPressSequenceMicros, uint32_t original_delay_ms, uint32_t play_ms, uint32_t lastRttUs, uint32_t lastRxProcessingTimeUs, uint32_t& out_tx_timestamp);

#endif // ESPNOW_T_H

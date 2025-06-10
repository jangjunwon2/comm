#include "espnow_t.h"
#include "utils_t.h" 
#include <algorithm> 

// ESP-NOW 송신 콜백 (단순 로그 출력)
void espNowSendCb(const uint8_t* mac_addr, esp_now_send_status_t status) {
    if (status != ESP_NOW_SEND_SUCCESS) {
        // [MODIFIED] 오류 시에만 ERROR 레벨로 출력
        logPrintf(LogLevel::LOG_ERROR, "ESP-NOW: 송신 실패, 상태=%d", status);
    } else {
        // [NEW] 성공 시에는 디버그 레벨로 출력 (너무 잦은 INFO 로그 방지)
        logPrintf(LogLevel::LOG_DEBUG, "ESP-NOW: 송신 성공.");
    }
}

// ESP-NOW 수신 콜백 (ACK 패킷 처리 - 송신부가 ACK를 받기 위해 필요)
void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
    // [MODIFIED] Comm::AckPacket 포인터 사용
    const Comm::AckPacket* ackPkt = nullptr; 

    // [MODIFIED] Comm::verifyAckPacket 함수를 사용하여 패킷 검증
    if (!Comm::verifyAckPacket(data, len, ackPkt)) {
        logPrintf(LogLevel::LOG_WARN, "COMM: 유효하지 않은 ACK 패킷 수신. 무시됨."); 
        return; // 유효하지 않은 ACK 패킷은 무시
    }

    uint8_t ackingDeviceID = ackPkt->senderId;  // [FIXED] deviceID를 senderId로 수정
    bool found = false;

    for (int i = 0; i < groupDeviceCount; i++) {
        if (runningDevices[i].deviceID == ackingDeviceID) {
            found = true;
            if (runningDevices[i].commStatus == COMM_AWAITING_ACK && 
                ackPkt->originalTxMicros == runningDevices[i].lastTxTimestamp) {
                runningDevices[i].commStatus = COMM_ACK_RECEIVED_SUCCESS;
                runningDevices[i].successfulAcks++; // 성공적인 ACK 카운트 증가

                unsigned long rtt = micros() - ackPkt->originalTxMicros; // RTT 계산 (송신 시점 - ACK 수신 시점)
                runningDevices[i].lastRttUs = rtt; // 해당 RunningDevice에 마지막 RTT 저장
                runningDevices[i].lastRxProcessingTimeUs = ackPkt->rxProcessingTimeUs; // 해당 RunningDevice에 수신기의 처리 시간 저장

                // [NEW] 전역 변수에 최신 RTT 및 RxProcessingTime 저장 (다음 실행에 사용될 값)
                g_lastKnownGlobalRttUs = rtt;
                g_lastKnownGlobalRxProcessingTimeUs = ackPkt->rxProcessingTimeUs;

                // [MODIFIED] 보정값 관련 로깅 개선
                logPrintf(LogLevel::LOG_INFO, "COMM: ID %d로부터 ACK 성공", ackingDeviceID);
                logPrintf(LogLevel::LOG_INFO, "COMM: ID %d - RTT: %lu us (예상 통신 지연: %lu ms)", 
                         ackingDeviceID, rtt, rtt / 2000);
                logPrintf(LogLevel::LOG_INFO, "COMM: ID %d - Rx 처리 시간: %lu us (예상 수신기 처리: %lu ms)", 
                         ackingDeviceID, ackPkt->rxProcessingTimeUs, ackPkt->rxProcessingTimeUs / 1000);
                logPrintf(LogLevel::LOG_INFO, "COMM: ID %d - 총 예상 지연: %lu ms (전역 값 업데이트됨)", 
                         ackingDeviceID, (rtt / 2000) + (ackPkt->rxProcessingTimeUs / 1000));
            } else {
                logPrintf(LogLevel::LOG_WARN, "COMM: ID %d로부터 상태/타임스탬프 불일치 ACK 수신. 무시됨. (현재 상태: %d, 수신 패킷 TX 타임스탬프: %u, 예상 TX 타임스탬프: %u)", 
                         ackingDeviceID, runningDevices[i].commStatus, ackPkt->originalTxMicros, runningDevices[i].lastTxTimestamp);
            }
            break;
        }
    }

    if (!found) {
        logPrintf(LogLevel::LOG_WARN, "COMM: 알 수 없는 장치(ID: %d)로부터 ACK 수신. 무시됨.", ackingDeviceID);
    }
}

// ESP-NOW 초기화
bool initEspNow() {
    WiFi.mode(WIFI_STA); // Wi-Fi 스테이션 모드 설정
    delay(50); // 잠시 대기

    // Wi-Fi 채널 설정 시도
    if (esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE) != ESP_OK) {
        logPrintf(LogLevel::LOG_ERROR, "ESP-NOW: Wi-Fi 채널 %d 설정 실패", WIFI_CHANNEL);
        return false;
    }
    // ESP-NOW 초기화 시도
    if (esp_now_init() != ESP_OK) {
        logPrintf(LogLevel::LOG_ERROR, "ESP-NOW: esp_now_init 실패");
        return false;
    }

    // ESP-NOW 송신 및 수신 콜백 등록
    esp_now_register_send_cb(espNowSendCb);
    esp_now_register_recv_cb(OnDataRecv);

    // 브로드캐스트 피어 정보 설정
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, broadcastAddress, 6);
    peerInfo.channel = WIFI_CHANNEL;
    peerInfo.encrypt = false; // 암호화 사용 안 함

    // 브로드캐스트 피어 추가 시도
    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
        logPrintf(LogLevel::LOG_ERROR, "ESP-NOW: 브로드캐스트 피어 추가 실패.");
        return false;
    }

    logPrintf(LogLevel::LOG_INFO, "ESP-NOW: 초기화 완료 (채널=%d)", WIFI_CHANNEL);
    espNowInitialized = true; // 초기화 상태 플래그 설정
    return true;
}

// [MODIFIED] 실행 명령 전송 함수에 txButtonPressSequenceMicros_arg 파라미터 추가
// txButtonPressSequenceMicros_arg: 해당 시퀀스가 시작된 버튼 누름 시점의 micros() 값 (RunningDevice에서 가져옴)
bool sendExecutionCommand(uint8_t targetId, uint32_t txButtonPressSequenceMicros_arg, uint32_t original_delay_ms, uint32_t play_ms, uint32_t lastRttUs, uint32_t lastRxProcessingTimeUs, uint32_t& out_tx_timestamp) {
    Comm::CommPacket packet;
    
    // txButtonPressSequenceMicros_arg 인자를 fillPacket에 전달
    Comm::fillPacket(packet, targetId, txButtonPressSequenceMicros_arg, original_delay_ms, play_ms, lastRttUs, lastRxProcessingTimeUs);
    
    out_tx_timestamp = packet.txMicros; // 실제 패킷이 전송된 시각 기록

    // [MODIFIED] 보정값 관련 로깅 개선
    logPrintf(LogLevel::LOG_INFO, "COMM: ID %d - CMD 전송 (버튼: %u us, 패킷: %u us, 지연: %u ms, 플레이: %u ms)", 
                    targetId, txButtonPressSequenceMicros_arg, out_tx_timestamp, original_delay_ms, play_ms);
    logPrintf(LogLevel::LOG_INFO, "COMM: ID %d - 보정값 관련 내용: 이전 RTT: %u us, 이전 Rx 처리: %u us", 
                    targetId, lastRttUs, lastRxProcessingTimeUs);
    logPrintf(LogLevel::LOG_INFO, "COMM: ID %d - 예상 통신 지연: %u ms, 예상 수신기 처리: %u ms", 
                    targetId, lastRttUs / 2000, lastRxProcessingTimeUs / 1000);

    esp_err_t result = esp_now_send(broadcastAddress, (uint8_t*)&packet, sizeof(packet));
    return (result == ESP_NOW_SEND_SUCCESS);
}

// [MODIFIED] 그룹 통신 안정성을 위해 한 번의 호출에 하나의 패킷만 전송하도록 수정
// 이 함수는 loop()에서 반복적으로 호출되어야 합니다.
bool manageCommunication() {
    unsigned long currentTime = millis();
    bool all_comm_done = true;

    for (int i = 0; i < groupDeviceCount; i++) {
        RunningDevice& device = runningDevices[i];

        // 이미 ACK를 성공적으로 받았거나 모든 재시도 실패 시 다음 장치로 넘어감
        if (device.commStatus == COMM_ACK_RECEIVED_SUCCESS || device.commStatus == COMM_FAILED_NO_ACK) {
            continue;
        }

        all_comm_done = false; // 아직 통신이 완료되지 않은 장치가 있음

        switch (device.commStatus) {
            case COMM_PENDING_INITIAL_SEND:
            case COMM_ACK_FAILED_RETRYING: {
                // 초기 전송이거나 재시도가 필요한 경우
                // 재시도 간격이 지났거나, 첫 전송 시도인 경우 (device.sendAttempts == 0)
                if (currentTime - device.lastPacketSendTime >= RETRY_INTERVAL_MS || device.sendAttempts == 0) {
                    logPrintf(LogLevel::LOG_INFO, "COMM: 장치 %d로 CMD 전송 시도 #%d (대상 지연: %u ms, 플레이: %u ms)", 
                                device.deviceID, device.sendAttempts + 1, device.delayTime, device.playTime);
                    
                    uint32_t tx_time;
                    if (sendExecutionCommand(device.deviceID, device.txButtonPressSequenceMicros, 
                                          device.delayTime, device.playTime,
                                          device.lastRttUs, device.lastRxProcessingTimeUs, tx_time)) {
                        device.sendAttempts++;
                        device.lastPacketSendTime = currentTime;
                        device.ackTimeoutDeadline = currentTime + ACK_TIMEOUT_MS;
                        device.lastTxTimestamp = tx_time; // 실제 패킷 전송 시각 저장
                        device.commStatus = COMM_AWAITING_ACK; // ACK 대기 상태로 변경
                    }
                    // [IMPORTANT] 한 번의 manageCommunication 호출에서는 하나의 장치에 대해서만 통신 시도를 합니다.
                    // 이렇게 해야 여러 장치에 대한 브로드캐스트 전송이 한 번에 몰리지 않고 분산되어 안정성이 높아집니다.
                    return false;  
                }
                break;
            }

            case COMM_AWAITING_ACK: { // ACK 대기 중
                // ACK 타임아웃이 발생한 경우
                if (currentTime > device.ackTimeoutDeadline) {
                    logPrintf(LogLevel::LOG_WARN, "COMM: 장치 %d에 대한 ACK 타임아웃 (시도 #%d)", device.deviceID, device.sendAttempts);
                    if (device.sendAttempts >= MAX_SEND_ATTEMPTS) {
                        device.commStatus = COMM_FAILED_NO_ACK; // 최대 재시도 횟수 초과 시 실패 상태로 변경
                        logPrintf(LogLevel::LOG_ERROR, "COMM: 장치 %d에 대한 모든 시도 실패. 실패로 표시.", device.deviceID);
                    } else {
                        device.commStatus = COMM_ACK_FAILED_RETRYING; // 재시도 상태로 변경
                    }
                }
                break;
            }
        }
    }
    return all_comm_done; // 모든 통신이 완료되었는지 반환
}

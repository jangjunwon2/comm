#include "espnow_t.h"
#include "utils_t.h" 
#include <algorithm> 

// ESP-NOW 송신 콜백 (단순 로그 출력)
void espNowSendCb(const uint8_t* mac_addr, esp_now_send_status_t status) {
    if (status != ESP_NOW_SEND_SUCCESS) {
        logPrintf(LogLevel::LOG_ERROR, "ESP-NOW: 송신 실패, 상태=%d", status);
    } else {
        logPrintf(LogLevel::LOG_DEBUG, "ESP-NOW: 송신 성공.");
    }
}

// ESP-NOW 수신 콜백 (ACK 패킷 처리 - 송신부가 ACK를 받기 위해 필요)
void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
    const Comm::AckPacket* ackPkt = nullptr; 

    if (!Comm::verifyAckPacket(data, len, ackPkt)) {
        logPrintf(LogLevel::LOG_WARN, "COMM: 유효하지 않은 ACK 패킷 수신. 무시됨."); 
        return; // 유효하지 않은 ACK 패킷은 무시
    }

    uint8_t ackingDeviceID = ackPkt->senderId;
    unsigned long rtt = micros() - ackPkt->originalTxMicros; // RTT 계산

    for (int i = 0; i < groupDeviceCount; ++i) {
        RunningDevice& device = runningDevices[i];
        if (device.deviceID == ackingDeviceID) {
            // [MODIFIED] ACK 수신 시 상태별 처리
            if (device.commStatus == COMM_AWAITING_RTT_ACK) {
                // RTT 요청에 대한 ACK를 받은 경우
                if (device.lastTxTimestamp == ackPkt->originalTxMicros) {
                    device.currentSequenceRttUs = rtt; // 현재 시퀀스의 RTT 저장
                    device.currentSequenceRxProcessingTimeUs = ackPkt->rxProcessingTimeUs; // 현재 시퀀스의 Rx 처리 시간 저장
                    device.successfulAcks++;
                    device.commStatus = COMM_PENDING_FINAL_COMMAND; // 최종 명령 전송 대기 상태로 변경
                    logPrintf(LogLevel::LOG_INFO, "COMM: ID %d로부터 RTT ACK 성공. RTT: %lu us, RxProc: %lu us.", 
                                ackingDeviceID, rtt, ackPkt->rxProcessingTimeUs);
                } else {
                    logPrintf(LogLevel::LOG_WARN, "COMM: ID %d로부터 RTT ACK 수신 (타임스탬프 불일치). 무시됨. (현재 TX: %u, 수신 ACK TX: %u)", 
                                ackingDeviceID, device.lastTxTimestamp, ackPkt->originalTxMicros);
                }
            } else if (device.commStatus == COMM_AWAITING_FINAL_ACK) {
                // 최종 명령에 대한 ACK를 받은 경우
                if (device.lastTxTimestamp == ackPkt->originalTxMicros) {
                    device.successfulAcks++;
                    device.commStatus = COMM_ACK_RECEIVED_SUCCESS; // 최종 통신 성공 상태로 변경
                    logPrintf(LogLevel::LOG_INFO, "COMM: ID %d로부터 최종 CMD ACK 성공. RTT: %lu us, RxProc: %lu us.", 
                                ackingDeviceID, rtt, ackPkt->rxProcessingTimeUs);
                } else {
                    logPrintf(LogLevel::LOG_WARN, "COMM: ID %d로부터 최종 CMD ACK 수신 (타임스탬프 불일치). 무시됨. (현재 TX: %u, 수신 ACK TX: %u)", 
                                ackingDeviceID, device.lastTxTimestamp, ackPkt->originalTxMicros);
                }
            } else {
                logPrintf(LogLevel::LOG_WARN, "COMM: ID %d로부터 ACK 수신 (예상치 못한 상태: %d). 무시됨.", 
                            ackingDeviceID, device.commStatus);
            }
            return; // 해당 장치에 대한 ACK를 찾았으므로 함수 종료
        }
    }
}

// ESP-NOW 초기화
bool initEspNow() {
    WiFi.mode(WIFI_STA); // Wi-Fi 스테이션 모드 설정
    delay(50); // 잠시 대기

    if (esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE) != ESP_OK) {
        logPrintf(LogLevel::LOG_ERROR, "ESP-NOW: Wi-Fi 채널 %d 설정 실패", WIFI_CHANNEL);
        return false;
    }
    if (esp_now_init() != ESP_OK) {
        logPrintf(LogLevel::LOG_ERROR, "ESP-NOW: esp_now_init 실패");
        return false;
    }

    esp_now_register_send_cb(espNowSendCb);
    esp_now_register_recv_cb(OnDataRecv);

    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, broadcastAddress, 6);
    peerInfo.channel = WIFI_CHANNEL;
    peerInfo.encrypt = false;
    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
        logPrintf(LogLevel::LOG_ERROR, "ESP-NOW: 브로드캐스트 피어 추가 실패.");
        return false;
    }

    logPrintf(LogLevel::LOG_INFO, "ESP-NOW: 초기화 완료 (채널=%d)", WIFI_CHANNEL);
    espNowInitialized = true;
    return true;
}

// [MODIFIED] 실행 명령 전송 함수에 packetType 파라미터 추가
bool sendExecutionCommand(Comm::PacketType type, uint8_t targetId, uint32_t txButtonPressSequenceMicros_arg, uint32_t original_delay_ms, uint32_t play_ms, uint32_t rttUs, uint32_t rxProcessingTimeUs, uint32_t& out_tx_timestamp) {
    Comm::CommPacket packet;
    
    // [MODIFIED] Comm::fillPacket 함수에 packetType 추가
    Comm::fillPacket(packet, type, targetId, txButtonPressSequenceMicros_arg, original_delay_ms, play_ms, rttUs, rxProcessingTimeUs);
    
    out_tx_timestamp = packet.txMicros; // 실제 패킷이 전송된 시각 기록

    const char* packetTypeStr = (type == Comm::RTT_REQUEST) ? "RTT_REQUEST" : "FINAL_COMMAND";

    logPrintf(LogLevel::LOG_DEBUG, "COMM: ID %d - %s 전송 시도 (버튼: %u us, 패킷: %u us, 지연: %u ms, 플레이: %u ms)", 
                        targetId, packetTypeStr, txButtonPressSequenceMicros_arg, out_tx_timestamp, original_delay_ms, play_ms);
    logPrintf(LogLevel::LOG_DEBUG, "COMM: ID %d - 포함된 RTT: %u us, 포함된 Rx 처리: %u us", 
                        targetId, rttUs, rxProcessingTimeUs);

    esp_err_t result = esp_now_send(broadcastAddress, (uint8_t*)&packet, sizeof(packet));

    if (result == ESP_OK) {
        return true;
    } else {
        logPrintf(LogLevel::LOG_ERROR, "ESP-NOW: ID %d로 %s 전송 실패 (에러=%d)", targetId, packetTypeStr, result);
        return false;
    }
}

// [MODIFIED] 그룹 통신 안정성을 위해 한 번의 호출에 하나의 패킷만 전송하도록 수정
// 이 함수는 loop()에서 반복적으로 호출되어야 합니다.
bool manageCommunication() {
    unsigned long currentTime = millis();
    bool all_comm_done = true; // 모든 장치 통신이 완료되었는지 여부

    // [NEW] 한 번의 manageCommunication 호출에서는 하나의 장치에 대해서만 통신 시도를 합니다.
    // 이렇게 해야 여러 장치에 대한 브로드캐스트 전송이 한 번에 몰리지 않고 분산되어 안정성이 높아집니다.
    RunningDevice* currentDeviceToProcess = nullptr;
    for (int i = 0; i < groupDeviceCount; ++i) {
        if (runningDevices[i].commStatus != COMM_ACK_RECEIVED_SUCCESS &&
            runningDevices[i].commStatus != COMM_FAILED_NO_ACK) {
            currentDeviceToProcess = &runningDevices[i];
            all_comm_done = false; // 아직 통신이 완료되지 않은 장치가 있음
            break; 
        }
    }

    if (!currentDeviceToProcess) {
        return all_comm_done; // 모든 장치 통신이 완료됨
    }

    RunningDevice& device = *currentDeviceToProcess;

    switch (device.commStatus) {
        case COMM_PENDING_RTT_REQUEST:
        case COMM_AWAITING_RTT_ACK: { // [FIXED] Typo 'COMM_AWAITing_RTT_ACK' corrected to 'COMM_AWAITING_RTT_ACK'
            if (device.commStatus == COMM_PENDING_RTT_REQUEST || currentTime - device.lastPacketSendTime >= RETRY_INTERVAL_MS) {
                // RTT 요청 패킷 전송 (이전 RTT, RxProc는 0으로 보냄)
                logPrintf(LogLevel::LOG_INFO, "COMM: 장치 %d로 RTT_REQUEST 전송 시도 #%d", 
                            device.deviceID, device.sendAttempts + 1);
                
                uint32_t tx_time;
                if (sendExecutionCommand(Comm::RTT_REQUEST, device.deviceID, device.txButtonPressSequenceMicros, 
                                        device.delayTime, device.playTime, 0, 0, tx_time)) { // RTT/RxProc는 0으로 초기 전송
                    device.sendAttempts++;
                    device.lastPacketSendTime = currentTime;
                    device.ackTimeoutDeadline = currentTime + ACK_TIMEOUT_MS;
                    device.lastTxTimestamp = tx_time;
                    device.commStatus = COMM_AWAITING_RTT_ACK;
                } else {
                    logPrintf(LogLevel::LOG_ERROR, "COMM: 장치 %d RTT_REQUEST 전송 실패. 재시도 필요.", device.deviceID);
                }
            } else if (currentTime > device.ackTimeoutDeadline) {
                // RTT 요청 ACK 타임아웃
                logPrintf(LogLevel::LOG_WARN, "COMM: 장치 %d에 대한 RTT_ACK 타임아웃 (시도 #%d)", device.deviceID, device.sendAttempts);
                if (device.sendAttempts >= MAX_SEND_ATTEMPTS) {
                    device.commStatus = COMM_FAILED_NO_ACK;
                    logPrintf(LogLevel::LOG_ERROR, "COMM: 장치 %d에 대한 RTT_ACK 모든 시도 실패. 실패로 표시.", device.deviceID);
                } else {
                    // 재시도를 위해 상태 유지 (manageCommunication이 다시 호출될 때 재시도 로직으로 들어감)
                    device.commStatus = COMM_PENDING_RTT_REQUEST; // 다시 전송 대기 상태로
                }
            }
            return false; // 하나의 장치에 대해서만 처리 후 즉시 반환
        }

        case COMM_PENDING_FINAL_COMMAND:
        case COMM_AWAITING_FINAL_ACK: { // 최종 명령 패킷 전송 및 ACK 대기
            if (device.commStatus == COMM_PENDING_FINAL_COMMAND || currentTime - device.lastPacketSendTime >= RETRY_INTERVAL_MS) {
                // 최종 명령 패킷 전송 (RTT 및 RxProc 값 포함)
                logPrintf(LogLevel::LOG_INFO, "COMM: 장치 %d로 FINAL_COMMAND 전송 시도 #%d (포함 RTT: %u us, RxProc: %u us)", 
                            device.deviceID, device.sendAttempts + 1, device.currentSequenceRttUs, device.currentSequenceRxProcessingTimeUs);
                
                uint32_t tx_time;
                if (sendExecutionCommand(Comm::FINAL_COMMAND, device.deviceID, device.txButtonPressSequenceMicros, 
                                        device.delayTime, device.playTime, 
                                        device.currentSequenceRttUs, device.currentSequenceRxProcessingTimeUs, tx_time)) {
                    device.sendAttempts++; // sendAttempts는 전체 시퀀스에 대해 누적
                    device.lastPacketSendTime = currentTime;
                    device.ackTimeoutDeadline = currentTime + ACK_TIMEOUT_MS;
                    device.lastTxTimestamp = tx_time;
                    device.commStatus = COMM_AWAITING_FINAL_ACK;
                } else {
                    logPrintf(LogLevel::LOG_ERROR, "COMM: 장치 %d FINAL_COMMAND 전송 실패. 재시도 필요.", device.deviceID);
                }
            } else if (currentTime > device.ackTimeoutDeadline) {
                // 최종 명령 ACK 타임아웃
                logPrintf(LogLevel::LOG_WARN, "COMM: 장치 %d에 대한 FINAL_ACK 타임아웃 (시도 #%d)", device.deviceID, device.sendAttempts);
                if (device.sendAttempts >= MAX_SEND_ATTEMPTS) {
                    device.commStatus = COMM_FAILED_NO_ACK;
                    logPrintf(LogLevel::LOG_ERROR, "COMM: 장치 %d에 대한 FINAL_ACK 모든 시도 실패. 실패로 표시.", device.deviceID);
                } else {
                    // 재시도를 위해 상태 유지 (manageCommunication이 다시 호출될 때 재시도 로직으로 들어감)
                    device.commStatus = COMM_PENDING_FINAL_COMMAND; // 다시 전송 대기 상태로
                }
            }
            return false; // 하나의 장치에 대해서만 처리 후 즉시 반환
        }

        default:
            // COMM_ACK_RECEIVED_SUCCESS 또는 COMM_FAILED_NO_ACK는 위에서 이미 처리되었음
            break; 
    }
    return all_comm_done; // 모든 통신이 완료되었는지 반환 (이 코드는 사실상 도달하지 않음)
}

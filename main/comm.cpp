// =========================================================================
// comm.cpp
// =========================================================================

/**
 * @file comm.cpp
 * @brief CommManager 클래스의 구현입니다.
 * @version 7.6.0 // [MODIFIED] 버전 업데이트
 * @date 2024-06-13
 */
#include "comm.h"
#include "mode.h"
#include "utils.h"
#include <esp_wifi.h>
// #include <algorithm> // std::max는 이 파일에서 직접 사용되지 않으므로 제거 가능

extern CommManager commManager;

// 전역 ESP-NOW 수신 콜백 함수. CommManager의 멤버 함수로 전달합니다.
void onDataRecv(const esp_now_recv_info_t* info, const uint8_t* incomingData, int len) {
    commManager.handleEspNowRecv(info, incomingData, len);
}

// 전역 ESP-NOW 송신 콜백 함수. CommManager의 멤버 함수로 전달합니다.
void onDataSent(const uint8_t* mac_addr, esp_now_send_status_t status) {
    commManager.handleEspNowSendStatus(mac_addr, status);
}

CommManager::CommManager() : _modeManager(nullptr), _myDeviceId(DEFAULT_DEVICE_ID) {
    // 수신부에서는 runningDevices 배열이 필요 없습니다. (송신부에서 관리)
    // 따라서 memset 호출을 제거합니다.
}

bool CommManager::begin(uint8_t deviceId, ModeManager* modeMgr) {
    _myDeviceId = deviceId;
    _modeManager = modeMgr;
    Log::Info(PSTR("COMM: Device ID %d로 ESP-NOW 초기화 중 (수신부)"), _myDeviceId);
    if (!initEspNowStack()) return false;
    registerCallbacks();
    Log::Info(PSTR("COMM: ESP-NOW 초기화 성공 (채널: %d)."), ESP_NOW_CHANNEL);
    return true;
}

bool CommManager::initEspNowStack() {
    WiFi.mode(WIFI_STA); // Wi-Fi 스테이션 모드로 설정
    if (esp_wifi_set_channel(ESP_NOW_CHANNEL, WIFI_SECOND_CHAN_NONE) != ESP_OK) {
        Log::Error(PSTR("COMM: Wi-Fi 채널 %d 설정 실패."), ESP_NOW_CHANNEL);
        return false;
    }
    if (esp_now_init() != ESP_OK) {
        Log::Error(PSTR("COMM: ESP-NOW 초기화 실패!"));
        // 오류 발생 시 모드 변경 (선택 사항, _modeManager가 유효한지 확인)
        if (_modeManager) _modeManager->switchToMode(DeviceMode::MODE_ERROR, true);
        return false;
    }
    // 송신부의 브로드캐스트 주소를 피어로 추가합니다.
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, BROADCAST_ADDRESS, 6);
    peerInfo.channel = ESP_NOW_CHANNEL;
    peerInfo.encrypt = false; // 암호화 사용 안 함
    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
        Log::Error(PSTR("COMM: 브로드캐스트 피어 추가 실패."));
        return false;
    }
    return true;
}

void CommManager::reinitForEspNow() {
    Log::Info(PSTR("COMM: Wi-Fi 모드 종료 후 ESP-NOW 재초기화 중 (수신부)."));
    esp_now_deinit(); // 기존 ESP-NOW 인스턴스 해제
    WiFi.disconnect(true, true); // Wi-Fi 연결 해제
    delay(100); 
    if (!initEspNowStack()) { // ESP-NOW 스택 재초기화
        Log::Error(PSTR("COMM: ESP-NOW 재초기화 실패 (수신부)."));
        return;
    }
    registerCallbacks(); // 콜백 다시 등록
    Log::Info(PSTR("COMM: ESP-NOW 재초기화 성공 (수신부)."));
}

void CommManager::registerCallbacks() {
    esp_now_register_recv_cb(onDataRecv); // 수신 콜백 등록
    esp_now_register_send_cb(onDataSent); // 송신 콜백 등록 (ACK 전송 상태 확인용)
}

void CommManager::updateMyDeviceId(uint8_t newId) {
    if (_myDeviceId != newId) {
        Log::Info(PSTR("COMM: 장치 ID가 %d에서 %d로 업데이트됨 (수신부)."), _myDeviceId, newId);
        _myDeviceId = newId;
    }
}

// [MODIFIED] CommPacket을 const 포인터로 받음
void CommManager::handleEspNowRecv(const esp_now_recv_info_t* recv_info, const uint8_t* incomingData, int len) {
    const Comm::CommPacket* pkt = nullptr;
    bool forMe = false;

    uint32_t rxTime = micros(); 

    if (!Comm::verifyCommPacket(incomingData, len, pkt, _myDeviceId, forMe)) {
        Log::Warn(PSTR("COMM: 유효하지 않거나 손상된 ESP-NOW 패킷 수신."));
        return;
    }
    if (!forMe) {
        Log::Debug(PSTR("COMM: 나를 위한 패킷이 아님. 대상 ID: %u, 내 ID: %u."), pkt->targetId, _myDeviceId);
        return;
    }

    if (_modeManager) {
        _modeManager->handleEspNowCommand(recv_info->src_addr, pkt); // [MODIFIED] 포인터 전달
    }
}

void CommManager::handleEspNowSendStatus(const uint8_t* mac_addr, esp_now_send_status_t status) {
    Log::Debug(PSTR("COMM: MAC %02X:%02X:%02X:%02X:%02X:%02X로 ACK 전송 상태: %s"),
        mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5],
        status == ESP_NOW_SEND_SUCCESS ? "성공" : "실패");
}

void CommManager::sendAck(const uint8_t* targetMac, uint32_t original_packet_tx_timestamp, uint32_t rx_time) {
    Comm::AckPacket ackPacket;
    
    uint32_t rxProcessingTime = micros() - rx_time;
    
    Comm::fillAckPacket(ackPacket, _myDeviceId, original_packet_tx_timestamp, rxProcessingTime);
    
    if (!esp_now_is_peer_exist(targetMac)) {
        esp_now_peer_info_t peer = {};
        memcpy(peer.peer_addr, targetMac, 6);
        peer.channel = ESP_NOW_CHANNEL;
        peer.encrypt = false;
        esp_err_t addStatus = esp_now_add_peer(&peer);
        if (addStatus != ESP_OK && addStatus != ESP_ERR_ESPNOW_EXIST) {
            Log::Warn(PSTR("COMM: ACK 피어 추가 실패."));
            return;
        }
    }
    esp_now_send(targetMac, (uint8_t*)&ackPacket, sizeof(ackPacket));
}

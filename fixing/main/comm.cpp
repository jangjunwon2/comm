// =========================================================================
// comm.cpp
// =========================================================================

/**
 * @file comm.cpp
 * @brief CommManager 클래스의 구현입니다.
 * @version 7.8.0
 * @date 2024-06-14
 */
#include "comm.h"
#include "mode.h" // [FIX] ModeManager의 전체 정의를 위해 포함
#include "utils.h"
#include <esp_wifi.h>

// main.ino에 정의될 전역 객체
extern CommManager commManager;

// 전역 콜백 함수 -> CommManager의 멤버 함수로 라우팅
void onDataRecv(const esp_now_recv_info_t* info, const uint8_t* incomingData, int len) {
    commManager.handleEspNowRecv(info, incomingData, len);
}

void onDataSent(const uint8_t* mac_addr, esp_now_send_status_t status) {
    commManager.handleEspNowSendStatus(mac_addr, status);
}

CommManager::CommManager() : _modeManager(nullptr), _myDeviceId(DEFAULT_DEVICE_ID) {}

bool CommManager::begin(uint8_t deviceId, ModeManager* modeMgr) {
    _myDeviceId = deviceId;
    _modeManager = modeMgr;
    Log::Info(PSTR("COMM: Initializing ESP-NOW for Receiver (ID: %d)"), _myDeviceId);
    return initEspNowStack();
}

bool CommManager::initEspNowStack() {
    WiFi.mode(WIFI_STA);
    if (esp_wifi_set_channel(ESP_NOW_CHANNEL, WIFI_SECOND_CHAN_NONE) != ESP_OK) {
        Log::Error(PSTR("COMM: Failed to set Wi-Fi channel %d."), ESP_NOW_CHANNEL);
        return false;
    }
    if (esp_now_init() != ESP_OK) {
        Log::Error(PSTR("COMM: ESP-NOW Init Failed!"));
        if (_modeManager) _modeManager->switchToMode(DeviceMode::MODE_ERROR, true);
        return false;
    }

    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, BROADCAST_ADDRESS, 6);
    peerInfo.channel = ESP_NOW_CHANNEL;
    peerInfo.encrypt = false;
    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
        Log::Error(PSTR("COMM: Failed to add broadcast peer."));
        return false;
    }
    registerCallbacks();
    Log::Info(PSTR("COMM: ESP-NOW initialized successfully on channel %d."), ESP_NOW_CHANNEL);
    return true;
}


void CommManager::reinitForEspNow() {
    Log::Info(PSTR("COMM: Re-initializing ESP-NOW for Receiver..."));
    esp_now_deinit();
    delay(100); 
    if (!initEspNowStack()) { 
        Log::Error(PSTR("COMM: Failed to re-initialize ESP-NOW."));
        return;
    }
    Log::Info(PSTR("COMM: ESP-NOW re-initialized successfully."));
}

void CommManager::registerCallbacks() {
    esp_now_register_recv_cb(onDataRecv);
    esp_now_register_send_cb(onDataSent);
}

void CommManager::updateMyDeviceId(uint8_t newId) {
    if (_myDeviceId != newId) {
        Log::Info(PSTR("COMM: Receiver Device ID updated from %d to %d."), _myDeviceId, newId);
        _myDeviceId = newId;
    }
}

void CommManager::handleEspNowRecv(const esp_now_recv_info_t* recv_info, const uint8_t* incomingData, int len) {
    const Comm::CommPacket* pkt = nullptr;
    bool forMe = false;

    if (!Comm::verifyCommPacket(incomingData, len, pkt, _myDeviceId, forMe)) {
        Log::Warn(PSTR("COMM: Received invalid or corrupt ESP-NOW packet."));
        return;
    }
    if (!forMe) {
        Log::Debug(PSTR("COMM: Ignored packet for other device (Target: %u, Mine: %u)."), pkt->targetId, _myDeviceId);
        return;
    }

    if (_modeManager) {
        _modeManager->handleEspNowCommand(recv_info->src_addr, pkt);
    }
}

void CommManager::handleEspNowSendStatus(const uint8_t* mac_addr, esp_now_send_status_t status) {
    Log::Debug(PSTR("COMM: ACK sent to %02X:%02X:%02X:%02X:%02X:%02X, Status: %s"),
        mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5],
        status == ESP_NOW_SEND_SUCCESS ? "Success" : "Fail");
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
            Log::Warn(PSTR("COMM: Failed to add peer for ACK."));
            return;
        }
    }
    esp_now_send(targetMac, (uint8_t*)&ackPacket, sizeof(ackPacket));
}

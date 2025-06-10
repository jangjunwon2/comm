/* ==========================================================================
 * File: espnow_t.cpp
 * 역할: EspNow 클래스의 모든 통신 로직을 구현합니다.
 * ========================================================================== */
#include "espnow_t.h"
#include "espnow_comm_shared.h"
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include "utils_t.h" // logPrintf를 사용하기 위함

EspNow* EspNow::instance = nullptr;

// 브로드캐스트 주소 (모든 장치에 전송)
uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// ESP-NOW 콜백 함수
// 참고: 수신 콜백의 파라미터가 최신 ESP-IDF 버전에 맞게 수정되었습니다.
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
    // logPrintf(LogLevel::LOG_DEBUG, "ESP-NOW Send Status: %s", status == ESP_NOW_SEND_SUCCESS ? "Success" : "Fail");
}

void OnDataRecv(const esp_now_recv_info * info, const uint8_t *incomingData, int len) {
    // 송신기는 수신할 일이 거의 없으므로 비워둡니다.
}

EspNow& EspNow::getInstance() {
    static EspNow espnow_instance;
    if (instance == nullptr) {
        instance = &espnow_instance;
    }
    return espnow_instance;
}

// Public Methods
void EspNow::init() {
    WiFi.mode(WIFI_STA);
    delay(50);
    if (esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE) != ESP_OK) {
        logPrintf(LogLevel::LOG_ERROR, "ESP-NOW: Channel setup failed");
        return;
    }
    if (esp_now_init() != ESP_OK) {
        logPrintf(LogLevel::LOG_ERROR, "ESP-NOW: Init failed");
        return;
    }
    esp_now_register_send_cb(onDataSent);
    esp_now_register_recv_cb(onDataRecv);
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, broadcastAddress, 6);
    peerInfo.channel = WIFI_CHANNEL;
    peerInfo.encrypt = false;
    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
        logPrintf(LogLevel::LOG_ERROR, "ESP-NOW: Peer add failed");
        return;
    }
    logPrintf(LogLevel::LOG_INFO, "ESP-NOW Initialized with RTT logic.");
}

void EspNow::startCommunication(const std::vector<DeviceSettings>& devices, uint32_t buttonPressMicros) {
    commDevices.clear();
    unsigned long now = millis();
    for (const auto& dev_setting : devices) {
        RunningDeviceComm new_device;
        new_device.id = dev_setting.id;
        unsigned long delay_ms = (dev_setting.delay_m * 60 + dev_setting.delay_s) * 1000UL;
        unsigned long play_ms = dev_setting.play_s * 1000UL;
        new_device.delayEndTime = now + delay_ms;
        new_device.playEndTime = new_device.delayEndTime + play_ms;
        new_device.isDelayDone = false;
        
        new_device.status = CommStatus::COMM_PENDING_RTT_REQUEST;
        new_device.sendAttempts = 0;
        new_device.txButtonPressSequenceMicros = buttonPressMicros;
        
        commDevices.push_back(new_device);
    }
    logPrintf(LogLevel::LOG_INFO, "COMM: Starting sequence for %d devices.", commDevices.size());
}

void EspNow::manageCommunication() {
    unsigned long currentTime = millis();
    for (auto& device : commDevices) {
        if (device.status == CommStatus::COMM_SUCCESS || device.status == CommStatus::COMM_FAILED) {
            continue;
        }

        bool needs_action = (device.sendAttempts == 0) || (currentTime - device.lastPacketSendTime >= RETRY_INTERVAL_MS);
        bool is_timed_out = (device.status == CommStatus::COMM_AWAITING_RTT_ACK || device.status == CommStatus::COMM_AWAITING_FINAL_ACK) && (currentTime >= device.ackTimeoutDeadline);

        if (is_timed_out) {
            logPrintf(LogLevel::LOG_WARN, "COMM: ID %d ACK timeout (attempt %d)", device.id, device.sendAttempts);
            device.sendAttempts++;
            if (device.status == CommStatus::COMM_AWAITING_RTT_ACK) device.status = CommStatus::COMM_PENDING_RTT_REQUEST;
            if (device.status == CommStatus::COMM_AWAITING_FINAL_ACK) device.status = CommStatus::COMM_PENDING_FINAL_COMMAND;
        }
        
        if (device.sendAttempts >= MAX_SEND_ATTEMPTS) {
            logPrintf(LogLevel::LOG_ERROR, "COMM: ID %d failed after all attempts.", device.id);
            device.status = CommStatus::COMM_FAILED;
            continue;
        }

        if (needs_action && (device.status == CommStatus::COMM_PENDING_RTT_REQUEST || device.status == CommStatus::COMM_PENDING_FINAL_COMMAND)) {
            sendCommand(device);
        }
    }
}

bool EspNow::sendCommand(RunningDeviceComm& device) {
    Comm::CommPacket packet;
    DeviceSettings& settings = Config::getInstance().getDeviceSettings()[device.id - 1];
    uint32_t delayMs = (settings.delay_m * 60 + settings.delay_s) * 1000UL;
    uint32_t playMs = settings.play_s * 1000UL;
    
    Comm::PacketType type = (device.status == CommStatus::COMM_PENDING_RTT_REQUEST) ? Comm::RTT_REQUEST : Comm::FINAL_COMMAND;
    
    Comm::fillPacket(packet, type, device.id, device.txButtonPressSequenceMicros, 
                     delayMs, playMs, device.currentRttUs, device.currentRxProcUs);

    logPrintf(LogLevel::LOG_DEBUG, "COMM: Sending %s to ID %d", type == Comm::RTT_REQUEST ? "RTT_REQUEST" : "FINAL_COMMAND", device.id);

    if (esp_now_send(broadcastAddress, (uint8_t*)&packet, sizeof(packet)) == ESP_OK) {
        device.lastPacketSendTime = millis();
        device.ackTimeoutDeadline = device.lastPacketSendTime + ACK_TIMEOUT_MS;
        device.lastTxTimestamp = packet.txMicros;
        device.status = (type == Comm::RTT_REQUEST) ? CommStatus::COMM_AWAITING_RTT_ACK : CommStatus::COMM_AWAITING_FINAL_ACK;
        return true;
    }
    return false;
}

bool EspNow::isCommunicationDone() {
    for (const auto& device : commDevices) {
        if (device.status != CommStatus::COMM_SUCCESS && device.status != CommStatus::COMM_FAILED) {
            return false;
        }
    }
    return true;
}

const std::vector<RunningDevice>& EspNow::getRunningDeviceStates() const {
    // This is a bit of a hack to fit the existing structure.
    // It re-casts the derived vector to the base vector.
    // A better solution would be to change the getter to return vector<RunningDeviceComm>
    return reinterpret_cast<const std::vector<RunningDevice>&>(commDevices);
}

void EspNow::onDataSent(const uint8_t* mac_addr, esp_now_send_status_t status) {
    // Optional: log send status
}

void EspNow::onDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
    if (instance == nullptr) return;

    const Comm::AckPacket* ackPkt = nullptr;
    if (!Comm::verifyAckPacket(data, len, ackPkt)) return;

    for (auto& device : instance->commDevices) {
        if (device.id == ackPkt->senderId && device.lastTxTimestamp == ackPkt->originalTxMicros) {
            if (device.status == CommStatus::COMM_AWAITING_RTT_ACK) {
                device.currentRttUs = micros() - ackPkt->originalTxMicros;
                device.currentRxProcUs = ackPkt->rxProcessingTimeUs;
                device.status = CommStatus::COMM_PENDING_FINAL_COMMAND;
                device.sendAttempts = 0; // Reset attempts for the next stage
                logPrintf(LogLevel::LOG_INFO, "COMM: ID %d RTT ACK OK. RTT=%u", device.id, device.currentRttUs);
                return;
            } else if (device.status == CommStatus::COMM_AWAITING_FINAL_ACK) {
                device.status = CommStatus::COMM_SUCCESS;
                logPrintf(LogLevel::LOG_INFO, "COMM: ID %d FINAL ACK OK. Sequence complete.", device.id);
                DeviceSettings& settings = Config::getInstance().getDeviceSettings()[device.id - 1];
                uint32_t delayMs = (settings.delay_m * 60 + settings.delay_s) * 1000UL;
                uint32_t playMs = settings.play_s * 1000UL;
                uint32_t one_way_latency_us = (device.currentRttUs > device.currentRxProcUs) ? (device.currentRttUs - device.currentRxProcUs) / 2 : 0;
                unsigned long compensated_start_time_us = device.txButtonPressSequenceMicros + one_way_latency_us;
                device.delayEndTime = (compensated_start_time_us / 1000) + delayMs;
                device.playEndTime = device.delayEndTime + playMs;
                return;
            }
        }
    }
}

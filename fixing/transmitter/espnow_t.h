/* ==========================================================================
 * File: espnow_t.h
 * 역할: 통신 상태 기계를 관리하는 EspNow 클래스를 선언합니다.
 * [수정] esp_now.h 헤더를 포함하여 타입 오류를 해결합니다.
 * ========================================================================== */
#ifndef ESPNOW_T_H
#define ESPNOW_T_H

#include <esp_now.h> // [수정] 이 헤더를 추가하여 esp_now 관련 타입을 인식하도록 합니다.
#include "config_t.h"
#include "utils_t.h" // RunningDevice 구조체를 사용하기 위함
#include <vector>

#define MAX_SEND_ATTEMPTS 3
#define ACK_TIMEOUT_MS 100
#define RETRY_INTERVAL_MS 50

enum class CommStatus {
    COMM_PENDING,
    COMM_PENDING_RTT_REQUEST,
    COMM_AWAITING_RTT_ACK,
    COMM_PENDING_FINAL_COMMAND,
    COMM_AWAITING_FINAL_ACK,
    COMM_SUCCESS,
    COMM_FAILED
};

struct RunningDeviceComm : public RunningDevice {
    CommStatus status;
    int sendAttempts;
    unsigned long lastPacketSendTime;
    unsigned long ackTimeoutDeadline;
    uint32_t txButtonPressSequenceMicros;
    uint32_t lastTxTimestamp;
    uint32_t currentRttUs;
    uint32_t currentRxProcUs;
};

class EspNow {
public:
    static EspNow& getInstance();
    void init();
    void startCommunication(const std::vector<DeviceSettings>& devices, uint32_t buttonPressMicros);
    void manageCommunication();
    bool isCommunicationDone();
    const std::vector<RunningDevice>& getRunningDeviceStates() const;

private:
    EspNow() {}
    EspNow(const EspNow&) = delete;
    void operator=(const EspNow&) = delete;

    static void onDataSent(const uint8_t* mac_addr, esp_now_send_status_t status);
    static void onDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len);
    
    bool sendCommand(RunningDeviceComm& device);

    std::vector<RunningDeviceComm> commDevices;
    static EspNow* instance; 
};

#endif // ESPNOW_T_H

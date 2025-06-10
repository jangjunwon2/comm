/* ==========================================================================
 * File: espnow_comm_shared.h
 * 역할: 원본 코드의 2단계 통신을 위한 데이터 구조체를 정의합니다.
 * ========================================================================== */
#ifndef ESPNOW_COMM_SHARED_H
#define ESPNOW_COMM_SHARED_H

#include <stdint.h>

#define WIFI_CHANNEL 1
#define MAGIC_NUMBER 0xDEADBEEF 

namespace Comm {
    enum PacketType : uint8_t {
        RTT_REQUEST,
        FINAL_COMMAND,
    };

    #pragma pack(push, 1)
    struct CommPacket {
        uint32_t magic;
        PacketType type;
        uint8_t targetDeviceId;
        uint32_t txButtonPressSequenceMicros;
        uint32_t txMicros;
        uint32_t delayMs;
        uint32_t playMs;
        uint32_t rttUs;
        uint32_t rxProcessingTimeUs;
    };

    struct AckPacket {
        uint32_t magic;
        uint8_t senderId;
        uint32_t originalTxMicros;
        uint32_t rxProcessingTimeUs;
    };
    #pragma pack(pop)

    inline void fillPacket(CommPacket& packet, PacketType type, uint8_t targetId, uint32_t txButtonPressSequenceMicros_arg, uint32_t original_delay_ms, uint32_t play_ms, uint32_t rttUs_arg, uint32_t rxProcessingTimeUs_arg) {
        packet.magic = MAGIC_NUMBER;
        packet.type = type;
        packet.targetDeviceId = targetId;
        packet.txButtonPressSequenceMicros = txButtonPressSequenceMicros_arg;
        packet.delayMs = original_delay_ms;
        packet.playMs = play_ms;
        packet.rttUs = rttUs_arg;
        packet.rxProcessingTimeUs = rxProcessingTimeUs_arg;
        packet.txMicros = micros();
    }

    inline bool verifyAckPacket(const uint8_t* data, int len, const AckPacket*& out_packet) {
        if (len != sizeof(AckPacket)) return false;
        out_packet = reinterpret_cast<const AckPacket*>(data);
        return out_packet->magic == MAGIC_NUMBER;
    }
}

#endif // ESPNOW_COMM_SHARED_H

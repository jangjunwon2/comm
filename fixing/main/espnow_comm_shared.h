/**
 * @file espnow_comm_shared.h
 * @brief ESP-NOW 통신을 위한 공유 구조체 및 헬퍼
 * @version 6.1.0
 * @date 2024-06-14
 */
#pragma once
#ifndef ESPNOW_COMM_SHARED_H
#define ESPNOW_COMM_SHARED_H

#include <stdint.h>
#include <string.h>
#include <Arduino.h>

namespace Comm {

//---------------------------------------------------------------------
//  서명 및 버전
//---------------------------------------------------------------------
static constexpr uint8_t kSig[4]   = { 'M','L','A','B' };
static constexpr uint8_t kVersion  = 0x03;

//---------------------------------------------------------------------
//  패킷 타입 열거형
//---------------------------------------------------------------------
enum PacketType : uint8_t {
    RTT_REQUEST = 0x01,
    FINAL_COMMAND = 0x02
};

//---------------------------------------------------------------------
//  패킷 레이아웃 (일관성을 위해 팩킹됨)
//---------------------------------------------------------------------
#pragma pack(push, 1)

// [FIX] 컴파일러 오류를 해결하기 위해 구조체 멤버 이름이 송신기와 일치하도록 수정
struct CommPacket {
    uint8_t  signature[4];
    uint8_t  version;
    uint8_t  packetType;
    uint8_t  targetId;
    uint32_t txButtonPressMicros; 
    uint32_t txMicros;
    uint32_t delayMs;
    uint32_t playMs;
    uint32_t lastKnownRttUs;
    uint32_t lastKnownRxProcessingTimeUs;
    uint8_t  crc8;
};

struct AckPacket {
    uint8_t  signature[4];
    uint8_t  version;
    uint8_t  senderId;
    uint32_t originalTxMicros;
    uint32_t rxProcessingTimeUs;
    uint8_t  crc8;
};

#pragma pack(pop)

static_assert(sizeof(CommPacket) == 32, "CommPacket size mismatch");
static_assert(sizeof(AckPacket) == 15, "AckPacket size mismatch");

//---------------------------------------------------------------------
//  CRC-8 함수
//---------------------------------------------------------------------
inline uint8_t crc8(const uint8_t *data, size_t len) {
    uint8_t crc = 0x00;
    while (len--) {
        uint8_t inbyte = *data++;
        for (uint8_t i = 8; i; --i) {
            uint8_t mix = (crc ^ inbyte) & 0x01;
            crc >>= 1;
            if (mix) crc ^= 0x8C;
            inbyte >>= 1;
        }
    }
    return crc;
}

//---------------------------------------------------------------------
//  송신부 헬퍼 함수
//---------------------------------------------------------------------
inline void fillPacket(CommPacket &pkt, PacketType type, uint8_t tgtId, uint32_t txButtonPressMicros, uint32_t delayMs, uint32_t playMs, uint32_t rttUs, uint32_t rxProcessingTimeUs) {
    memcpy(pkt.signature, kSig, 4);
    pkt.version        = kVersion;
    pkt.packetType     = type;
    pkt.targetId       = tgtId;
    pkt.txButtonPressMicros = txButtonPressMicros;
    pkt.txMicros       = micros();
    pkt.delayMs        = delayMs;
    pkt.playMs         = playMs;
    pkt.lastKnownRttUs = rttUs;
    pkt.lastKnownRxProcessingTimeUs = rxProcessingTimeUs;
    pkt.crc8           = crc8(reinterpret_cast<const uint8_t*>(&pkt), sizeof(CommPacket) - 1);
}

//---------------------------------------------------------------------
//  수신부 헬퍼 함수
//---------------------------------------------------------------------
inline bool verifyCommPacket(const uint8_t* data, size_t len, const CommPacket*& pkt, uint8_t myId, bool &forMe) {
    if (len < sizeof(CommPacket)) return false;
    pkt = reinterpret_cast<const CommPacket*>(data);

    if (memcmp(pkt->signature, kSig, 4) != 0) return false;
    if (pkt->version != kVersion) return false;
    
    uint8_t calc_crc = crc8(data, sizeof(CommPacket) - 1);
    if (calc_crc != pkt->crc8) return false;

    forMe = (pkt->targetId == 0) || (pkt->targetId == myId);
    return true;
}

inline bool verifyAckPacket(const uint8_t* data, size_t len, const AckPacket*& pkt) {
    if (len < sizeof(AckPacket)) return false;
    pkt = reinterpret_cast<const AckPacket*>(data);

    if (memcmp(pkt->signature, kSig, 4) != 0) return false;
    if (pkt->version != kVersion) return false;

    uint8_t calc_crc = crc8(data, sizeof(AckPacket) - 1);
    if (calc_crc != pkt->crc8) return false;

    return true;
}

inline void fillAckPacket(AckPacket& ack, uint8_t senderId, uint32_t originalTxMicros, uint32_t rxProcessingTime) {
    memcpy(ack.signature, kSig, 4);
    ack.version = kVersion;
    ack.senderId = senderId;
    ack.originalTxMicros = originalTxMicros;
    ack.rxProcessingTimeUs = rxProcessingTime;
    ack.crc8 = crc8(reinterpret_cast<const uint8_t*>(&ack), sizeof(AckPacket) - 1);
}

} // namespace Comm

#endif // ESPNOW_COMM_SHARED_H

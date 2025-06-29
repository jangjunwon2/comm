/**
 * @file espnow_comm_shared.h
 * @brief ESP-NOW 통신을 위한 공유 구조체 및 헬퍼 (지연시간 보정 기능 추가)
 * @version 6.0.0 // [MODIFIED] 버전 업데이트
 * @date 2024-06-13
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
static constexpr uint8_t kSig[4]   = { 'M','L','A','B' }; // "MLAB"
static constexpr uint8_t kVersion  = 0x03; // [MODIFIED] 레이아웃 변경으로 버전 업데이트

//---------------------------------------------------------------------
//  패킷 레이아웃 (일관성을 위해 팩킹됨)
//---------------------------------------------------------------------
#pragma pack(push, 1)

// [NEW] 패킷 타입 열거형
enum PacketType : uint8_t {
    RTT_REQUEST = 0x01,  // RTT 측정을 위한 요청 패킷
    FINAL_COMMAND = 0x02 // 최종 명령 실행을 위한 패킷 (보정값 포함)
};

// 명령 패킷 (송신기 -> 수신기)
struct CommPacket {
    uint8_t  signature[4];
    uint8_t  version;
    uint8_t  packetType;                // [NEW] 패킷 타입
    uint8_t  targetId;
    uint32_t txButtonPressMicros;       // [NEW] 버튼이 눌린 시점의 송신부 micros() 타임스탬프
    uint32_t txMicros;                  // 패킷 전송 시점의 송신부 micros() 타임스탬프
    uint32_t delayMs;
    uint32_t playMs;
    uint32_t lastKnownRttUs;            // [수정] RTT_REQUEST에서는 0, FINAL_COMMAND에서는 측정된 RTT
    uint32_t lastKnownRxProcessingTimeUs; // [수정] RTT_REQUEST에서는 0, FINAL_COMMAND에서는 측정된 수신기 처리 시간
    uint8_t  crc8;
};

// 확인 응답 패킷 (수신기 -> 송신기)
struct AckPacket {
    uint8_t  signature[4];
    uint8_t  version;
    uint8_t  senderId;
    uint32_t originalTxMicros;          // 원본 CommPacket의 txMicros 값
    uint32_t rxProcessingTimeUs;        // [수정됨] 수신기가 CMD를 받고 ACK를 보내기까지 걸린 처리 시간
    uint8_t  crc8;
};

#pragma pack(pop)

// 모든 플랫폼에서 구조체 크기가 예상대로인지 확인
// CommPacket 크기는 packetType 추가로 인해 변경됨
static_assert(sizeof(CommPacket) == 32, "CommPacket size mismatch"); // 31 + 1 (packetType) = 32 bytes
static_assert(sizeof(AckPacket) == 15, "AckPacket size mismatch");

//---------------------------------------------------------------------
//  Dallas/Maxim CRC-8 (다항식 0x31, 초기값 0x00)
//---------------------------------------------------------------------
inline uint8_t crc8(const uint8_t *data, size_t len) {
    uint8_t crc = 0x00;
    while (len--) {
        uint8_t inbyte = *data++;
        for (uint8_t i = 8; i; --i) {
            uint8_t mix = (crc ^ inbyte) & 0x01;
            crc >>= 1;
            if (mix) crc ^= 0x8C; // 0x31 반전됨
            inbyte >>= 1;
        }
    }
    return crc;
}

//---------------------------------------------------------------------
//  송신부 헬퍼 함수
//---------------------------------------------------------------------
// [수정됨] packetType 파라미터 추가
inline void fillPacket(CommPacket &pkt, PacketType type, uint8_t tgtId, uint32_t txButtonPressMicros, uint32_t delayMs, uint32_t playMs, uint32_t rttUs, uint32_t rxProcessingTimeUs) {
    memcpy(pkt.signature, kSig, 4);
    pkt.version        = kVersion;
    pkt.packetType     = type;          // [NEW]
    pkt.targetId       = tgtId;
    pkt.txButtonPressMicros = txButtonPressMicros;
    pkt.txMicros       = micros();      // 패킷 전송 시각
    pkt.delayMs        = delayMs;
    pkt.playMs         = playMs;
    pkt.lastKnownRttUs = rttUs;         // [MODIFIED] RTT_REQUEST 시 0, FINAL_COMMAND 시 실제 RTT
    pkt.lastKnownRxProcessingTimeUs = rxProcessingTimeUs; // [MODIFIED] RTT_REQUEST 시 0, FINAL_COMMAND 시 실제 Rx 처리 시간
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

    // targetId가 0(브로드캐스트)이거나 내 ID와 일치할 때 forMe = true
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

// [수정됨] rxProcessingTime 파라미터 추가
inline void fillAckPacket(AckPacket& ack, uint8_t senderId, uint32_t originalTxMicros, uint32_t rxProcessingTime) {
    memcpy(ack.signature, kSig, 4);
    ack.version = kVersion;
    ack.senderId = senderId;
    ack.originalTxMicros = originalTxMicros;
    ack.rxProcessingTimeUs = rxProcessingTime; // [NEW] 수신기 처리 시간 추가
    ack.crc8 = crc8(reinterpret_cast<const uint8_t*>(&ack), sizeof(AckPacket) - 1);
}

inline uint32_t latencyUs(const CommPacket &pkt) {
    return micros() - pkt.txMicros;
}

} // namespace Comm

#endif // ESPNOW_COMM_SHARED_H

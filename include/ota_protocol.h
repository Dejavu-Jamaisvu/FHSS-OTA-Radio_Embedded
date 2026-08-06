#ifndef OTA_PROTOCOL_H
#define OTA_PROTOCOL_H

/*
 * ota-protocol — firmware-esp32 <-> gateway-ota 공유 OTA 패킷 규격
 * Draft v0.1 (팀 최종 합의 전, docs-architecture에서 확정 예정)
 *
 * 순수 C99, 헤더 하나로 끝나는 header-only 라이브러리입니다.
 * <stdint.h>/<stddef.h>/<stdbool.h>/<string.h>만 쓰기 때문에 ESP-IDF(ESP32),
 * 데스크톱 Linux(gateway-ota, Qt/C++), 커널 모듈이 아닌 어떤 C/C++ 빌드
 * 환경에서도 그대로 컴파일됩니다. (C++에서 쓸 때는 extern "C"로 자동 처리됨)
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------- 패킷 타입 ---------- */
typedef enum {
    OTA_PKT_DATA = 0, /* BIN 청크 데이터 */
    OTA_PKT_ACK  = 1, /* 청크 수신 확인 */
    OTA_PKT_NACK = 2, /* 청크 수신 실패(CRC 오류 등) 통보, 재전송 요청 */
} ota_packet_type_t;

#define OTA_PROTOCOL_VERSION 1u

/* CC1101의 TX/RX FIFO가 64byte라서, 패킷 하나(헤더+payload)는 이 안에 들어와야 함 */
#define OTA_CC1101_FIFO_SIZE 64u

/*
 * 패킷 헤더 (9byte, 정렬 패딩 없음).
 * #pragma pack(push,1)로 컴파일러가 멤버 사이에 정렬 패딩을 넣지 못하게 막아서,
 * sizeof(ota_packet_header_t)가 항상 "필드 크기의 합"과 정확히 같게 만듭니다.
 * 그래야 이 구조체를 그대로 바이트로 복사했을 때 송신·수신 측 크기가 어긋나지 않습니다.
 */
#pragma pack(push, 1)
typedef struct {
    uint8_t  version;         /* 프로토콜 버전 */
    uint8_t  type;            /* ota_packet_type_t */
    uint16_t seq;             /* 이 패킷의 시퀀스 번호 (0 ~ total_chunks-1). ACK/NACK은 대상 청크의 seq */
    uint16_t total_chunks;    /* 파일 전체를 나눈 청크 총 개수 (DATA에서만 의미 있음) */
    uint8_t  payload_length;  /* 패딩을 뺀 실제 데이터 길이 (byte). ACK/NACK은 0 */
    uint16_t crc16;           /* payload에 대한 CRC-16/CCITT-FALSE. ACK/NACK은 0 */
} ota_packet_header_t;
#pragma pack(pop)

#define OTA_PACKET_HEADER_SIZE ((uint32_t)sizeof(ota_packet_header_t)) /* 9 byte */
#define OTA_MAX_PAYLOAD_SIZE   (OTA_CC1101_FIFO_SIZE - OTA_PACKET_HEADER_SIZE) /* 55 byte */
#define OTA_MAX_PACKET_SIZE    OTA_CC1101_FIFO_SIZE

/* ---------- CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF) ---------- */
static inline uint16_t ota_protocol_crc16(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xFFFFu;
    for (size_t i = 0; i < length; ++i) {
        crc ^= (uint16_t)((uint16_t)data[i] << 8);
        for (int bit = 0; bit < 8; ++bit) {
            if (crc & 0x8000u)
                crc = (uint16_t)((crc << 1) ^ 0x1021u);
            else
                crc = (uint16_t)(crc << 1);
        }
    }
    return crc;
}

/*
 * DATA 패킷(헤더+payload)을 packet_out 버퍼에 직렬화.
 * packet_out은 최소 OTA_MAX_PACKET_SIZE 만큼 있어야 안전합니다.
 * 반환값: 실제로 채운 바이트 수. 실패(payload가 너무 크거나 버퍼 부족) 시 0.
 */
static inline size_t ota_protocol_encode_data(
    uint8_t *packet_out, size_t packet_out_capacity,
    uint16_t seq, uint16_t total_chunks,
    const uint8_t *payload, size_t payload_length)
{
    if (payload == NULL || payload_length > OTA_MAX_PAYLOAD_SIZE)
        return 0;
    if (packet_out_capacity < OTA_PACKET_HEADER_SIZE + payload_length)
        return 0;

    ota_packet_header_t header;
    header.version = OTA_PROTOCOL_VERSION;
    header.type = (uint8_t)OTA_PKT_DATA;
    header.seq = seq;
    header.total_chunks = total_chunks;
    header.payload_length = (uint8_t)payload_length;
    header.crc16 = ota_protocol_crc16(payload, payload_length);

    memcpy(packet_out, &header, OTA_PACKET_HEADER_SIZE);
    memcpy(packet_out + OTA_PACKET_HEADER_SIZE, payload, payload_length);
    return OTA_PACKET_HEADER_SIZE + payload_length;
}

/*
 * ACK/NACK 패킷(헤더만, payload 없음)을 packet_out 버퍼에 직렬화.
 * 반환값: 실제로 채운 바이트 수(=OTA_PACKET_HEADER_SIZE). 실패 시 0.
 */
static inline size_t ota_protocol_encode_control(
    uint8_t *packet_out, size_t packet_out_capacity,
    ota_packet_type_t type, uint16_t seq)
{
    if (packet_out_capacity < OTA_PACKET_HEADER_SIZE)
        return 0;
    if (type != OTA_PKT_ACK && type != OTA_PKT_NACK)
        return 0;

    ota_packet_header_t header;
    header.version = OTA_PROTOCOL_VERSION;
    header.type = (uint8_t)type;
    header.seq = seq;
    header.total_chunks = 0;
    header.payload_length = 0;
    header.crc16 = 0;

    memcpy(packet_out, &header, OTA_PACKET_HEADER_SIZE);
    return OTA_PACKET_HEADER_SIZE;
}

/*
 * 수신한 바이트열(packet)을 헤더+payload로 해석하고, DATA 패킷이면 CRC16까지 검증.
 * 성공 시 true를 반환하며 header_out/payload_out/payload_length_out을 채웁니다.
 * payload_out은 packet 내부를 가리키는 포인터입니다(별도 복사 없음, packet의
 * 수명이 끝나기 전까지만 유효).
 * ACK/NACK 패킷이면 payload_out=NULL, payload_length_out=0으로 채우고 true 반환.
 * 실패(길이 부족, CRC 불일치) 시 false — 이 경우 호출자가 NACK 처리해야 함.
 */
static inline bool ota_protocol_decode(
    const uint8_t *packet, size_t packet_length,
    ota_packet_header_t *header_out,
    const uint8_t **payload_out, size_t *payload_length_out)
{
    if (packet == NULL || header_out == NULL || packet_length < OTA_PACKET_HEADER_SIZE)
        return false;

    memcpy(header_out, packet, OTA_PACKET_HEADER_SIZE);

    if (header_out->type == (uint8_t)OTA_PKT_DATA) {
        if (packet_length < OTA_PACKET_HEADER_SIZE + header_out->payload_length)
            return false;

        const uint8_t *payload = packet + OTA_PACKET_HEADER_SIZE;
        if (ota_protocol_crc16(payload, header_out->payload_length) != header_out->crc16)
            return false; /* CRC 불일치 -> 호출자가 NACK 보내야 함 */

        if (payload_out) *payload_out = payload;
        if (payload_length_out) *payload_length_out = header_out->payload_length;
    } else {
        if (payload_out) *payload_out = NULL;
        if (payload_length_out) *payload_length_out = 0;
    }
    return true;
}

#ifdef __cplusplus
}
#endif

#endif /* OTA_PROTOCOL_H */

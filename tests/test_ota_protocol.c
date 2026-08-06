/*
 * ota_protocol.h 셀프테스트.
 * Qt도 ESP-IDF도 필요 없이 gcc 하나로만 빌드/실행됩니다:
 *   gcc -std=c99 -Wall -Wextra -I../include test_ota_protocol.c -o test_ota_protocol
 *   ./test_ota_protocol
 */
#include "ota_protocol.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_header_size(void)
{
    assert(OTA_PACKET_HEADER_SIZE == 9);
    assert(OTA_MAX_PAYLOAD_SIZE == 55);
    assert(OTA_MAX_PACKET_SIZE == 64);
    printf("[OK] header size = %u byte, max payload = %u byte\n",
           OTA_PACKET_HEADER_SIZE, OTA_MAX_PAYLOAD_SIZE);
}

static void test_data_roundtrip(void)
{
    const uint8_t payload[20] = "hello ota-protocol!";
    uint8_t packet[OTA_MAX_PACKET_SIZE];

    size_t written = ota_protocol_encode_data(packet, sizeof(packet), 7, 100, payload, sizeof(payload));
    assert(written == OTA_PACKET_HEADER_SIZE + sizeof(payload));

    ota_packet_header_t header;
    const uint8_t *decoded_payload = NULL;
    size_t decoded_length = 0;
    bool ok = ota_protocol_decode(packet, written, &header, &decoded_payload, &decoded_length);

    assert(ok);
    assert(header.type == (uint8_t)OTA_PKT_DATA);
    assert(header.seq == 7);
    assert(header.total_chunks == 100);
    assert(decoded_length == sizeof(payload));
    assert(memcmp(decoded_payload, payload, sizeof(payload)) == 0);
    printf("[OK] DATA 패킷 인코딩/디코딩 왕복 성공 (seq=%u, len=%zu)\n", header.seq, decoded_length);
}

static void test_corrupted_payload_fails_crc(void)
{
    const uint8_t payload[4] = { 0x01, 0x02, 0x03, 0x04 };
    uint8_t packet[OTA_MAX_PACKET_SIZE];

    size_t written = ota_protocol_encode_data(packet, sizeof(packet), 1, 10, payload, sizeof(payload));
    assert(written > 0);

    /* payload 한 byte를 일부러 깨뜨림 -> CRC가 안 맞아야 함 */
    packet[OTA_PACKET_HEADER_SIZE] ^= 0xFF;

    ota_packet_header_t header;
    const uint8_t *decoded_payload = NULL;
    size_t decoded_length = 0;
    bool ok = ota_protocol_decode(packet, written, &header, &decoded_payload, &decoded_length);

    assert(!ok); /* CRC 불일치로 실패해야 정상 */
    printf("[OK] 손상된 payload는 CRC 검증에서 거부됨\n");
}

static void test_ack_nack_roundtrip(void)
{
    uint8_t ack_packet[OTA_MAX_PACKET_SIZE];
    size_t ack_len = ota_protocol_encode_control(ack_packet, sizeof(ack_packet), OTA_PKT_ACK, 42);
    assert(ack_len == OTA_PACKET_HEADER_SIZE);

    ota_packet_header_t header;
    const uint8_t *payload = NULL;
    size_t payload_len = 0;
    bool ok = ota_protocol_decode(ack_packet, ack_len, &header, &payload, &payload_len);

    assert(ok);
    assert(header.type == (uint8_t)OTA_PKT_ACK);
    assert(header.seq == 42);
    assert(payload == NULL);
    assert(payload_len == 0);
    printf("[OK] ACK 패킷 인코딩/디코딩 성공 (seq=%u)\n", header.seq);

    uint8_t nack_packet[OTA_MAX_PACKET_SIZE];
    size_t nack_len = ota_protocol_encode_control(nack_packet, sizeof(nack_packet), OTA_PKT_NACK, 43);
    assert(nack_len == OTA_PACKET_HEADER_SIZE);
    printf("[OK] NACK 패킷 인코딩 성공\n");
}

static void test_payload_too_large_rejected(void)
{
    uint8_t oversized_payload[OTA_MAX_PAYLOAD_SIZE + 1];
    memset(oversized_payload, 0xAB, sizeof(oversized_payload));
    uint8_t packet[OTA_MAX_PACKET_SIZE + 16];

    size_t written = ota_protocol_encode_data(packet, sizeof(packet), 0, 1, oversized_payload, sizeof(oversized_payload));
    assert(written == 0); /* OTA_MAX_PAYLOAD_SIZE 초과라 실패해야 정상 */
    printf("[OK] 최대 payload 크기를 넘으면 인코딩이 거부됨\n");
}

int main(void)
{
    test_header_size();
    test_data_roundtrip();
    test_corrupted_payload_fails_crc();
    test_ack_nack_roundtrip();
    test_payload_too_large_rejected();
    printf("\n모든 테스트 통과\n");
    return 0;
}

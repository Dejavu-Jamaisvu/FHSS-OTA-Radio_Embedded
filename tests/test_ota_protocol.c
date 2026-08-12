/*
 * ota_protocol.h(v0.2) 셀프테스트.
 * Qt도 ESP-IDF도 필요 없이 gcc 하나로만 빌드/실행됩니다:
 *   gcc -std=c99 -Wall -Wextra -I../include test_ota_protocol.c -o test_ota_protocol
 *   ./test_ota_protocol
 */
#include "ota_protocol.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_sizes(void)
{
    assert(OTA_RF_PACKET_BODY_MAX_SIZE == 60);
    assert(OTA_DATA_HEADER_SIZE == 13);
    assert(OTA_MAX_PAYLOAD_SIZE == 47);
    assert(OTA_START_PACKET_SIZE == 50);
    assert(OTA_END_PACKET_SIZE == 14);
    assert(OTA_ACK_PACKET_SIZE == 12);
    printf("[OK] 패킷 크기: START=%u DATA header=%u(+payload<=%u) END=%u ACK/NACK=%u\n",
           OTA_START_PACKET_SIZE, OTA_DATA_HEADER_SIZE, OTA_MAX_PAYLOAD_SIZE,
           OTA_END_PACKET_SIZE, OTA_ACK_PACKET_SIZE);
}

static void test_little_endian_byte_order(void)
{
    /* session_id = 0x11223344를 LE로 쓰면 바이트 순서가 44 33 22 11 이어야 함 */
    uint8_t buf[4];
    ota_write_u32_le(buf, 0x11223344u);
    assert(buf[0] == 0x44 && buf[1] == 0x33 && buf[2] == 0x22 && buf[3] == 0x11);
    assert(ota_read_u32_le(buf) == 0x11223344u);
    printf("[OK] Little Endian 직렬화 확인 (0x11223344 -> 44 33 22 11)\n");

    /* device_id용 3byte(24bit) 버전도 같은 방식으로 확인 */
    uint8_t buf24[3];
    ota_write_u24_le(buf24, 0x00AABBCCu);
    assert(buf24[0] == 0xCC && buf24[1] == 0xBB && buf24[2] == 0xAA);
    assert(ota_read_u24_le(buf24) == 0x00AABBCCu);
    printf("[OK] 24bit(device_id) Little Endian 직렬화 확인 (0xAABBCC -> CC BB AA)\n");
}

static void test_total_chunks_calc(void)
{
    assert(ota_protocol_total_chunks(0) == 0);
    assert(ota_protocol_total_chunks(1) == 1);
    assert(ota_protocol_total_chunks(OTA_MAX_PAYLOAD_SIZE) == 1);
    assert(ota_protocol_total_chunks(OTA_MAX_PAYLOAD_SIZE + 1) == 2);
    assert(ota_protocol_total_chunks(100000) == (100000 + OTA_MAX_PAYLOAD_SIZE - 1) / OTA_MAX_PAYLOAD_SIZE);
    printf("[OK] total_chunks 계산 (올림 나눗셈) 확인\n");
}

static void test_start_roundtrip(void)
{
    ota_start_fields_t fields;
    fields.session_id = 0xAABBCCDDu;
    fields.target_device_id = OTA_BROADCAST_DEVICE_ID;
    fields.image_size = 123456;
    fields.total_chunks = ota_protocol_total_chunks(123456);
    for (int i = 0; i < 32; ++i) fields.image_sha256[i] = (uint8_t)i;

    uint8_t packet[OTA_START_PACKET_SIZE];
    size_t written = ota_protocol_encode_start(packet, sizeof(packet), &fields);
    assert(written == OTA_START_PACKET_SIZE);

    uint8_t version;
    ota_packet_type_t type;
    assert(ota_protocol_peek_type(packet, written, &version, &type));
    assert(version == OTA_PROTOCOL_VERSION);
    assert(type == OTA_PKT_START);

    ota_start_fields_t decoded;
    memset(&decoded, 0, sizeof(decoded));
    bool ok = ota_protocol_decode_start(packet, written, &decoded);
    assert(ok);
    assert(decoded.session_id == fields.session_id);
    assert(decoded.target_device_id == OTA_BROADCAST_DEVICE_ID);
    assert(decoded.image_size == fields.image_size);
    assert(decoded.total_chunks == fields.total_chunks);
    assert(memcmp(decoded.image_sha256, fields.image_sha256, 32) == 0);
    printf("[OK] START 패킷 인코딩/디코딩 왕복 성공 (session=0x%08X, image_size=%u)\n",
           decoded.session_id, decoded.image_size);
}

static void test_data_roundtrip(void)
{
    const uint8_t payload[20] = "hello ota-protocol!";
    uint8_t packet[OTA_RF_PACKET_BODY_MAX_SIZE];

    size_t written = ota_protocol_encode_data(packet, sizeof(packet), 0xAABBCCDDu, 7, payload, sizeof(payload));
    assert(written == OTA_DATA_HEADER_SIZE + sizeof(payload));

    ota_data_header_fields_t header;
    memset(&header, 0, sizeof(header));
    const uint8_t *decoded_payload = NULL;
    size_t decoded_length = 0;
    bool ok = ota_protocol_decode_data(packet, written, &header, &decoded_payload, &decoded_length);

    assert(ok);
    assert(header.session_id == 0xAABBCCDDu);
    assert(header.sequence == 7);
    assert(decoded_length == sizeof(payload));
    assert(memcmp(decoded_payload, payload, sizeof(payload)) == 0);
    printf("[OK] DATA 패킷 인코딩/디코딩 왕복 성공 (seq=%u, len=%zu)\n", header.sequence, decoded_length);
}

static void test_corrupted_payload_fails_crc(void)
{
    const uint8_t payload[4] = { 0x01, 0x02, 0x03, 0x04 };
    uint8_t packet[OTA_RF_PACKET_BODY_MAX_SIZE];

    size_t written = ota_protocol_encode_data(packet, sizeof(packet), 1, 10, payload, sizeof(payload));
    assert(written > 0);

    /* payload 한 byte를 일부러 깨뜨림 -> CRC가 안 맞아야 함 */
    packet[OTA_DATA_HEADER_SIZE] ^= 0xFF;

    ota_data_header_fields_t header_before;
    memset(&header_before, 0xEE, sizeof(header_before)); /* 손대지 않았는지 확인용 마커 */
    ota_data_header_fields_t header = header_before;
    const uint8_t *decoded_payload = NULL;
    size_t decoded_length = 0;
    bool ok = ota_protocol_decode_data(packet, written, &header, &decoded_payload, &decoded_length);

    assert(!ok); /* CRC 불일치로 실패해야 정상 */
    assert(memcmp(&header, &header_before, sizeof(header)) == 0); /* 실패 시 out 파라미터 안 건드림 */
    printf("[OK] 손상된 payload는 CRC 검증에서 거부되고 out 파라미터도 안 바뀜\n");
}

static void test_payload_too_large_rejected(void)
{
    uint8_t oversized_payload[OTA_MAX_PAYLOAD_SIZE + 1];
    memset(oversized_payload, 0xAB, sizeof(oversized_payload));
    uint8_t packet[OTA_RF_PACKET_BODY_MAX_SIZE + 16];

    size_t written = ota_protocol_encode_data(packet, sizeof(packet), 0, 1, oversized_payload, sizeof(oversized_payload));
    assert(written == 0); /* OTA_MAX_PAYLOAD_SIZE 초과라 실패해야 정상 */
    printf("[OK] 최대 payload 크기를 넘으면 인코딩이 거부됨\n");
}

static void test_end_roundtrip(void)
{
    ota_end_fields_t fields = { .session_id = 0xAABBCCDDu, .image_size = 123456, .total_chunks = 2628 };
    uint8_t packet[OTA_END_PACKET_SIZE];

    size_t written = ota_protocol_encode_end(packet, sizeof(packet), &fields);
    assert(written == OTA_END_PACKET_SIZE);

    ota_end_fields_t decoded;
    memset(&decoded, 0, sizeof(decoded));
    bool ok = ota_protocol_decode_end(packet, written, &decoded);
    assert(ok);
    assert(decoded.session_id == fields.session_id);
    assert(decoded.image_size == fields.image_size);
    assert(decoded.total_chunks == fields.total_chunks);
    printf("[OK] END 패킷 인코딩/디코딩 왕복 성공\n");
}

static void test_ack_nack_roundtrip(void)
{
    ota_ack_fields_t ack_fields = {
        .session_id = 0xAABBCCDDu,
        .acknowledged_type = (uint8_t)OTA_PKT_DATA,
        .sequence = 42,
        .result_code = (uint8_t)OTA_RESULT_OK,
    };
    uint8_t ack_packet[OTA_ACK_PACKET_SIZE];
    size_t ack_len = ota_protocol_encode_ack(ack_packet, sizeof(ack_packet), OTA_PKT_ACK, &ack_fields);
    assert(ack_len == OTA_ACK_PACKET_SIZE);

    ota_packet_type_t decoded_type;
    ota_ack_fields_t decoded;
    memset(&decoded, 0, sizeof(decoded));
    bool ok = ota_protocol_decode_ack(ack_packet, ack_len, &decoded_type, &decoded);
    assert(ok);
    assert(decoded_type == OTA_PKT_ACK);
    assert(decoded.sequence == 42);
    assert(decoded.result_code == (uint8_t)OTA_RESULT_OK);
    printf("[OK] ACK 패킷 인코딩/디코딩 성공 (seq=%u)\n", decoded.sequence);

    ota_ack_fields_t nack_fields = {
        .session_id = 0xAABBCCDDu,
        .acknowledged_type = (uint8_t)OTA_PKT_DATA,
        .sequence = 43,
        .result_code = (uint8_t)OTA_RESULT_INVALID_CRC,
    };
    uint8_t nack_packet[OTA_ACK_PACKET_SIZE];
    size_t nack_len = ota_protocol_encode_ack(nack_packet, sizeof(nack_packet), OTA_PKT_NACK, &nack_fields);
    assert(nack_len == OTA_ACK_PACKET_SIZE);

    ok = ota_protocol_decode_ack(nack_packet, nack_len, &decoded_type, &decoded);
    assert(ok);
    assert(decoded_type == OTA_PKT_NACK);
    assert(decoded.result_code == (uint8_t)OTA_RESULT_INVALID_CRC);
    printf("[OK] NACK 패킷 인코딩/디코딩 성공 (result_code=INVALID_CRC)\n");
}

static void test_peek_type_dispatches_correctly(void)
{
    ota_ack_fields_t fields = { .session_id = 1, .acknowledged_type = (uint8_t)OTA_PKT_END,
                                 .sequence = OTA_CONTROL_SEQUENCE, .result_code = (uint8_t)OTA_RESULT_OK };
    uint8_t packet[OTA_ACK_PACKET_SIZE];
    ota_protocol_encode_ack(packet, sizeof(packet), OTA_PKT_ACK, &fields);

    uint8_t version = 0;
    ota_packet_type_t type;
    assert(ota_protocol_peek_type(packet, sizeof(packet), &version, &type));
    assert(version == OTA_PROTOCOL_VERSION);
    assert(type == OTA_PKT_ACK);
    printf("[OK] peek_type으로 디코더 분기 가능 확인\n");
}

static void test_discover_roundtrip(void)
{
    uint8_t packet[OTA_DISCOVER_PACKET_SIZE];
    size_t written = ota_protocol_encode_discover(packet, sizeof(packet));
    assert(written == OTA_DISCOVER_PACKET_SIZE);
    assert(ota_protocol_decode_discover(packet, written));
    printf("[OK] DISCOVER 패킷(바디 없음) 인코딩/디코딩 성공\n");
}

static void test_discover_ack_roundtrip(void)
{
    /* device_id는 이제 3byte(0 ~ OTA_DEVICE_ID_MAX)만 담김 */
    ota_discover_ack_fields_t fields = {
        .device_id = 0x00AABBCCu,
        .fw_major = 1,
        .fw_minor = 2,
        .fw_patch = 3,
    };
    uint8_t packet[OTA_DISCOVER_ACK_PACKET_SIZE];
    size_t written = ota_protocol_encode_discover_ack(packet, sizeof(packet), &fields);
    assert(written == OTA_DISCOVER_ACK_PACKET_SIZE);
    assert(written == 8); /* 2(header) + 3(device_id) + 3(fw) */

    uint8_t version = 0;
    ota_packet_type_t type;
    assert(ota_protocol_peek_type(packet, written, &version, &type));
    assert(type == OTA_PKT_DISCOVER_ACK);

    ota_discover_ack_fields_t decoded;
    memset(&decoded, 0, sizeof(decoded));
    bool ok = ota_protocol_decode_discover_ack(packet, written, &decoded);
    assert(ok);
    assert(decoded.device_id == fields.device_id);
    assert(decoded.fw_major == 1 && decoded.fw_minor == 2 && decoded.fw_patch == 3);
    printf("[OK] DISCOVER_ACK 패킷 인코딩/디코딩 왕복 성공 (device_id=0x%06X, fw=%u.%u.%u)\n",
           decoded.device_id, decoded.fw_major, decoded.fw_minor, decoded.fw_patch);
}

static void test_discover_ack_device_id_too_large_rejected(void)
{
    /* 3byte(24bit)에 안 들어가는 값은 인코딩이 거부돼야 함 (조용히 잘리면 안 됨) */
    ota_discover_ack_fields_t fields = {
        .device_id = OTA_DEVICE_ID_MAX + 1u, /* 0x01000000 — 4byte째가 필요한 값 */
        .fw_major = 1, .fw_minor = 0, .fw_patch = 0,
    };
    uint8_t packet[OTA_DISCOVER_ACK_PACKET_SIZE];
    size_t written = ota_protocol_encode_discover_ack(packet, sizeof(packet), &fields);
    assert(written == 0);
    printf("[OK] device_id가 24bit를 넘으면 DISCOVER_ACK 인코딩이 거부됨\n");
}

int main(void)
{
    test_sizes();
    test_little_endian_byte_order();
    test_total_chunks_calc();
    test_start_roundtrip();
    test_data_roundtrip();
    test_corrupted_payload_fails_crc();
    test_payload_too_large_rejected();
    test_end_roundtrip();
    test_ack_nack_roundtrip();
    test_peek_type_dispatches_correctly();
    test_discover_roundtrip();
    test_discover_ack_roundtrip();
    test_discover_ack_device_id_too_large_rejected();
    printf("\n모든 테스트 통과\n");
    return 0;
}

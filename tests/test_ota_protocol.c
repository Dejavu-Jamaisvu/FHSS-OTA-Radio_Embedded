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
    assert(OTA_DATA_HEADER_SIZE == 12);
    assert(OTA_MAX_PAYLOAD_SIZE == 48);
    assert(OTA_START_PACKET_SIZE == 49);
    assert(OTA_END_PACKET_SIZE == 13);
    assert(OTA_ACK_PACKET_SIZE == 11);
    assert(OTA_FHSS_CONFIG_PACKET_SIZE == 33);
    assert(OTA_FHSS_ACTIVATE_PACKET_SIZE == 13);
    assert(OTA_FHSS_SYNC_PACKET_SIZE == 13);
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

    ota_packet_type_t type;
    assert(ota_protocol_peek_type(packet, written, &type));
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

    ota_packet_type_t type;
    assert(ota_protocol_peek_type(packet, sizeof(packet), &type));
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
    assert(written == 7); /* 1(type) + 3(device_id) + 3(fw) */

    ota_packet_type_t type;
    assert(ota_protocol_peek_type(packet, written, &type));
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

static ota_fhss_config_fields_t valid_fhss_config(void)
{
    ota_fhss_config_fields_t fields = {
        .session_id = 0x11223344u,
        .target_device_id = OTA_BROADCAST_DEVICE_ID,
        .generation = 7u,
        .algorithm_version = OTA_FHSS_ALGORITHM_VERSION,
        .channel_profile_id = 1u,
        .first_channel = 1u,
        .channel_count = 100u,
        .rendezvous_channel = 1u,
        .reserved_channel = 0u,
        .seed = OTA_FHSS_DEFAULT_SEED,
        .slot_duration_us = 300000u,
        .channel_switch_guard_us = 5000u,
    };
    return fields;
}

static void test_fhss_config_roundtrip(void)
{
    ota_fhss_config_fields_t fields = valid_fhss_config();
    ota_fhss_config_fields_t decoded;
    uint8_t packet[OTA_FHSS_CONFIG_PACKET_SIZE];
    ota_packet_type_t type;

    memset(&decoded, 0, sizeof(decoded));
    assert(ota_protocol_encode_fhss_config(packet, sizeof(packet), &fields) ==
           OTA_FHSS_CONFIG_PACKET_SIZE);
    assert(ota_protocol_peek_type(packet, sizeof(packet), &type));
    assert(type == OTA_PKT_FHSS_CONFIG);
    assert(ota_protocol_decode_fhss_config(packet, sizeof(packet), &decoded));
    assert(decoded.session_id == fields.session_id);
    assert(decoded.target_device_id == fields.target_device_id);
    assert(decoded.generation == fields.generation);
    assert(decoded.algorithm_version == fields.algorithm_version);
    assert(decoded.channel_profile_id == fields.channel_profile_id);
    assert(decoded.first_channel == fields.first_channel);
    assert(decoded.channel_count == fields.channel_count);
    assert(decoded.rendezvous_channel == fields.rendezvous_channel);
    assert(decoded.reserved_channel == fields.reserved_channel);
    assert(decoded.seed == fields.seed);
    assert(decoded.slot_duration_us == fields.slot_duration_us);
    assert(decoded.channel_switch_guard_us ==
           fields.channel_switch_guard_us);

    /* session_id 0x11223344 is serialized Little Endian. */
    assert(packet[1] == 0x44 && packet[2] == 0x33 &&
           packet[3] == 0x22 && packet[4] == 0x11);
    printf("[OK] FHSS_CONFIG 왕복/바이트 순서 확인\n");
}

static void test_fhss_config_crc_failure(void)
{
    ota_fhss_config_fields_t fields = valid_fhss_config();
    ota_fhss_config_fields_t before;
    ota_fhss_config_fields_t decoded;
    uint8_t packet[OTA_FHSS_CONFIG_PACKET_SIZE];

    assert(ota_protocol_encode_fhss_config(packet, sizeof(packet), &fields) ==
           OTA_FHSS_CONFIG_PACKET_SIZE);
    packet[19] ^= 0x80u; /* seed를 깨뜨리되 CRC는 갱신하지 않는다. */
    memset(&before, 0xA5, sizeof(before));
    decoded = before;
    assert(!ota_protocol_decode_fhss_config(packet, sizeof(packet), &decoded));
    assert(memcmp(&decoded, &before, sizeof(decoded)) == 0);
    printf("[OK] 손상된 FHSS_CONFIG CRC 거부 및 out 파라미터 보존\n");
}

static void test_fhss_config_invalid_values(void)
{
    ota_fhss_config_fields_t fields = valid_fhss_config();
    uint8_t packet[OTA_FHSS_CONFIG_PACKET_SIZE];

    fields.channel_count = 0u;
    assert(ota_protocol_encode_fhss_config(packet, sizeof(packet), &fields) == 0u);
    fields = valid_fhss_config();
    fields.first_channel = 250u;
    fields.channel_count = 10u;
    assert(ota_protocol_encode_fhss_config(packet, sizeof(packet), &fields) == 0u);
    fields = valid_fhss_config();
    fields.rendezvous_channel = 2u;
    assert(ota_protocol_encode_fhss_config(packet, sizeof(packet), &fields) == 0u);
    fields = valid_fhss_config();
    fields.reserved_channel = 50u;
    assert(ota_protocol_encode_fhss_config(packet, sizeof(packet), &fields) == 0u);
    fields = valid_fhss_config();
    fields.channel_switch_guard_us = fields.slot_duration_us;
    assert(ota_protocol_encode_fhss_config(packet, sizeof(packet), &fields) == 0u);
    printf("[OK] 잘못된 FHSS_CONFIG 값 거부\n");
}

static void test_fhss_activate_roundtrip(void)
{
    const ota_fhss_activate_fields_t fields = {
        .session_id = 0x12345678u,
        .target_device_id = OTA_BROADCAST_DEVICE_ID,
        .generation = 7u,
    };
    ota_fhss_activate_fields_t decoded;
    uint8_t packet[OTA_FHSS_ACTIVATE_PACKET_SIZE];

    assert(ota_protocol_encode_fhss_activate(packet, sizeof(packet), &fields) ==
           OTA_FHSS_ACTIVATE_PACKET_SIZE);
    assert(ota_protocol_decode_fhss_activate(packet, sizeof(packet), &decoded));
    assert(decoded.session_id == fields.session_id);
    assert(decoded.target_device_id == fields.target_device_id);
    assert(decoded.generation == fields.generation);
    printf("[OK] FHSS_ACTIVATE 왕복 확인\n");
}

static void test_fhss_sync_roundtrip(void)
{
    const ota_fhss_sync_fields_t fields = {
        .sync_version = OTA_FHSS_SYNC_VERSION,
        .generation = 7u,
        .sequence = 0x1234u,
        .hop_index = 42u,
        .slot_number = 0x11223344u,
    };
    ota_fhss_sync_fields_t decoded;
    uint8_t packet[OTA_FHSS_SYNC_PACKET_SIZE];

    assert(ota_protocol_encode_fhss_sync(packet, sizeof(packet), &fields) ==
           OTA_FHSS_SYNC_PACKET_SIZE);
    assert(ota_protocol_decode_fhss_sync(packet, sizeof(packet), &decoded));
    assert(decoded.sync_version == fields.sync_version);
    assert(decoded.generation == fields.generation);
    assert(decoded.sequence == fields.sequence);
    assert(decoded.hop_index == fields.hop_index);
    assert(decoded.slot_number == fields.slot_number);
    assert(packet[6] == 0x34 && packet[7] == 0x12);
    assert(packet[9] == 0x44 && packet[10] == 0x33 &&
           packet[11] == 0x22 && packet[12] == 0x11);

    packet[1] = (uint8_t)(OTA_FHSS_SYNC_VERSION + 1u);
    assert(!ota_protocol_decode_fhss_sync(packet, sizeof(packet), &decoded));
    printf("[OK] FHSS_SYNC 왕복/LE/버전 거부 확인\n");
}

static void test_fhss_sequence_golden_vector(void)
{
    static const uint8_t expected_first_20[] = {
        1, 72, 92, 28, 32, 67, 50, 53, 22, 17,
        7, 37, 68, 27, 86, 51, 46, 80, 33, 99,
    };
    uint8_t sequence[100];

    assert(ota_fhss_build_sequence(sequence, sizeof(sequence), 1u, 100u,
                                   OTA_FHSS_DEFAULT_SEED));
    assert(memcmp(sequence, expected_first_20,
                  sizeof(expected_first_20)) == 0);
    assert(sequence[0] == 1u); /* rendezvous channel은 셔플하지 않는다. */
    for (size_t i = 0u; i < sizeof(sequence); ++i) {
        assert(sequence[i] >= 1u && sequence[i] <= 100u);
        for (size_t j = 0u; j < i; ++j)
            assert(sequence[i] != sequence[j]);
    }
    printf("[OK] FHSS seed 고정 테스트 벡터와 채널 중복 없음 확인\n");
}

static void test_fhss_sequence_invalid_range(void)
{
    uint8_t sequence[100];

    assert(!ota_fhss_build_sequence(NULL, 100u, 1u, 100u, 1u));
    assert(!ota_fhss_build_sequence(sequence, 99u, 1u, 100u, 1u));
    assert(!ota_fhss_build_sequence(sequence, sizeof(sequence), 250u, 10u, 1u));
    assert(!ota_fhss_build_sequence(sequence, sizeof(sequence), 1u, 0u, 1u));
    printf("[OK] 잘못된 FHSS 채널 범위 거부\n");
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
    test_fhss_config_roundtrip();
    test_fhss_config_crc_failure();
    test_fhss_config_invalid_values();
    test_fhss_activate_roundtrip();
    test_fhss_sync_roundtrip();
    test_fhss_sequence_golden_vector();
    test_fhss_sequence_invalid_range();
    printf("\n모든 테스트 통과\n");
    return 0;
}

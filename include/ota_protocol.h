#ifndef OTA_PROTOCOL_H
#define OTA_PROTOCOL_H

/*
 * ota-protocol — firmware-esp32 <-> gateway-ota 공유 OTA 패킷 규격
 * v0.2 (2026-08-11) — RF/ESP32 담당 팀원의 plan_A.md 제안을 채택해서 다시 씀.
 * 2026-08-11 후속: version 필드 제거(아래 "왜 version이 없는지" 참고).
 *
 * v0.1(9byte, DATA/ACK/NACK 3종류)은 폐기됨. 무엇이 왜 바뀌었는지는
 * docs/design-notes-ota-protocol-es.md "v0.2로 확정" 절 참고.
 *
 * 순수 C99, 헤더 하나로 끝나는 header-only 라이브러리입니다.
 * <stdint.h>/<stddef.h>/<stdbool.h>/<string.h>만 쓰기 때문에 ESP-IDF(ESP32),
 * 데스크톱 Linux(gateway-ota, Qt/C++), 커널 모듈이 아닌 어떤 C/C++ 빌드
 * 환경에서도 그대로 컴파일됩니다. (C++에서 쓸 때는 extern "C"로 자동 처리됨)
 *
 * 이 헤더는 "wire format"(패킷을 바이트로 어떻게 담는지)만 다룹니다.
 * 재전송 전략(청크마다 Stop-and-Wait으로 할지, 배치/윈도우로 묶어서 할지)은
 * 이 패킷 포맷과 무관한 세션 레이어(`ota_client`/`gateway-ota`의 `OtaSession`)
 * 결정 사항이라 여기 포함하지 않습니다 — 어느 쪽으로 하든 같은 DATA/ACK/NACK
 * 포맷을 그대로 씁니다.
 *
 * 왜 version 필드가 없는지 (2026-08-11 결정):
 * - 원래는 모든 패킷 맨 앞에 1byte 프로토콜 버전을 뒀었는데, 매직 넘버
 *   도입을 검토하면서 "애초에 이 두 개가 막아주는 문제가 뭔지" 다시 따져봄.
 * - `OTA_START`/`OTA_DATA`/`OTA_END`/`OTA_ACK`/`OTA_NACK`는 전부 `session_id`를
 *   싣고 있고, 이미 진행 중인 세션이라면 session_id가 사실상 매직 넘버보다도
 *   더 강력한 필터 역할을 함(고정된 공개값이 아니라 세션마다 랜덤하게 생기는
 *   값이라 노이즈가 우연히 맞아떨어질 확률이 더 낮음).
 * - `OTA_DISCOVER`/`OTA_DISCOVER_ACK`는 session_id가 없는 세션 시작 이전
 *   단계라 무방비이긴 하지만, "조회"라는 낮은 위험도 작업이라 문제 없음 —
 *   노이즈를 잘못 해석해도 최악의 경우 화면에 가짜 기기가 하나 뜨는
 *   정도(재조회하면 그만)지, 실제 플래시 기록으로 이어지지 않음. CC1101
 *   하드웨어 CRC가 1차로 걸러주기도 함.
 * - 그래서 매직 넘버도, version도 둘 다 도입하지 않기로 결정. 다만 향후
 *   "정말 다른 버전끼리 붙는" 상황(v0.1 vs v0.2처럼)을 다시 만나면, 그때는
 *   session_id로도 못 막는 경우(세션 시작 전 START 패킷 자체의 오해석)라
 *   재검토 필요.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * RF 상위 계층(rf_transport)이 보장하는 패킷 바디 최대 크기.
 * CC1101 하드웨어 FIFO(64byte) 자체가 아니라, RF 담당이 실측/오버헤드
 * 고려해서 "이 값 이상은 우리 계층에 안 넘어온다"고 확정한 값 (plan_A.md 참고).
 */
#define OTA_RF_PACKET_BODY_MAX_SIZE 60u

#define OTA_DATA_HEADER_SIZE  12u
#define OTA_MAX_PAYLOAD_SIZE  48u  /* OTA_RF_PACKET_BODY_MAX_SIZE - OTA_DATA_HEADER_SIZE */
#define OTA_START_PACKET_SIZE 49u
#define OTA_END_PACKET_SIZE   13u
#define OTA_ACK_PACKET_SIZE   11u
#define OTA_DISCOVER_PACKET_SIZE     1u  /* type 뿐, 바디 없음 */
#define OTA_DISCOVER_ACK_PACKET_SIZE 7u  /* device_id(3) + fw_major/minor/patch */

/* FHSS control packets (all remain below the 60-byte RF body limit). */
#define OTA_FHSS_ALGORITHM_VERSION       1u
#define OTA_FHSS_SYNC_VERSION            1u
#define OTA_FHSS_DEFAULT_SEED            0x46485353u
#define OTA_FHSS_ZERO_SEED_FALLBACK      0x6D2B79F5u
#define OTA_FHSS_CONFIG_PACKET_SIZE      33u
#define OTA_FHSS_ACTIVATE_PACKET_SIZE    13u
#define OTA_FHSS_SYNC_PACKET_SIZE        13u

/* `sequence`/`total_chunks`는 plan_A.md 원안대로 32bit 유지(2026-08-11).
 * 한때 65,535개(16bit)면 충분해 보인다고 판단해 축소를 검토했으나, RF
 * 담당 의견으로 "일단 여유 있게 32bit로 두고, 나중에 실제로 좁혀도 되는
 * 게 확인되면 그때 줄이자"로 정리해 되돌림. 축소 검토 근거는
 * docs/design-notes-ota-protocol-es.md에 기록만 남겨둠. */

#define OTA_BROADCAST_DEVICE_ID 0xFFFFFFFFu
/* START/END(제어 패킷)에 대한 ACK/NACK의 sequence 필드는 이 값을 씀
 * ("특정 청크가 아니라 제어 패킷 자체에 대한 응답"이라는 표시) */
#define OTA_CONTROL_SEQUENCE 0xFFFFFFFFu

/* ---------- 패킷 타입 ---------- */
typedef enum {
    OTA_PKT_START        = 1,
    OTA_PKT_DATA         = 2,
    OTA_PKT_END          = 3,
    OTA_PKT_ACK          = 4,
    OTA_PKT_NACK         = 5,
    OTA_PKT_DISCOVER     = 6,  /* 조회 질의 — 게이트웨이 -> 브로드캐스트("OTA 대기중인 기기 있나?") */
    OTA_PKT_DISCOVER_ACK = 7,  /* 조회 응답 — MENU_OTA 대기중인 ESP32 각자 응답 */

    OTA_PKT_FHSS_CONFIG   = 8,  /* hopping configuration */
    OTA_PKT_FHSS_ACTIVATE = 9,  /* activate a stored generation */
    OTA_PKT_FHSS_SYNC     = 10, /* runtime slot synchronization */
} ota_packet_type_t;

/* ---------- ACK/NACK의 result_code ----------
 * (2026-08-11: version 필드를 없애면서 OTA_RESULT_INVALID_VERSION도 같이
 * 제거함 — 더 이상 검증할 version이 없으므로) */
typedef enum {
    OTA_RESULT_OK = 0,
    OTA_RESULT_INVALID_TYPE,
    OTA_RESULT_INVALID_SESSION,
    OTA_RESULT_INVALID_TARGET,
    OTA_RESULT_INVALID_SIZE,
    OTA_RESULT_INVALID_SEQUENCE,
    OTA_RESULT_INVALID_CRC,
    OTA_RESULT_WRITE_FAILED,
    OTA_RESULT_VERIFY_FAILED,
    OTA_RESULT_BUSY,
    OTA_RESULT_TIMEOUT,
} ota_result_t;

/* ---------- 패킷별 필드 (메모리 상의 표현 — 이 struct를 그대로 memcpy해서
 * 전송하지 않습니다. 항상 아래 encode/decode 함수를 거쳐 바이트 단위로
 * 조립/해석합니다) ---------- */

typedef struct {
    uint32_t session_id;
    uint32_t target_device_id;   /* OTA_BROADCAST_DEVICE_ID = 전체 브로드캐스트 */
    uint32_t image_size;
    uint32_t total_chunks;
    uint8_t  image_sha256[32];
} ota_start_fields_t;

typedef struct {
    uint32_t session_id;
    uint32_t sequence;
    uint8_t  payload_length;
    uint16_t crc16;
} ota_data_header_fields_t;

typedef struct {
    uint32_t session_id;
    uint32_t image_size;
    uint32_t total_chunks;
} ota_end_fields_t;

typedef struct {
    uint32_t session_id;
    uint8_t  acknowledged_type; /* 이 응답이 어떤 패킷 타입에 대한 것인지 (ota_packet_type_t) */
    uint32_t sequence;          /* DATA면 그 청크의 sequence, START/END면 OTA_CONTROL_SEQUENCE */
    uint8_t  result_code;       /* ota_result_t */
} ota_ack_fields_t;

/* OTA_DISCOVER는 페이로드가 없어서(브로드캐스트 질의 자체) 별도 필드 구조체가
 * 없습니다. OTA_DISCOVER_ACK만 응답 기기 정보를 싣습니다. */
typedef struct {
    uint32_t device_id;   /* 이 기기의 고유 ID. ESP32 MAC 주소(6byte)의
                            * 뒤 3byte(NIC 고유 부분)를 그대로 씀 — 앞
                            * 3byte(OUI, Organizationally Unique Identifier)는
                            * 제조사 식별용이라 같은 제조사 칩끼리는 전부
                            * 동일해서 기기 구분에 쓸모가 없음, 뒤 3byte만으로
                            * 충분히 구분 가능(2026-08-11 결정). 와이어에는
                            * 하위 3byte만 실림(0 ~ OTA_DEVICE_ID_MAX),
                            * 상위 1byte는 항상 0이어야 함 —
                            * OTA_START.target_device_id(4byte)에 그대로
                            * 넣을 때 자동으로 제로 확장(zero-extend)됨 */
    uint8_t  fw_major;
    uint8_t  fw_minor;
    uint8_t  fw_patch;
} ota_discover_ack_fields_t;

/* device_id가 와이어에 담을 수 있는 최댓값 (3byte = 16,777,215) */
/* FHSS_CONFIG sends the inputs needed to reproduce a hopping order instead
 * of sending the entire channel array. A receiver stores this as pending
 * configuration and applies it only after a matching FHSS_ACTIVATE packet. */
typedef struct {
    uint32_t session_id;
    uint32_t target_device_id;
    uint32_t generation;
    uint8_t algorithm_version;
    uint8_t channel_profile_id;
    uint8_t first_channel;
    uint8_t channel_count;
    uint8_t rendezvous_channel;
    uint8_t reserved_channel;
    uint32_t seed;
    uint32_t slot_duration_us;
    uint32_t channel_switch_guard_us;
} ota_fhss_config_fields_t;

typedef struct {
    uint32_t session_id;
    uint32_t target_device_id;
    uint32_t generation;
} ota_fhss_activate_fields_t;

typedef struct {
    uint8_t sync_version;
    uint32_t generation;
    uint16_t sequence;
    uint8_t hop_index;
    uint32_t slot_number;
} ota_fhss_sync_fields_t;

#define OTA_DEVICE_ID_MAX 0xFFFFFFu

/* ---------- Little Endian 바이트 조립/해석 ----------
 * 컴파일 플랫폼의 구조체 정렬·엔디안에 기대지 않기 위해 항상 바이트 단위로
 * 직접 조립/해석합니다 (v0.1은 #pragma pack + 구조체 memcpy에 암묵적으로
 * 의존했었는데, 지금 플랫폼(ESP32/라즈베리파이)이 우연히 둘 다 Little-Endian이라
 * 문제가 없었을 뿐이었습니다). plan_A.md가 명시한 "모든 다중 바이트 정수는
 * Little Endian" 컨벤션을 그대로 따릅니다.
 */

static inline void ota_write_u16_le(uint8_t *buf, uint16_t v)
{
    buf[0] = (uint8_t)(v & 0xFFu);
    buf[1] = (uint8_t)((v >> 8) & 0xFFu);
}

static inline void ota_write_u32_le(uint8_t *buf, uint32_t v)
{
    buf[0] = (uint8_t)(v & 0xFFu);
    buf[1] = (uint8_t)((v >> 8) & 0xFFu);
    buf[2] = (uint8_t)((v >> 16) & 0xFFu);
    buf[3] = (uint8_t)((v >> 24) & 0xFFu);
}

/* device_id 전용 3byte(24bit) Little Endian 헬퍼. u16/u32와 패턴은 동일하고
 * 바이트 하나만 적음 — OTA_DISCOVER_ACK의 device_id에만 씁니다. */
static inline void ota_write_u24_le(uint8_t *buf, uint32_t v)
{
    buf[0] = (uint8_t)(v & 0xFFu);
    buf[1] = (uint8_t)((v >> 8) & 0xFFu);
    buf[2] = (uint8_t)((v >> 16) & 0xFFu);
}

static inline uint32_t ota_read_u24_le(const uint8_t *buf)
{
    return (uint32_t)buf[0]
         | ((uint32_t)buf[1] << 8)
         | ((uint32_t)buf[2] << 16);
}

static inline uint16_t ota_read_u16_le(const uint8_t *buf)
{
    return (uint16_t)((uint16_t)buf[0] | ((uint16_t)buf[1] << 8));
}

static inline uint32_t ota_read_u32_le(const uint8_t *buf)
{
    return (uint32_t)buf[0]
         | ((uint32_t)buf[1] << 8)
         | ((uint32_t)buf[2] << 16)
         | ((uint32_t)buf[3] << 24);
}

/* ---------- CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF) ----------
 * DATA 패킷의 payload에만 적용 (CC1101 하드웨어 CRC와 별개의 애플리케이션
 * 계층 검증 — 중복이어도 비용이 거의 없어서 유지하기로 함) */
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

/* 이미지 크기로부터 총 청크 개수 계산 (plan_A.md §7과 동일한 올림 나눗셈) */
static inline uint32_t ota_protocol_total_chunks(uint32_t image_size)
{
    return (image_size + OTA_MAX_PAYLOAD_SIZE - 1u) / OTA_MAX_PAYLOAD_SIZE;
}

/*
 * 수신한 바이트열의 맨 앞 1byte(type)만 먼저 들여다봅니다.
 * 어떤 종류의 패킷인지 모를 때, 이걸로 먼저 타입만 확인한 뒤
 * 그에 맞는 ota_protocol_decode_*()를 호출하면 됩니다.
 */
static inline bool ota_protocol_peek_type(
    const uint8_t *packet, size_t packet_length,
    ota_packet_type_t *type_out)
{
    if (packet == NULL || packet_length < 1)
        return false;
    if (type_out) *type_out = (ota_packet_type_t)packet[0];
    return true;
}

/* ================= OTA_START ================= */
/* Offset 0:type 1:session_id(4) 5:target_device_id(4)
 * 9:image_size(4) 13:total_chunks(4) 17:image_sha256(32) = 49byte */

static inline size_t ota_protocol_encode_start(
    uint8_t *packet_out, size_t packet_out_capacity,
    const ota_start_fields_t *fields)
{
    if (packet_out == NULL || fields == NULL)
        return 0;
    if (packet_out_capacity < OTA_START_PACKET_SIZE)
        return 0;

    packet_out[0] = (uint8_t)OTA_PKT_START;
    ota_write_u32_le(&packet_out[1],  fields->session_id);
    ota_write_u32_le(&packet_out[5],  fields->target_device_id);
    ota_write_u32_le(&packet_out[9],  fields->image_size);
    ota_write_u32_le(&packet_out[13], fields->total_chunks);
    memcpy(&packet_out[17], fields->image_sha256, 32);

    return OTA_START_PACKET_SIZE;
}

static inline bool ota_protocol_decode_start(
    const uint8_t *packet, size_t packet_length,
    ota_start_fields_t *fields_out)
{
    if (packet == NULL || fields_out == NULL || packet_length != OTA_START_PACKET_SIZE)
        return false;
    if (packet[0] != (uint8_t)OTA_PKT_START)
        return false;

    ota_start_fields_t fields;
    fields.session_id       = ota_read_u32_le(&packet[1]);
    fields.target_device_id = ota_read_u32_le(&packet[5]);
    fields.image_size       = ota_read_u32_le(&packet[9]);
    fields.total_chunks     = ota_read_u32_le(&packet[13]);
    memcpy(fields.image_sha256, &packet[17], 32);

    *fields_out = fields; /* 검증 통과 후에만 out 파라미터에 반영 */
    return true;
}

/* ================= OTA_DATA ================= */
/* Offset 0:type 1:session_id(4) 5:sequence(4) 9:payload_length(1)
 * 10:crc16(2) 12:firmware payload(0~48) = 최대 60byte */

static inline size_t ota_protocol_encode_data(
    uint8_t *packet_out, size_t packet_out_capacity,
    uint32_t session_id, uint32_t sequence,
    const uint8_t *payload, size_t payload_length)
{
    if (packet_out == NULL || (payload == NULL && payload_length > 0))
        return 0;
    if (payload_length > OTA_MAX_PAYLOAD_SIZE)
        return 0;
    if (packet_out_capacity < OTA_DATA_HEADER_SIZE + payload_length)
        return 0;

    const uint16_t crc = (payload_length > 0)
        ? ota_protocol_crc16(payload, payload_length) : 0u;

    packet_out[0] = (uint8_t)OTA_PKT_DATA;
    ota_write_u32_le(&packet_out[1], session_id);
    ota_write_u32_le(&packet_out[5], sequence);
    packet_out[9] = (uint8_t)payload_length;
    ota_write_u16_le(&packet_out[10], crc);
    if (payload_length > 0)
        memcpy(&packet_out[OTA_DATA_HEADER_SIZE], payload, payload_length);

    return OTA_DATA_HEADER_SIZE + payload_length;
}

/*
 * 실패(길이 부족, CRC 불일치) 시 false — 이 경우 header_out/payload_out/
 * payload_length_out은 전혀 건드리지 않습니다 (검증 통과 후에만 반영).
 */
static inline bool ota_protocol_decode_data(
    const uint8_t *packet, size_t packet_length,
    ota_data_header_fields_t *header_out,
    const uint8_t **payload_out, size_t *payload_length_out)
{
    if (packet == NULL || header_out == NULL || packet_length < OTA_DATA_HEADER_SIZE)
        return false;
    if (packet[0] != (uint8_t)OTA_PKT_DATA)
        return false;

    ota_data_header_fields_t header;
    header.session_id     = ota_read_u32_le(&packet[1]);
    header.sequence       = ota_read_u32_le(&packet[5]);
    header.payload_length = packet[9];
    header.crc16          = ota_read_u16_le(&packet[10]);

    if (header.payload_length > OTA_MAX_PAYLOAD_SIZE)
        return false;
    if (packet_length < OTA_DATA_HEADER_SIZE + header.payload_length)
        return false;

    const uint8_t *payload = (header.payload_length > 0)
        ? &packet[OTA_DATA_HEADER_SIZE] : NULL;

    if (header.payload_length > 0 &&
        ota_protocol_crc16(payload, header.payload_length) != header.crc16)
        return false; /* CRC 불일치 -> 호출자가 NACK 보내야 함 */

    *header_out = header;
    if (payload_out) *payload_out = payload;
    if (payload_length_out) *payload_length_out = header.payload_length;
    return true;
}

/* ================= OTA_END ================= */
/* Offset 0:type 1:session_id(4) 5:image_size(4) 9:total_chunks(4) = 13byte */

static inline size_t ota_protocol_encode_end(
    uint8_t *packet_out, size_t packet_out_capacity,
    const ota_end_fields_t *fields)
{
    if (packet_out == NULL || fields == NULL)
        return 0;
    if (packet_out_capacity < OTA_END_PACKET_SIZE)
        return 0;

    packet_out[0] = (uint8_t)OTA_PKT_END;
    ota_write_u32_le(&packet_out[1], fields->session_id);
    ota_write_u32_le(&packet_out[5], fields->image_size);
    ota_write_u32_le(&packet_out[9], fields->total_chunks);

    return OTA_END_PACKET_SIZE;
}

static inline bool ota_protocol_decode_end(
    const uint8_t *packet, size_t packet_length,
    ota_end_fields_t *fields_out)
{
    if (packet == NULL || fields_out == NULL || packet_length != OTA_END_PACKET_SIZE)
        return false;
    if (packet[0] != (uint8_t)OTA_PKT_END)
        return false;

    ota_end_fields_t fields;
    fields.session_id   = ota_read_u32_le(&packet[1]);
    fields.image_size   = ota_read_u32_le(&packet[5]);
    fields.total_chunks = ota_read_u32_le(&packet[9]);

    *fields_out = fields;
    return true;
}

/* ================= ACK / NACK ================= */
/* Offset 0:type 1:session_id(4) 5:acknowledged_type(1)
 * 6:sequence(4) 10:result_code(1) = 11byte */

static inline size_t ota_protocol_encode_ack(
    uint8_t *packet_out, size_t packet_out_capacity,
    ota_packet_type_t type, /* OTA_PKT_ACK 또는 OTA_PKT_NACK */
    const ota_ack_fields_t *fields)
{
    if (packet_out == NULL || fields == NULL)
        return 0;
    if (type != OTA_PKT_ACK && type != OTA_PKT_NACK)
        return 0;
    if (packet_out_capacity < OTA_ACK_PACKET_SIZE)
        return 0;

    packet_out[0] = (uint8_t)type;
    ota_write_u32_le(&packet_out[1], fields->session_id);
    packet_out[5] = fields->acknowledged_type;
    ota_write_u32_le(&packet_out[6], fields->sequence);
    packet_out[10] = fields->result_code;

    return OTA_ACK_PACKET_SIZE;
}

static inline bool ota_protocol_decode_ack(
    const uint8_t *packet, size_t packet_length,
    ota_packet_type_t *type_out, ota_ack_fields_t *fields_out)
{
    if (packet == NULL || fields_out == NULL || packet_length != OTA_ACK_PACKET_SIZE)
        return false;
    if (packet[0] != (uint8_t)OTA_PKT_ACK && packet[0] != (uint8_t)OTA_PKT_NACK)
        return false;

    ota_ack_fields_t fields;
    fields.session_id        = ota_read_u32_le(&packet[1]);
    fields.acknowledged_type = packet[5];
    fields.sequence          = ota_read_u32_le(&packet[6]);
    fields.result_code       = packet[10];

    if (type_out) *type_out = (ota_packet_type_t)packet[0];
    *fields_out = fields;
    return true;
}

/* ================= DISCOVER / DISCOVER_ACK ================= */
/* 기기 조회(2026-08-11 도입) — "OTA 대기중인 무전기 목록을 보여주고 사용자가
 * 고르게 하자"는 결정에 따라 추가. session_id가 아직 없는 시점(세션 시작
 * 전)이라 START와 무관하게 독립적으로 동작합니다. 이 조회로 알아낸
 * device_id를 OTA_START의 target_device_id에 넣으면(아래 설명 참고) 이후
 * 흐름은 기존 START/DATA/END/ACK/NACK 그대로입니다(포맷 변경 없음).
 *
 * 여러 기기가 동시에 DISCOVER_ACK를 보내면 무선 충돌이 날 수 있으므로,
 * 응답 전 짧은 랜덤 지연(백오프)을 두는 것은 ESP32 쪽 구현 책임입니다
 * (이 헤더가 정하는 wire format과 무관 — docs/design-notes 참고).
 *
 * DISCOVER   — Offset 0:type = 1byte (바디 없음)
 * DISCOVER_ACK — Offset 0:type 1:device_id(3) 4:fw_major(1)
 *                5:fw_minor(1) 6:fw_patch(1) = 7byte
 *
 * device_id는 와이어(3byte, 0 ~ OTA_DEVICE_ID_MAX=16,777,215)로만 축소된
 * 것이고(2026-08-11, 원래 4byte였음), ota_discover_ack_fields_t.device_id
 * 자체는 여전히 uint32_t입니다 — ota_read_u24_le()가 상위 1byte를 이미 0으로
 * 채운 채로 돌려주기 때문에, target_device_id(uint32_t)에 대입 코드 한 줄만
 * 쓰면 자동으로 zero-extend된 것과 같은 값이 됩니다(별도 변환 불필요).
 * 화면엔 "AA-BB-CC"처럼 hex 3그룹으로 조립해서 표시. OTA_BROADCAST_DEVICE_ID
 * (0xFFFFFFFF, 4byte 전부 0xFF)와는 상위 byte가 항상 달라서 충돌 걱정 없음.
 */

static inline size_t ota_protocol_encode_discover(
    uint8_t *packet_out, size_t packet_out_capacity)
{
    if (packet_out == NULL || packet_out_capacity < OTA_DISCOVER_PACKET_SIZE)
        return 0;

    packet_out[0] = (uint8_t)OTA_PKT_DISCOVER;

    return OTA_DISCOVER_PACKET_SIZE;
}

static inline bool ota_protocol_decode_discover(
    const uint8_t *packet, size_t packet_length)
{
    if (packet == NULL || packet_length != OTA_DISCOVER_PACKET_SIZE)
        return false;
    if (packet[0] != (uint8_t)OTA_PKT_DISCOVER)
        return false;

    return true;
}

static inline size_t ota_protocol_encode_discover_ack(
    uint8_t *packet_out, size_t packet_out_capacity,
    const ota_discover_ack_fields_t *fields)
{
    if (packet_out == NULL || fields == NULL)
        return 0;
    if (packet_out_capacity < OTA_DISCOVER_ACK_PACKET_SIZE)
        return 0;
    if (fields->device_id > OTA_DEVICE_ID_MAX)
        return 0; /* 3byte(24bit)에 안 들어가는 값 — 조용히 잘리는 대신 거부 */

    packet_out[0] = (uint8_t)OTA_PKT_DISCOVER_ACK;
    ota_write_u24_le(&packet_out[1], fields->device_id);
    packet_out[4] = fields->fw_major;
    packet_out[5] = fields->fw_minor;
    packet_out[6] = fields->fw_patch;

    return OTA_DISCOVER_ACK_PACKET_SIZE;
}

static inline bool ota_protocol_decode_discover_ack(
    const uint8_t *packet, size_t packet_length,
    ota_discover_ack_fields_t *fields_out)
{
    if (packet == NULL || fields_out == NULL || packet_length != OTA_DISCOVER_ACK_PACKET_SIZE)
        return false;
    if (packet[0] != (uint8_t)OTA_PKT_DISCOVER_ACK)
        return false;

    ota_discover_ack_fields_t fields;
    fields.device_id = ota_read_u24_le(&packet[1]);
    fields.fw_major   = packet[4];
    fields.fw_minor   = packet[5];
    fields.fw_patch   = packet[6];

    *fields_out = fields;
    return true;
}

/* ================= FHSS CONFIG / ACTIVATE / SYNC ================= */

static inline bool ota_fhss_config_is_valid(
    const ota_fhss_config_fields_t *fields)
{
    uint16_t last_channel;

    if (fields == NULL ||
        fields->algorithm_version != OTA_FHSS_ALGORITHM_VERSION ||
        fields->channel_count == 0u ||
        fields->slot_duration_us == 0u ||
        fields->channel_switch_guard_us >= fields->slot_duration_us)
        return false;

    last_channel = (uint16_t)fields->first_channel +
                   (uint16_t)fields->channel_count - 1u;
    if (last_channel > 255u)
        return false;
    /* 드라이버와 ESP32 모두 호핑 순서의 첫 채널을 셔플하지 않고
     * 초기 동기 획득용 랑데부 채널로 사용한다. */
    if (fields->rendezvous_channel != fields->first_channel)
        return false;
    if (fields->reserved_channel >= fields->first_channel &&
        fields->reserved_channel <= last_channel)
        return false;

    return true;
}

/* Offset 0:type, 1:session(4), 5:target(4), 9:generation(4),
 * 13:algorithm, 14:profile, 15:first channel, 16:count,
 * 17:rendezvous, 18:reserved, 19:seed(4), 23:slot duration(4),
 * 27:switch guard(4), 31:CRC16 over bytes 0..30 = 33 bytes. */
static inline size_t ota_protocol_encode_fhss_config(
    uint8_t *packet_out, size_t packet_out_capacity,
    const ota_fhss_config_fields_t *fields)
{
    uint16_t crc;

    if (packet_out == NULL || fields == NULL ||
        packet_out_capacity < OTA_FHSS_CONFIG_PACKET_SIZE ||
        !ota_fhss_config_is_valid(fields))
        return 0;

    packet_out[0] = (uint8_t)OTA_PKT_FHSS_CONFIG;
    ota_write_u32_le(&packet_out[1], fields->session_id);
    ota_write_u32_le(&packet_out[5], fields->target_device_id);
    ota_write_u32_le(&packet_out[9], fields->generation);
    packet_out[13] = fields->algorithm_version;
    packet_out[14] = fields->channel_profile_id;
    packet_out[15] = fields->first_channel;
    packet_out[16] = fields->channel_count;
    packet_out[17] = fields->rendezvous_channel;
    packet_out[18] = fields->reserved_channel;
    ota_write_u32_le(&packet_out[19], fields->seed);
    ota_write_u32_le(&packet_out[23], fields->slot_duration_us);
    ota_write_u32_le(&packet_out[27], fields->channel_switch_guard_us);
    crc = ota_protocol_crc16(packet_out, 31u);
    ota_write_u16_le(&packet_out[31], crc);
    return OTA_FHSS_CONFIG_PACKET_SIZE;
}

static inline bool ota_protocol_decode_fhss_config(
    const uint8_t *packet, size_t packet_length,
    ota_fhss_config_fields_t *fields_out)
{
    ota_fhss_config_fields_t fields;

    if (packet == NULL || fields_out == NULL ||
        packet_length != OTA_FHSS_CONFIG_PACKET_SIZE ||
        packet[0] != (uint8_t)OTA_PKT_FHSS_CONFIG ||
        ota_read_u16_le(&packet[31]) != ota_protocol_crc16(packet, 31u))
        return false;

    fields.session_id = ota_read_u32_le(&packet[1]);
    fields.target_device_id = ota_read_u32_le(&packet[5]);
    fields.generation = ota_read_u32_le(&packet[9]);
    fields.algorithm_version = packet[13];
    fields.channel_profile_id = packet[14];
    fields.first_channel = packet[15];
    fields.channel_count = packet[16];
    fields.rendezvous_channel = packet[17];
    fields.reserved_channel = packet[18];
    fields.seed = ota_read_u32_le(&packet[19]);
    fields.slot_duration_us = ota_read_u32_le(&packet[23]);
    fields.channel_switch_guard_us = ota_read_u32_le(&packet[27]);

    if (!ota_fhss_config_is_valid(&fields))
        return false;
    *fields_out = fields;
    return true;
}

/* Offset 0:type, 1:session(4), 5:target(4), 9:generation(4). */
static inline size_t ota_protocol_encode_fhss_activate(
    uint8_t *packet_out, size_t packet_out_capacity,
    const ota_fhss_activate_fields_t *fields)
{
    if (packet_out == NULL || fields == NULL ||
        packet_out_capacity < OTA_FHSS_ACTIVATE_PACKET_SIZE)
        return 0;
    packet_out[0] = (uint8_t)OTA_PKT_FHSS_ACTIVATE;
    ota_write_u32_le(&packet_out[1], fields->session_id);
    ota_write_u32_le(&packet_out[5], fields->target_device_id);
    ota_write_u32_le(&packet_out[9], fields->generation);
    return OTA_FHSS_ACTIVATE_PACKET_SIZE;
}

static inline bool ota_protocol_decode_fhss_activate(
    const uint8_t *packet, size_t packet_length,
    ota_fhss_activate_fields_t *fields_out)
{
    ota_fhss_activate_fields_t fields;

    if (packet == NULL || fields_out == NULL ||
        packet_length != OTA_FHSS_ACTIVATE_PACKET_SIZE ||
        packet[0] != (uint8_t)OTA_PKT_FHSS_ACTIVATE)
        return false;
    fields.session_id = ota_read_u32_le(&packet[1]);
    fields.target_device_id = ota_read_u32_le(&packet[5]);
    fields.generation = ota_read_u32_le(&packet[9]);
    *fields_out = fields;
    return true;
}

/* Runtime SYNC is deliberately 13 bytes, matching the current ESP32 packet
 * budget while using this protocol's Little Endian convention.
 * Offset 0:type, 1:sync version, 2:generation(4), 6:sequence(2),
 * 8:hop index, 9:slot number(4). */
static inline size_t ota_protocol_encode_fhss_sync(
    uint8_t *packet_out, size_t packet_out_capacity,
    const ota_fhss_sync_fields_t *fields)
{
    if (packet_out == NULL || fields == NULL ||
        fields->sync_version != OTA_FHSS_SYNC_VERSION ||
        packet_out_capacity < OTA_FHSS_SYNC_PACKET_SIZE)
        return 0;
    packet_out[0] = (uint8_t)OTA_PKT_FHSS_SYNC;
    packet_out[1] = fields->sync_version;
    ota_write_u32_le(&packet_out[2], fields->generation);
    ota_write_u16_le(&packet_out[6], fields->sequence);
    packet_out[8] = fields->hop_index;
    ota_write_u32_le(&packet_out[9], fields->slot_number);
    return OTA_FHSS_SYNC_PACKET_SIZE;
}

static inline bool ota_protocol_decode_fhss_sync(
    const uint8_t *packet, size_t packet_length,
    ota_fhss_sync_fields_t *fields_out)
{
    ota_fhss_sync_fields_t fields;

    if (packet == NULL || fields_out == NULL ||
        packet_length != OTA_FHSS_SYNC_PACKET_SIZE ||
        packet[0] != (uint8_t)OTA_PKT_FHSS_SYNC ||
        packet[1] != OTA_FHSS_SYNC_VERSION)
        return false;
    fields.sync_version = packet[1];
    fields.generation = ota_read_u32_le(&packet[2]);
    fields.sequence = ota_read_u16_le(&packet[6]);
    fields.hop_index = packet[8];
    fields.slot_number = ota_read_u32_le(&packet[9]);
    *fields_out = fields;
    return true;
}

static inline uint32_t ota_fhss_xorshift32(uint32_t *state)
{
    uint32_t value = *state;
    value ^= value << 13u;
    value ^= value >> 17u;
    value ^= value << 5u;
    *state = value;
    return value;
}

/* Build the same rendezvous-first permutation used by firmware-esp32:
 * sequence[0] stays fixed and only sequence[1..count-1] is shuffled. */
static inline bool ota_fhss_build_sequence(
    uint8_t *sequence_out, size_t sequence_capacity,
    uint8_t first_channel, uint8_t channel_count, uint32_t seed)
{
    uint16_t last_channel;
    uint32_t state;
    size_t i;

    if (sequence_out == NULL || channel_count == 0u ||
        sequence_capacity < channel_count)
        return false;
    last_channel = (uint16_t)first_channel + (uint16_t)channel_count - 1u;
    if (last_channel > 255u)
        return false;

    for (i = 0u; i < channel_count; ++i)
        sequence_out[i] = (uint8_t)((uint16_t)first_channel + i);

    state = seed != 0u ? seed : OTA_FHSS_ZERO_SEED_FALLBACK;
    for (i = channel_count; i > 2u; --i) {
        size_t selected = 1u +
            (size_t)(ota_fhss_xorshift32(&state) % (uint32_t)(i - 1u));
        uint8_t temporary = sequence_out[i - 1u];
        sequence_out[i - 1u] = sequence_out[selected];
        sequence_out[selected] = temporary;
    }
    return true;
}

#ifdef __cplusplus
}
#endif

#endif /* OTA_PROTOCOL_H */

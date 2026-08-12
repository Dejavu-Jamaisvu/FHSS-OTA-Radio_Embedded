#ifndef OTA_PROTOCOL_H
#define OTA_PROTOCOL_H

/*
 * ota-protocol — firmware-esp32 <-> gateway-ota 공유 OTA 패킷 규격
 * v0.2 (2026-08-11) — RF/ESP32 담당 팀원의 plan_A.md 제안을 채택해서 다시 씀.
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
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OTA_PROTOCOL_VERSION 1u

/*
 * RF 상위 계층(rf_transport)이 보장하는 패킷 바디 최대 크기.
 * CC1101 하드웨어 FIFO(64byte) 자체가 아니라, RF 담당이 실측/오버헤드
 * 고려해서 "이 값 이상은 우리 계층에 안 넘어온다"고 확정한 값 (plan_A.md 참고).
 */
#define OTA_RF_PACKET_BODY_MAX_SIZE 60u

#define OTA_DATA_HEADER_SIZE  13u
#define OTA_MAX_PAYLOAD_SIZE  47u  /* OTA_RF_PACKET_BODY_MAX_SIZE - OTA_DATA_HEADER_SIZE */
#define OTA_START_PACKET_SIZE 50u
#define OTA_END_PACKET_SIZE   14u
#define OTA_ACK_PACKET_SIZE   12u
#define OTA_DISCOVER_PACKET_SIZE     2u  /* version+type 뿐, 바디 없음 */
#define OTA_DISCOVER_ACK_PACKET_SIZE 8u  /* device_id(3) + fw_major/minor/patch */

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
} ota_packet_type_t;

/* ---------- ACK/NACK의 result_code ---------- */
typedef enum {
    OTA_RESULT_OK = 0,
    OTA_RESULT_INVALID_VERSION,
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
    uint32_t device_id;   /* 이 기기의 고유 ID. 와이어에는 하위 3byte만 실림
                            * (0 ~ OTA_DEVICE_ID_MAX), 상위 1byte는 항상 0이어야
                            * 함 — OTA_START.target_device_id(4byte)에 그대로
                            * 넣을 때 자동으로 제로 확장(zero-extend)됨 */
    uint8_t  fw_major;
    uint8_t  fw_minor;
    uint8_t  fw_patch;
} ota_discover_ack_fields_t;

/* device_id가 와이어에 담을 수 있는 최댓값 (3byte = 16,777,215) */
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
 * 수신한 바이트열의 맨 앞 2byte(version, type)만 먼저 들여다봅니다.
 * 어떤 종류의 패킷인지 모를 때, 이걸로 먼저 타입만 확인한 뒤
 * 그에 맞는 ota_protocol_decode_*()를 호출하면 됩니다.
 */
static inline bool ota_protocol_peek_type(
    const uint8_t *packet, size_t packet_length,
    uint8_t *version_out, ota_packet_type_t *type_out)
{
    if (packet == NULL || packet_length < 2)
        return false;
    if (version_out) *version_out = packet[0];
    if (type_out) *type_out = (ota_packet_type_t)packet[1];
    return true;
}

/* ================= OTA_START ================= */
/* Offset 0:version 1:type 2:session_id(4) 6:target_device_id(4)
 * 10:image_size(4) 14:total_chunks(4) 18:image_sha256(32) = 50byte */

static inline size_t ota_protocol_encode_start(
    uint8_t *packet_out, size_t packet_out_capacity,
    const ota_start_fields_t *fields)
{
    if (packet_out == NULL || fields == NULL)
        return 0;
    if (packet_out_capacity < OTA_START_PACKET_SIZE)
        return 0;

    packet_out[0] = (uint8_t)OTA_PROTOCOL_VERSION;
    packet_out[1] = (uint8_t)OTA_PKT_START;
    ota_write_u32_le(&packet_out[2],  fields->session_id);
    ota_write_u32_le(&packet_out[6],  fields->target_device_id);
    ota_write_u32_le(&packet_out[10], fields->image_size);
    ota_write_u32_le(&packet_out[14], fields->total_chunks);
    memcpy(&packet_out[18], fields->image_sha256, 32);

    return OTA_START_PACKET_SIZE;
}

static inline bool ota_protocol_decode_start(
    const uint8_t *packet, size_t packet_length,
    ota_start_fields_t *fields_out)
{
    if (packet == NULL || fields_out == NULL || packet_length != OTA_START_PACKET_SIZE)
        return false;
    if (packet[0] != (uint8_t)OTA_PROTOCOL_VERSION || packet[1] != (uint8_t)OTA_PKT_START)
        return false;

    ota_start_fields_t fields;
    fields.session_id       = ota_read_u32_le(&packet[2]);
    fields.target_device_id = ota_read_u32_le(&packet[6]);
    fields.image_size       = ota_read_u32_le(&packet[10]);
    fields.total_chunks     = ota_read_u32_le(&packet[14]);
    memcpy(fields.image_sha256, &packet[18], 32);

    *fields_out = fields; /* 검증 통과 후에만 out 파라미터에 반영 */
    return true;
}

/* ================= OTA_DATA ================= */
/* Offset 0:version 1:type 2:session_id(4) 6:sequence(4) 10:payload_length(1)
 * 11:crc16(2) 13:firmware payload(0~47) = 최대 60byte */

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

    packet_out[0] = (uint8_t)OTA_PROTOCOL_VERSION;
    packet_out[1] = (uint8_t)OTA_PKT_DATA;
    ota_write_u32_le(&packet_out[2], session_id);
    ota_write_u32_le(&packet_out[6], sequence);
    packet_out[10] = (uint8_t)payload_length;
    ota_write_u16_le(&packet_out[11], crc);
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
    if (packet[0] != (uint8_t)OTA_PROTOCOL_VERSION || packet[1] != (uint8_t)OTA_PKT_DATA)
        return false;

    ota_data_header_fields_t header;
    header.session_id     = ota_read_u32_le(&packet[2]);
    header.sequence       = ota_read_u32_le(&packet[6]);
    header.payload_length = packet[10];
    header.crc16          = ota_read_u16_le(&packet[11]);

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
/* Offset 0:version 1:type 2:session_id(4) 6:image_size(4) 10:total_chunks(4) = 14byte */

static inline size_t ota_protocol_encode_end(
    uint8_t *packet_out, size_t packet_out_capacity,
    const ota_end_fields_t *fields)
{
    if (packet_out == NULL || fields == NULL)
        return 0;
    if (packet_out_capacity < OTA_END_PACKET_SIZE)
        return 0;

    packet_out[0] = (uint8_t)OTA_PROTOCOL_VERSION;
    packet_out[1] = (uint8_t)OTA_PKT_END;
    ota_write_u32_le(&packet_out[2],  fields->session_id);
    ota_write_u32_le(&packet_out[6],  fields->image_size);
    ota_write_u32_le(&packet_out[10], fields->total_chunks);

    return OTA_END_PACKET_SIZE;
}

static inline bool ota_protocol_decode_end(
    const uint8_t *packet, size_t packet_length,
    ota_end_fields_t *fields_out)
{
    if (packet == NULL || fields_out == NULL || packet_length != OTA_END_PACKET_SIZE)
        return false;
    if (packet[0] != (uint8_t)OTA_PROTOCOL_VERSION || packet[1] != (uint8_t)OTA_PKT_END)
        return false;

    ota_end_fields_t fields;
    fields.session_id   = ota_read_u32_le(&packet[2]);
    fields.image_size   = ota_read_u32_le(&packet[6]);
    fields.total_chunks = ota_read_u32_le(&packet[10]);

    *fields_out = fields;
    return true;
}

/* ================= ACK / NACK ================= */
/* Offset 0:version 1:type 2:session_id(4) 6:acknowledged_type(1)
 * 7:sequence(4) 11:result_code(1) = 12byte */

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

    packet_out[0] = (uint8_t)OTA_PROTOCOL_VERSION;
    packet_out[1] = (uint8_t)type;
    ota_write_u32_le(&packet_out[2], fields->session_id);
    packet_out[6] = fields->acknowledged_type;
    ota_write_u32_le(&packet_out[7], fields->sequence);
    packet_out[11] = fields->result_code;

    return OTA_ACK_PACKET_SIZE;
}

static inline bool ota_protocol_decode_ack(
    const uint8_t *packet, size_t packet_length,
    ota_packet_type_t *type_out, ota_ack_fields_t *fields_out)
{
    if (packet == NULL || fields_out == NULL || packet_length != OTA_ACK_PACKET_SIZE)
        return false;
    if (packet[0] != (uint8_t)OTA_PROTOCOL_VERSION)
        return false;
    if (packet[1] != (uint8_t)OTA_PKT_ACK && packet[1] != (uint8_t)OTA_PKT_NACK)
        return false;

    ota_ack_fields_t fields;
    fields.session_id        = ota_read_u32_le(&packet[2]);
    fields.acknowledged_type = packet[6];
    fields.sequence          = ota_read_u32_le(&packet[7]);
    fields.result_code       = packet[11];

    if (type_out) *type_out = (ota_packet_type_t)packet[1];
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
 * DISCOVER   — Offset 0:version 1:type = 2byte (바디 없음)
 * DISCOVER_ACK — Offset 0:version 1:type 2:device_id(3) 5:fw_major(1)
 *                6:fw_minor(1) 7:fw_patch(1) = 8byte
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

    packet_out[0] = (uint8_t)OTA_PROTOCOL_VERSION;
    packet_out[1] = (uint8_t)OTA_PKT_DISCOVER;

    return OTA_DISCOVER_PACKET_SIZE;
}

static inline bool ota_protocol_decode_discover(
    const uint8_t *packet, size_t packet_length)
{
    if (packet == NULL || packet_length != OTA_DISCOVER_PACKET_SIZE)
        return false;
    if (packet[0] != (uint8_t)OTA_PROTOCOL_VERSION || packet[1] != (uint8_t)OTA_PKT_DISCOVER)
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

    packet_out[0] = (uint8_t)OTA_PROTOCOL_VERSION;
    packet_out[1] = (uint8_t)OTA_PKT_DISCOVER_ACK;
    ota_write_u24_le(&packet_out[2], fields->device_id);
    packet_out[5] = fields->fw_major;
    packet_out[6] = fields->fw_minor;
    packet_out[7] = fields->fw_patch;

    return OTA_DISCOVER_ACK_PACKET_SIZE;
}

static inline bool ota_protocol_decode_discover_ack(
    const uint8_t *packet, size_t packet_length,
    ota_discover_ack_fields_t *fields_out)
{
    if (packet == NULL || fields_out == NULL || packet_length != OTA_DISCOVER_ACK_PACKET_SIZE)
        return false;
    if (packet[0] != (uint8_t)OTA_PROTOCOL_VERSION || packet[1] != (uint8_t)OTA_PKT_DISCOVER_ACK)
        return false;

    ota_discover_ack_fields_t fields;
    fields.device_id = ota_read_u24_le(&packet[2]);
    fields.fw_major   = packet[5];
    fields.fw_minor   = packet[6];
    fields.fw_patch   = packet[7];

    *fields_out = fields;
    return true;
}

#ifdef __cplusplus
}
#endif

#endif /* OTA_PROTOCOL_H */

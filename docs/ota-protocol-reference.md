# `ota_protocol.h` 전체 패킷 레퍼런스

> `include/ota_protocol.h`(v0.2)에 정의된 패킷 7종 전체를 한 문서에 정리했습니다.
> 개별 패킷 요약은 `README.md`, DISCOVER 관련 배경은 `plan-a-adoption-summary.md`/
> `notion-discover-packet.md` 참고 — 이 문서는 "패킷 구조를 통째로 훑어볼 때" 보는
> 용도입니다.

## 1. 개요

- 순수 C99, header-only(`static inline` 함수만) — `firmware-esp32`(ESP-IDF)와
  `gateway-ota`(Qt/C++, Linux) 양쪽에서 같은 헤더 하나를 그대로 씁니다.
- 이 헤더는 **wire format**(패킷을 바이트로 어떻게 담을지)만 다룹니다. 재전송
  전략(Stop-and-Wait이냐 배치냐)은 세션 레이어(`gateway-ota`의 `OtaSession`)
  책임이라 이 헤더와 무관합니다 — 무슨 전략을 쓰든 같은 DATA/ACK/NACK 포맷을
  그대로 씁니다.
- RF 상위 계층(`rf_transport`)이 보장하는 패킷 바디 최대 크기: **60byte**
  (`OTA_RF_PACKET_BODY_MAX_SIZE`, RF 담당 실측 확정값).

## 2. 공통 규칙

| 규칙 | 내용 |
|---|---|
| 바이트 순서 | **Little Endian** 명시. 구조체를 그대로 `memcpy`하지 않고 `ota_write_u16/u24/u32_le()`로 바이트 단위 직접 조립(컴파일 플랫폼의 정렬·엔디안에 안 흔들리도록) |
| 프로토콜 버전 | 모든 패킷의 0번째 byte = `OTA_PROTOCOL_VERSION`(현재 1). 버전이 안 맞으면 decode 실패 |
| 패킷 타입 | 모든 패킷의 1번째 byte = `ota_packet_type_t` 값. `ota_protocol_peek_type()`으로 이 2byte만 먼저 읽어서 어떤 decode 함수를 호출할지 판단 |
| CRC | `OTA_DATA`의 payload에만 CRC-16/CCITT-FALSE(poly `0x1021`, init `0xFFFF`) 적용. CC1101 하드웨어 CRC와 별개의 애플리케이션 계층 검증(비용 거의 없어서 유지) |
| decode 실패 시 동작 | 모든 `decode_*` 함수는 **검증 통과 후에만 out 파라미터에 값을 씁니다** — 실패 시 호출자가 out 파라미터를 실수로 읽어도 오염된 값이 안 섞임 |

## 3. 패킷 타입 전체 (`ota_packet_type_t`)

| 값 | 이름 | 크기 | 방향 | 용도 |
|---|---|---|---|---|
| 1 | `OTA_PKT_START` | 50byte | 게이트웨이 → 무전기 | 세션 시작 |
| 2 | `OTA_PKT_DATA` | 13byte 헤더 + 최대 47byte payload | 게이트웨이 → 무전기 | 펌웨어 청크 |
| 3 | `OTA_PKT_END` | 14byte | 게이트웨이 → 무전기 | 세션 종료 |
| 4 | `OTA_PKT_ACK` | 12byte | 무전기 → 게이트웨이 | 성공 응답 |
| 5 | `OTA_PKT_NACK` | 12byte | 무전기 → 게이트웨이 | 실패 응답 (`result_code` 포함) |
| 6 | `OTA_PKT_DISCOVER` | 2byte | 게이트웨이 → 브로드캐스트 | 대상 기기 조회 질의 (2026-08-11 추가) |
| 7 | `OTA_PKT_DISCOVER_ACK` | 8byte | 무전기 → 게이트웨이 | 조회 응답 (2026-08-11 추가) |

## 4. 세션 흐름

```
게이트웨이                              무전기(들)
    │──── OTA_DISCOVER (브로드캐스트) ────▶│  (MENU_OTA 대기중인 전부)
    │◀─── OTA_DISCOVER_ACK (각자 응답) ────│
    │  (사용자가 목록에서 대상 선택)
    │──── OTA_START (선택된 target_device_id) ─▶│
    │◀──────────── ACK/NACK ───────────────│
    │──── OTA_DATA × N (세션 레이어가 순서/배치 결정) ─▶│
    │◀──────────── ACK/NACK (청크별) ───────│
    │──── OTA_END ──────────────────────▶│
    │◀──────────── ACK/NACK ───────────────│  (SHA256 검증 결과)
```

`OTA_DISCOVER`/`OTA_DISCOVER_ACK`는 `session_id`가 없는 세션 시작 **이전**
단계입니다. 조회로 알아낸 `device_id`를 `OTA_START.target_device_id`에 넣으면
그 뒤로는 완전히 독립적인 흐름(START→DATA×N→END, 전부 `session_id`로 묶임)이
이어집니다.

## 5. 패킷별 상세

### 5.1 `OTA_DISCOVER` — 2byte (바디 없음)

| Offset | Size | Field |
|---:|---:|---|
| 0 | 1 | version |
| 1 | 1 | type (`OTA_PKT_DISCOVER` = 6) |

별도 필드 구조체 없음(실을 내용이 없어서) — `ota_protocol_encode_discover()`는
인자 없이 패킷만 생성하고, `ota_protocol_decode_discover()`는 유효성만 bool로
반환합니다.

### 5.2 `OTA_DISCOVER_ACK` — 8byte

| Offset | Size | Field |
|---:|---:|---|
| 0 | 1 | version |
| 1 | 1 | type (`OTA_PKT_DISCOVER_ACK` = 7) |
| 2 | 3 | device_id (0 ~ `OTA_DEVICE_ID_MAX`=16,777,215) |
| 5 | 1 | fw_major |
| 6 | 1 | fw_minor |
| 7 | 1 | fw_patch |

`device_id`는 와이어상 3byte지만 C 구조체(`ota_discover_ack_fields_t.device_id`)는
`uint32_t`라 상위 byte가 항상 0으로 채워진 채 나옵니다 — `OTA_START.target_device_id`에
변환 없이 그대로 대입 가능. `OTA_DEVICE_ID_MAX`를 넘는 값은 인코딩이 거부됩니다.

### 5.3 `OTA_START` — 50byte

| Offset | Size | Field |
|---:|---:|---|
| 0 | 1 | version |
| 1 | 1 | type (`OTA_PKT_START` = 1) |
| 2 | 4 | session_id |
| 6 | 4 | target_device_id (`OTA_BROADCAST_DEVICE_ID`=`0xFFFFFFFF`이면 전체 브로드캐스트) |
| 10 | 4 | image_size |
| 14 | 4 | total_chunks (`ota_protocol_total_chunks()`로 계산, 올림 나눗셈) |
| 18 | 32 | image_sha256 |

### 5.4 `OTA_DATA` — 13byte 헤더 + 최대 47byte payload (최대 60byte)

| Offset | Size | Field |
|---:|---:|---|
| 0 | 1 | version |
| 1 | 1 | type (`OTA_PKT_DATA` = 2) |
| 2 | 4 | session_id |
| 6 | 4 | sequence |
| 10 | 1 | payload_length |
| 11 | 2 | crc16 (payload에 대한 CRC-16/CCITT-FALSE) |
| 13 | 0~47 | firmware payload |

### 5.5 `OTA_END` — 14byte

| Offset | Size | Field |
|---:|---:|---|
| 0 | 1 | version |
| 1 | 1 | type (`OTA_PKT_END` = 3) |
| 2 | 4 | session_id |
| 6 | 4 | image_size (START 값과 대조용) |
| 10 | 4 | total_chunks (START 값과 대조용) |

### 5.6 `OTA_ACK` / `OTA_NACK` — 12byte (포맷 공용)

| Offset | Size | Field |
|---:|---:|---|
| 0 | 1 | version |
| 1 | 1 | type (`OTA_PKT_ACK`=4 또는 `OTA_PKT_NACK`=5) |
| 2 | 4 | session_id |
| 6 | 1 | acknowledged_type (이 응답이 어떤 패킷에 대한 것인지) |
| 7 | 4 | sequence (DATA면 청크 sequence, START/END면 `OTA_CONTROL_SEQUENCE`=`0xFFFFFFFF`) |
| 11 | 1 | result_code (`ota_result_t`) |

## 6. `result_code` (`ota_result_t`) — NACK에 실리는 실패 사유

| 값 | 이름 | 의미 |
|---:|---|---|
| 0 | `OTA_RESULT_OK` | 성공 |
| 1 | `OTA_RESULT_INVALID_VERSION` | 프로토콜 버전 불일치 |
| 2 | `OTA_RESULT_INVALID_TYPE` | 알 수 없는 패킷 타입 |
| 3 | `OTA_RESULT_INVALID_SESSION` | session_id 불일치 |
| 4 | `OTA_RESULT_INVALID_TARGET` | target_device_id 불일치 |
| 5 | `OTA_RESULT_INVALID_SIZE` | image_size 오류(파티션 크기 초과 등) |
| 6 | `OTA_RESULT_INVALID_SEQUENCE` | sequence 오류(누락/역순) |
| 7 | `OTA_RESULT_INVALID_CRC` | payload CRC 불일치 |
| 8 | `OTA_RESULT_WRITE_FAILED` | 플래시 쓰기 실패 |
| 9 | `OTA_RESULT_VERIFY_FAILED` | SHA256 검증 실패 |
| 10 | `OTA_RESULT_BUSY` | 수신측이 처리 중(backpressure) |
| 11 | `OTA_RESULT_TIMEOUT` | 타임아웃 |

## 7. 제공 함수 (API) 전체

| 함수 | 역할 |
|---|---|
| `ota_protocol_peek_type()` | 맨 앞 2byte(version, type)만 먼저 확인 |
| `ota_protocol_encode_discover()` / `decode_discover()` | `OTA_DISCOVER` |
| `ota_protocol_encode_discover_ack()` / `decode_discover_ack()` | `OTA_DISCOVER_ACK` |
| `ota_protocol_encode_start()` / `decode_start()` | `OTA_START` |
| `ota_protocol_encode_data()` / `decode_data()` | `OTA_DATA` (+ payload CRC16 검증) |
| `ota_protocol_encode_end()` / `decode_end()` | `OTA_END` |
| `ota_protocol_encode_ack()` / `decode_ack()` | `OTA_ACK`/`OTA_NACK` 공용 |
| `ota_protocol_total_chunks()` | image_size로 총 청크 개수 계산 |
| `ota_protocol_crc16()` | CRC-16/CCITT-FALSE 계산 |
| `ota_write_u16/u24/u32_le()` / `ota_read_u16/u24/u32_le()` | Little Endian 직렬화/역직렬화 헬퍼 |

## 8. 아직 팀 합의가 필요한 항목

- [ ] `OTA_PKT_DISCOVER`/`OTA_PKT_DISCOVER_ACK` 타입값(6, 7) 충돌 확인 — ESP32/RF 담당
- [ ] `DISCOVER_ACK` 랜덤 백오프 범위(무선 충돌 회피) — RF 담당
- [ ] 매직 넘버 도입 여부
- [ ] `seq`/`total_chunks` 32bit 확정 (v0.1은 16bit)

## 참고 문서

- 전체 제안 원문: [`plan_A.md`](../plan_A.md)
- 실제 헤더: [`include/ota_protocol.h`](../include/ota_protocol.h)
- 테스트: [`tests/test_ota_protocol.c`](../tests/test_ota_protocol.c)

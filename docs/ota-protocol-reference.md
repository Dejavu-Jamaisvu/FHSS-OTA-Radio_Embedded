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
| 패킷 타입 | 모든 패킷의 0번째 byte = `ota_packet_type_t` 값. `ota_protocol_peek_type()`으로 이 1byte만 먼저 읽어서 어떤 decode 함수를 호출할지 판단 |
| CRC | `OTA_DATA`의 payload에만 CRC-16/CCITT-FALSE(poly `0x1021`, init `0xFFFF`) 적용. CC1101 하드웨어 CRC와 별개의 애플리케이션 계층 검증(비용 거의 없어서 유지) |
| decode 실패 시 동작 | 모든 `decode_*` 함수는 **검증 통과 후에만 out 파라미터에 값을 씁니다** — 실패 시 호출자가 out 파라미터를 실수로 읽어도 오염된 값이 안 섞임 |

> **2026-08-11: `version` 필드 제거.** 원래 모든 패킷 맨 앞에 1byte 프로토콜
> 버전이 있었지만, "이 필드가 실제로 뭘 막아주는지" 다시 따져본 결과 없애기로
> 결정했습니다. START/DATA/END/ACK/NACK는 이미 `session_id`(세션마다 랜덤값)를
> 갖고 있어 고정값인 버전/매직 넘버보다 오히려 더 강한 노이즈 필터가 되고,
> DISCOVER/DISCOVER_ACK는 session_id가 없어 무방비이긴 하지만 "조회"라는
> 낮은 위험도 작업이라(최악의 경우도 재조회하면 그만, 플래시 기록으로
> 이어지지 않음) 문제되지 않는다고 판단했습니다. 게다가 v0.1→v0.2로 wire
> format이 크게 바뀔 때도 버전 값이 한 번도 안 바뀐 걸 git log로 확인해서,
> 보호 기능이 실제로 작동한 적이 없었다는 것도 확인했습니다. 아래 모든
> 패킷 크기가 1byte씩 줄었고, `ota_result_t`에서 `OTA_RESULT_INVALID_VERSION`도
> 제거됐습니다.

## 3. 패킷 타입 전체 (`ota_packet_type_t`)

| 값 | 이름 | 크기 | 방향 | 용도 |
|---|---|---|---|---|
| 1 | `OTA_PKT_START` | 49byte | 게이트웨이 → 무전기 | 세션 시작 |
| 2 | `OTA_PKT_DATA` | 12byte 헤더 + 최대 48byte payload | 게이트웨이 → 무전기 | 펌웨어 청크 |
| 3 | `OTA_PKT_END` | 13byte | 게이트웨이 → 무전기 | 세션 종료 |
| 4 | `OTA_PKT_ACK` | 11byte | 무전기 → 게이트웨이 | 성공 응답 |
| 5 | `OTA_PKT_NACK` | 11byte | 무전기 → 게이트웨이 | 실패 응답 (`result_code` 포함) |
| 6 | `OTA_PKT_DISCOVER` | 1byte | 게이트웨이 → 브로드캐스트 | 대상 기기 조회 질의 (2026-08-11 추가) |
| 7 | `OTA_PKT_DISCOVER_ACK` | 7byte | 무전기 → 게이트웨이 | 조회 응답 (2026-08-11 추가) |

> `seq`/`total_chunks`를 16bit로 줄이는 방안을 검토했었으나(65,535개면
> ESP32 펌웨어 이미지 크기 대비 충분해 보인다는 판단), RF 담당 의견으로
> "일단 여유 있게 32bit로 두고, 나중에 실제로 좁혀도 되는 게 확인되면 그때
> 줄이자"로 정리되어 `plan_A.md` 원안 그대로 32bit 유지로 되돌림
> (2026-08-11). §8 참고.

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

### 5.1 `OTA_DISCOVER` — 1byte (바디 없음)

| Offset | Size | Field |
|---:|---:|---|
| 0 | 1 | type (`OTA_PKT_DISCOVER` = 6) |

별도 필드 구조체 없음(실을 내용이 없어서) — `ota_protocol_encode_discover()`는
인자 없이 패킷만 생성하고, `ota_protocol_decode_discover()`는 유효성만 bool로
반환합니다.

### 5.2 `OTA_DISCOVER_ACK` — 7byte

| Offset | Size | Field |
|---:|---:|---|
| 0 | 1 | type (`OTA_PKT_DISCOVER_ACK` = 7) |
| 1 | 3 | device_id (0 ~ `OTA_DEVICE_ID_MAX`=16,777,215) |
| 4 | 1 | fw_major |
| 5 | 1 | fw_minor |
| 6 | 1 | fw_patch |

`device_id`는 ESP32 MAC 주소(6byte)의 **뒤 3byte**(NIC 고유 부분)를 그대로
씁니다 — 앞 3byte(OUI, Organizationally Unique Identifier, 제조사 식별용)는
같은 제조사 칩끼리 전부 동일해서 기기 구분에 쓸모가 없기 때문입니다
(2026-08-11 결정). 와이어상 3byte지만 C 구조체
(`ota_discover_ack_fields_t.device_id`)는 `uint32_t`라 상위 byte가 항상
0으로 채워진 채 나옵니다 — `OTA_START.target_device_id`에 변환 없이 그대로
대입 가능. `OTA_DEVICE_ID_MAX`를 넘는 값은 인코딩이 거부됩니다.

### 5.3 `OTA_START` — 49byte

| Offset | Size | Field |
|---:|---:|---|
| 0 | 1 | type (`OTA_PKT_START` = 1) |
| 1 | 4 | session_id |
| 5 | 4 | target_device_id (`OTA_BROADCAST_DEVICE_ID`=`0xFFFFFFFF`이면 전체 브로드캐스트) |
| 9 | 4 | image_size |
| 13 | 4 | total_chunks (`ota_protocol_total_chunks()`로 계산, 올림 나눗셈) |
| 17 | 32 | image_sha256 |

### 5.4 `OTA_DATA` — 12byte 헤더 + 최대 48byte payload (최대 60byte)

| Offset | Size | Field |
|---:|---:|---|
| 0 | 1 | type (`OTA_PKT_DATA` = 2) |
| 1 | 4 | session_id |
| 5 | 4 | sequence |
| 9 | 1 | payload_length |
| 10 | 2 | crc16 (payload에 대한 CRC-16/CCITT-FALSE) |
| 12 | 0~48 | firmware payload |

### 5.5 `OTA_END` — 13byte

| Offset | Size | Field |
|---:|---:|---|
| 0 | 1 | type (`OTA_PKT_END` = 3) |
| 1 | 4 | session_id |
| 5 | 4 | image_size (START 값과 대조용) |
| 9 | 4 | total_chunks (START 값과 대조용) |

### 5.6 `OTA_ACK` / `OTA_NACK` — 11byte (포맷 공용)

| Offset | Size | Field |
|---:|---:|---|
| 0 | 1 | type (`OTA_PKT_ACK`=4 또는 `OTA_PKT_NACK`=5) |
| 1 | 4 | session_id |
| 5 | 1 | acknowledged_type (이 응답이 어떤 패킷에 대한 것인지) |
| 6 | 4 | sequence (DATA면 청크 sequence, START/END면 `OTA_CONTROL_SEQUENCE`=`0xFFFFFFFF`) |
| 10 | 1 | result_code (`ota_result_t`) |

## 6. `result_code` (`ota_result_t`) — NACK에 실리는 실패 사유

| 값 | 이름 | 의미 |
|---:|---|---|
| 0 | `OTA_RESULT_OK` | 성공 |
| 1 | `OTA_RESULT_INVALID_TYPE` | 알 수 없는 패킷 타입 |
| 2 | `OTA_RESULT_INVALID_SESSION` | session_id 불일치 |
| 3 | `OTA_RESULT_INVALID_TARGET` | target_device_id 불일치 |
| 4 | `OTA_RESULT_INVALID_SIZE` | image_size 오류(파티션 크기 초과 등) |
| 5 | `OTA_RESULT_INVALID_SEQUENCE` | sequence 오류(누락/역순) |
| 6 | `OTA_RESULT_INVALID_CRC` | payload CRC 불일치 |
| 7 | `OTA_RESULT_WRITE_FAILED` | 플래시 쓰기 실패 |
| 8 | `OTA_RESULT_VERIFY_FAILED` | SHA256 검증 실패 |
| 9 | `OTA_RESULT_BUSY` | 수신측이 처리 중(backpressure) |
| 10 | `OTA_RESULT_TIMEOUT` | 타임아웃 |

> 2026-08-11: `OTA_RESULT_INVALID_VERSION`(구 값 1)을 제거하면서 이후 값이
> 전부 하나씩 당겨졌습니다. 이 result_code는 NACK 패킷의 바이트 값으로도
> 나가므로, ESP32 쪽 구현도 이 번호 체계로 맞춰야 합니다(팀 공유 필요).

## 7. 제공 함수 (API) 전체

| 함수 | 역할 |
|---|---|
| `ota_protocol_peek_type()` | 맨 앞 1byte(type)만 먼저 확인 |
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

- [ ] `DISCOVER_ACK` 랜덤 백오프 범위(무선 충돌 회피) — RF 담당
- [ ] `seq`/`total_chunks` 32bit 확정 (v0.1은 16bit) — 16bit 축소를 검토했다가 "일단 32bit로 두고 나중에 줄이자"로 합의(2026-08-11).
      나중에 실측 후 다시 논의
- [x] ~~매직 넘버 도입 여부~~ — 2026-08-11 결정: 도입 안 함 (`version` 필드도 함께 제거)
- [x] ~~`OTA_PKT_DISCOVER`/`OTA_PKT_DISCOVER_ACK` 타입값(6, 7) 충돌 확인~~ —
      공유 단일 헤더(`ota_protocol.h`) 구조라 애초에 충돌 가능성 없음(version
      제거와 같은 이유)

## 참고 문서

- 전체 제안 원문: [`plan_A.md`](../plan_A.md)
- 실제 헤더: [`include/ota_protocol.h`](../include/ota_protocol.h)
- 테스트: [`tests/test_ota_protocol.c`](../tests/test_ota_protocol.c)

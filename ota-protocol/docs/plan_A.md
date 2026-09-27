# Plan A — CC1101 기반 A/B OTA 프로토콜 및 구현 계획

> 상태: Draft
> 공유 대상: `firmware-esp32`, `gateway-ota`, RF 담당
> RF 상위 계층 최대 packet body: **60 bytes (확정)**
> 제안자: 팀원 (RF/ESP32 쪽) — 2026-08-11 공유받음

## 1. 목적

라즈베리파이 게이트웨이가 CC1101을 통해 ESP32 무전기로 펌웨어를 전송하고, ESP32가 수신 이미지를 비활성 OTA 파티션에 스트리밍 기록한 뒤 새 이미지로 부팅하도록 구현한다.

```text
Raspberry Pi
  → CC1101
  → rf_transport
  → ota_protocol
  → ota_client
  → esp_ota_write()
  → 비활성 ota_0 또는 ota_1
```

전체 이미지를 RAM이나 별도 `storage` 파티션에 저장하지 않는다.

## 2. 계층별 책임

### RF 계층
- CC1101 초기화 및 송수신
- RX FIFO를 SPI로 읽어 ESP32 RAM으로 복사
- Length, RSSI, LQI, hardware CRC 등 CC1101 전용 정보 처리
- 상위 계층에 최대 60-byte packet body와 실제 길이 전달
- OTA 패킷 구조는 해석하지 않음

### OTA protocol
- START/DATA/END/ACK/NACK wire format 정의
- Little Endian 직렬화 및 역직렬화
- 패킷 길이, 버전, 타입, CRC 검증
- ESP32와 Raspberry Pi가 같은 공용 규격 사용

### ota_client
- OTA 세션 상태 관리
- sequence, 중복, 누락, timeout 처리
- 비활성 OTA 파티션 선택
- `esp_ota_begin/write/end` 호출
- 전체 이미지 SHA-256 검증
- 다음 부팅 파티션 선택

## 3. 기본 상수

```c
#define OTA_PROTOCOL_VERSION          1U
#define RF_PACKET_BODY_MAX_SIZE       60U
#define OTA_DATA_HEADER_SIZE          13U
#define OTA_DATA_MAX_PAYLOAD_SIZE     47U
#define OTA_FLASH_BUFFER_SIZE         4096U
```

```text
RF packet body       60 bytes
OTA_DATA header      13 bytes
Firmware payload     47 bytes
```

CC1101의 Length byte와 APPEND_STATUS 바이트는 RF 계층이 처리하며, 위 60바이트에는 포함하지 않는다.

## 4. 공통 Wire 규칙

- 모든 다중 바이트 정수는 Little Endian
- C 구조체를 그대로 `memcpy`하여 전송하지 않음
- encode/decode 함수가 바이트 단위로 직렬화
- `session_id`, `sequence`, `image_size`, `total_chunks`는 `uint32_t`
- payload CRC는 CRC-16/CCITT-FALSE
- 전체 이미지 검증은 SHA-256

CRC-16/CCITT-FALSE:
```text
Polynomial  0x1021
Initial     0xFFFF
RefIn       false
RefOut      false
XorOut      0x0000
범위        OTA_DATA firmware payload
```

## 5. 패킷 타입

```c
typedef enum {
    OTA_PACKET_START = 1,
    OTA_PACKET_DATA,
    OTA_PACKET_END,
    OTA_PACKET_ACK,
    OTA_PACKET_NACK,
} ota_packet_type_t;
```

## 6. OTA_START

OTA 세션 시작 시 한 번 전송한다.

| Offset | Size | Field |
|---:|---:|---|
| 0 | 1 | protocol_version |
| 1 | 1 | packet_type = START |
| 2 | 4 | session_id |
| 6 | 4 | target_device_id |
| 10 | 4 | image_size |
| 14 | 4 | total_chunks |
| 18 | 32 | image_sha256 |

총 50바이트.

```c
#define OTA_BROADCAST_DEVICE_ID 0xFFFFFFFFU
```

ESP32는 START 수신 시 다음을 수행한다.
1. version과 target 확인
2. 세션 중복 및 busy 여부 확인
3. `image_size <= update_partition->size` 확인
4. `total_chunks` 계산값 확인
5. 비활성 OTA 파티션 선택
6. `esp_ota_begin()` 호출
7. 내부 `ota_session_t` 초기화
8. START ACK 전송

## 7. OTA_DATA

| Offset | Size | Field |
|---:|---:|---|
| 0 | 1 | protocol_version |
| 1 | 1 | packet_type = DATA |
| 2 | 4 | session_id |
| 6 | 4 | sequence |
| 10 | 1 | payload_length |
| 11 | 2 | payload_crc16 |
| 13 | 0~47 | firmware payload |

최대 60바이트.

처리 규칙:
```text
sequence == expected_sequence
  → 길이/CRC 확인
  → flash buffer에 적재
  → received_bytes 증가
  → expected_sequence 증가
  → ACK
sequence < expected_sequence
  → 중복 패킷
  → ACK 재전송
sequence > expected_sequence
  → 누락 발생
  → expected_sequence를 NACK
```

청크 계산:
```c
total_chunks =
    (image_size + OTA_DATA_MAX_PAYLOAD_SIZE - 1U)
    / OTA_DATA_MAX_PAYLOAD_SIZE;
```

## 8. ESP32 Flash Buffer

ESP32는 정상 DATA payload를 4096-byte SRAM buffer에 순서대로 누적한다.

```text
47-byte DATA payload 반복 수신
  → 4096-byte buffer 누적
  → buffer가 가득 차면 esp_ota_write(4096)
  → 마지막에는 실제 남은 길이만 기록
```

Payload가 buffer 경계를 걸치면 남은 공간까지 복사하고 기록한 뒤, 나머지를 새 buffer에 복사한다.

RF 수신 경로에서는 플래시를 직접 쓰지 않는다.

```text
CC1101 GDO/RX
  → rf_transport가 FIFO를 즉시 읽음
  → ESP32 RAM 큐에 복사
  → OTA 전용 Task가 큐에서 처리
  → CRC 및 flash write
```

## 9. OTA_END

| Offset | Size | Field |
|---:|---:|---|
| 0 | 1 | protocol_version |
| 1 | 1 | packet_type = END |
| 2 | 4 | session_id |
| 6 | 4 | image_size |
| 10 | 4 | total_chunks |

총 14바이트.

END 수신 시 다음을 검증한다.
- session_id 일치
- `received_bytes == image_size`
- `expected_sequence == total_chunks`
- END의 크기/청크 수가 START와 일치

성공 처리:
1. 남은 flash buffer 기록
2. `esp_ota_end()`
3. 기록 이미지 SHA-256 계산 및 START 값과 비교
4. `esp_ota_set_boot_partition()`
5. END ACK 송신
6. ACK 송신 완료 후 재부팅

실패 시 `esp_ota_abort()`하고 현재 부팅 파티션을 유지한다.

## 10. ACK/NACK

| Offset | Size | Field |
|---:|---:|---|
| 0 | 1 | protocol_version |
| 1 | 1 | packet_type = ACK/NACK |
| 2 | 4 | session_id |
| 6 | 1 | acknowledged_type |
| 7 | 4 | sequence |
| 11 | 1 | result_code |

총 12바이트.

START/END 제어 응답은 다음 sequence를 사용한다.
```c
#define OTA_CONTROL_SEQUENCE 0xFFFFFFFFU
```

```c
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
```

## 11. 초기 전송 정책

1차 구현은 단일 단말 Stop-and-Wait 방식으로 한다.

```text
Pi: DATA #0 송신
ESP: 검증 및 수락
ESP: ACK #0
Pi: ACK 확인 후 DATA #1 송신
```

- ACK timeout 시 같은 sequence 재전송
- ESP32는 중복 DATA를 다시 기록하지 않고 ACK만 재전송
- NACK에는 ESP32가 기대하는 sequence를 기록
- FIFO 및 ESP32 큐 overflow를 우선 방지

## 12. 브로드캐스트 확장

여러 단말이 동시에 ACK하면 충돌할 수 있으므로 1차 범위에서는 단일 단말 OTA를 먼저 완성한다.

2차 범위:
1. Gateway가 DATA를 브로드캐스트
2. 단말이 누락 sequence를 기록
3. device_id 기반 응답 슬롯에서 NACK
4. Gateway가 누락 DATA 재전송
5. 단말별 최종 결과 확인

## 13. A/B 파티션 정책

제품에서 필요한 OTA 파티션:
```text
otadata
ota_0
ota_1
```

`factory`와 테스트용 `storage` 파티션은 제품 OTA에서 사용하지 않는다.

```text
최초 출하: ota_0 실행
1차 OTA: ota_1 기록 및 부팅
2차 OTA: ota_0 기록 및 부팅
```

START의 이미지 크기는 런타임에 비활성 OTA 파티션 크기와 비교한다.

## 14. Rollback

제품 설정:
```text
CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y
```

새 이미지 초기화 성공:
```c
esp_ota_mark_app_valid_cancel_rollback();
```

초기화 실패:
```c
esp_ota_mark_app_invalid_rollback_and_reboot();
```

## 15. ota-protocol 저장소 산출물

```text
ota-protocol/
├── include/
│   └── ota_protocol.h
├── tests/
│   ├── test_ota_protocol.c
│   └── test_vectors.h
├── README.md
└── plan_A.md
```

`ota_protocol.h` 범위:
- 상수 및 enum
- START/DATA/END/ACK/NACK encode
- 공통 decode
- CRC16
- Little Endian read/write
- 길이 및 필드 검증

ESP32와 Raspberry Pi는 동일한 공용 헤더와 test vector를 사용한다.

## 16. 구현 순서

### Phase 1 — 공용 프로토콜
- `ota_protocol.h` 작성
- 모든 패킷 encode/decode 구현
- CRC test vector 작성
- 최대/최소 길이와 오류 단위 테스트

### Phase 2 — ESP32 ota_client
- `ota_client_submit_packet()` 구현
- START/DATA/END dispatch
- 세션과 timeout 처리
- ACK/NACK callback
- 4096-byte flash buffer

### Phase 3 — RF 연결
- rf_transport RX callback 연결
- FIFO → RAM 큐
- ACK/NACK 송신
- FSM의 OTA_RECEIVING/OTA_APPLYING 연결

### Phase 4 — Raspberry Pi
- 이미지 SHA-256 및 session 생성
- 47-byte 단위 분할
- START/DATA/END 전송
- ACK/NACK timeout과 재전송

### Phase 5 — A/B 및 rollback
- ota_0 → ota_1
- ota_1 → ota_0
- 전원 차단
- 손상 이미지
- 새 이미지 부팅 실패 rollback

### Phase 6 — 브로드캐스트
- 다중 단말 응답 슬롯
- 누락 청크 수집
- 선택적 재전송

## 17. 테스트 항목

### 공용 프로토콜
- START/DATA/END/ACK/NACK encode/decode
- 최대 47-byte DATA
- 마지막 1~46-byte DATA
- CRC 오류
- 잘못된 길이, version, type
- ESP32/Pi 동일 golden vector

### 세션
- 정상 순차 수신
- 중복/누락/역순 sequence
- 잘못된 session
- oversized image
- timeout
- SHA-256 불일치
- END 조기 도착

### 실보드
- ota_0 실행 중 ota_1 업데이트
- ota_1 실행 중 ota_0 업데이트
- 수신/기록 중 전원 차단
- 마지막 DATA 또는 ACK 유실
- 정상 이미지 VALID 확정
- 잘못된 이미지 rollback

### RF
- 최대 60-byte packet 송수신
- 연속 수신 FIFO overflow
- CRC 실패
- ACK timeout/재전송
- 낮은 RSSI 및 패킷 손실

## 18. 완료 조건

- RF packet body 60바이트 제한을 모든 패킷이 만족
- ESP32와 Pi가 동일한 test vector 통과
- 비활성 OTA 슬롯에 이미지가 정확히 기록됨
- 중복/누락/손상 패킷이 안전하게 처리됨
- SHA-256 불일치 시 부팅 파티션을 변경하지 않음
- OTA 도중 전원 차단 후 기존 이미지로 부팅
- 새 이미지 정상 시 VALID 확정
- 새 이미지 실패 시 이전 슬롯으로 rollback
- ota_0 ↔ ota_1 양방향 업데이트 성공

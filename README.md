# ota-protocol

`firmware-esp32`와 `gateway-ota`가 공유하는 OTA 패킷 규격 (순수 C, header-only).
**Draft v0.1** — 아래 "팀 합의 필요 항목"이 확정되기 전까지는 내용이 바뀔 수 있습니다.

## 패킷 헤더 (`ota_packet_header_t`, `include/ota_protocol.h`)

9byte, `#pragma pack(push,1)`로 정렬 패딩 없음.

| 필드 | 타입 | 크기 | 의미 |
|---|---|---|---|
| `version` | `uint8_t` | 1byte | 프로토콜 버전 |
| `type` | `uint8_t` | 1byte | DATA(0) / ACK(1) / NACK(2) |
| `seq` | `uint16_t` | 2byte | 시퀀스 번호 (0 ~ total_chunks-1) |
| `total_chunks` | `uint16_t` | 2byte | 전체 청크 개수 |
| `payload_length` | `uint8_t` | 1byte | 실제 데이터 길이 (패딩 없음) |
| `crc16` | `uint16_t` | 2byte | payload CRC-16/CCITT-FALSE |

최대 payload **55byte** (CC1101 FIFO 64byte 기준). CRC는 **CRC-16/CCITT-FALSE**.

## API

`#include "ota_protocol.h"` 하나면 끝입니다 (C·C++ 공용, name mangling 걱정 없음).

- `ota_protocol_encode_data(...)` — DATA 패킷(헤더+payload) 직렬화
- `ota_protocol_encode_control(...)` — ACK/NACK 패킷(헤더만) 직렬화
- `ota_protocol_decode(...)` — 수신 바이트열 파싱 + CRC16 검증
- `ota_protocol_crc16(...)` — CRC 계산

함수별 사용법은 헤더 파일의 주석에, 실제 호출 예시는 `tests/test_ota_protocol.c`에
있습니다. 테스트는 `gcc`만 있으면 됩니다:

```
cd tests && gcc -std=c99 -I../include test_ota_protocol.c -o t && ./t
```

## 팀 합의 필요 항목

- [ ] `version` 값 운용 방식
- [ ] `type` 추가 필요 여부 (지금은 DATA/ACK/NACK 3종류뿐)
- [ ] NACK에 실패 사유(CRC 오류 vs 타임아웃 등)를 실을지
- [ ] CRC16 채택 여부 — CC1101 하드웨어 CRC(`crc_ok`)와 중복인지
- [ ] 최대 payload 55byte 확정 (`cc1101-radio-api.md`의 "61byte"와 재대조)
- [ ] `seq`/`total_chunks` 16bit로 충분한지 (최대 약 65,535개 청크 × 55byte ≈ 3.5MB)

## 담당
팀원2 + 팀원3, 4 공동 관리

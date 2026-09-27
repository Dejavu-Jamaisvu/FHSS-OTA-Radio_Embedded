# ota-protocol

`firmware-esp32`와 `gateway-ota`가 공유하는 OTA 패킷 규격 (순수 C, header-only).
**v0.2** (2026-08-11) — RF/ESP32 담당 팀원의 `plan_A.md` 제안을 채택해서 확정.
v0.1(9byte 헤더, DATA/ACK/NACK 3종류)은 폐기됨.

## 패킷 종류 (`include/ota_protocol.h`)

7종류: `OTA_PKT_START`/`OTA_PKT_DATA`/`OTA_PKT_END`/`OTA_PKT_ACK`/`OTA_PKT_NACK`/
`OTA_PKT_DISCOVER`/`OTA_PKT_DISCOVER_ACK`(2026-08-11 추가).
모든 다중 바이트 필드는 **Little Endian**으로 명시적으로 직렬화합니다(구조체를
그대로 `memcpy`하지 않고 `ota_write_u32_le()` 등으로 바이트 단위 조립 — 컴파일
플랫폼에 안 흔들리도록).

| 패킷 | 크기 | 용도 |
|---|---|---|
| `OTA_DISCOVER` | 1byte | 조회 질의 — 게이트웨이가 브로드캐스트로 "OTA 대기중인 기기 있나?" (바디 없음, type만) |
| `OTA_DISCOVER_ACK` | 7byte | 조회 응답 — MENU_OTA 대기중인 각 ESP32가 `device_id`(3byte)+`fw_major/minor/patch` 실어서 응답 |
| `OTA_START` | 49byte | 세션 시작 — `session_id`/`target_device_id`/`image_size`/`total_chunks`/`image_sha256`(32byte) |
| `OTA_DATA` | 12byte 헤더 + 최대 48byte payload | 펌웨어 청크. `session_id`/`sequence`/`payload_length`/`crc16` |
| `OTA_END` | 13byte | 세션 종료 — `session_id`/`image_size`/`total_chunks` (START 값과 대조용) |
| `OTA_ACK`/`OTA_NACK` | 11byte | `session_id`/`acknowledged_type`/`sequence`/`result_code`(`ota_result_t`, 실패 사유 포함) |

`OTA_DISCOVER`/`OTA_DISCOVER_ACK`는 세션 시작 **이전** 단계라 `session_id`가
없습니다. `device_id`는 와이어(패킷)상으로만 3byte(0~16,777,215,
`OTA_DEVICE_ID_MAX`)로 줄었고, 디코딩되면 여전히 `uint32_t`라 상위 byte가
이미 0으로 채워진 채 나옵니다 — 화면엔 "AA-BB-CC"처럼 hex 3그룹으로 조립해서
표시. 조회로 알아낸 `device_id`를 그대로(별도 변환 코드 없이)
`OTA_START.target_device_id`(4byte)에 대입하면 이후 흐름(START/DATA/END/ACK/NACK)은
전혀 바뀌지 않습니다 — 기존 포맷에 얹는 순수 추가입니다. `device_id` 자체는
ESP32 MAC 주소(6byte)의 **뒤 3byte**(NIC 고유 부분)를 그대로 씁니다 — 앞
3byte(OUI, 제조사 식별용)는 같은 제조사 칩끼리 전부 동일해서 기기 구분에
쓸모가 없기 때문입니다(2026-08-11 결정).

**2026-08-11: `version` 필드 제거.** 모든 패킷 맨 앞의 1byte 버전 필드를
없앴습니다. 매직 넘버 도입을 검토하다가 "애초에 뭘 막아주는 필드인지" 다시
따져본 결과: START/DATA/END/ACK/NACK는 이미 `session_id`(세션마다 랜덤)를
갖고 있어 매직 넘버보다 강한 필터가 되고, DISCOVER/DISCOVER_ACK는 session_id가
없어 무방비지만 "조회"라는 낮은 위험도 작업이라(최악의 경우도 재조회하면
그만, 실제 플래시 기록으로 이어지지 않음) 문제되지 않는다고 판단해 매직
넘버도 version도 둘 다 넣지 않기로 했습니다. 자세한 배경은
`docs/design-notes-ota-protocol-es.md` 참고.

RF 상위 계층(`rf_transport`)이 보장하는 패킷 바디 최대 크기는 **60byte로 확정**
(`OTA_RF_PACKET_BODY_MAX_SIZE`, RF 담당 실측/확정값). payload는 CRC-16/CCITT-FALSE로
검증(CC1101 하드웨어 CRC와 별개, 비용 거의 없어 유지).

**재전송 전략은 이 헤더 범위 밖입니다.** 청크마다 Stop-and-Wait으로 할지, 배치/윈도우로
묶어서 할지는 세션 레이어(`gateway-ota`의 `OtaSession`, ESP32의 `ota_client`)가
결정하며, 어느 쪽이든 이 DATA/ACK/NACK 포맷을 그대로 씁니다. `plan_A.md`는 1차
구현으로 Stop-and-Wait을 제안했고, 배치/윈도우 방식(개인 설계 노트 참고)은 이후
필요하면 세션 레이어에서 얹을 수 있는 최적화로 남겨둡니다.

## API

`#include "ota_protocol.h"` 하나면 끝입니다 (C·C++ 공용, name mangling 걱정 없음).

- `ota_protocol_peek_type(...)` — 맨 앞 2byte(version, type)만 먼저 확인 (어떤 decode_*를 호출할지 판단용)
- `ota_protocol_encode_discover/decode_discover` — OTA_DISCOVER (바디 없음, 성공 여부만)
- `ota_protocol_encode_discover_ack/decode_discover_ack` — OTA_DISCOVER_ACK
- `ota_protocol_encode_start/decode_start` — OTA_START
- `ota_protocol_encode_data/decode_data` — OTA_DATA (+ payload CRC16 검증)
- `ota_protocol_encode_end/decode_end` — OTA_END
- `ota_protocol_encode_ack/decode_ack` — OTA_ACK/OTA_NACK 공용
- `ota_protocol_total_chunks(image_size)` — 이미지 크기로 총 청크 개수 계산
- `ota_protocol_crc16(...)` — CRC 계산

모든 decode 함수는 **검증에 실패하면 out 파라미터를 전혀 건드리지 않습니다**
(호출자가 반환값을 안 보고 out 파라미터를 읽어도 검증 안 된 값이 섞이지 않도록).

함수별 사용법은 헤더 파일의 주석에, 실제 호출 예시는 `tests/test_ota_protocol.c`에
있습니다(14개 케이스: 크기 상수, LE 직렬화(16/24/32bit), total_chunks 계산,
START/DATA/END/ACK/NACK/DISCOVER/DISCOVER_ACK 왕복, CRC 손상 거부, payload
초과 거부, device_id 24bit 초과 거부, peek_type). `peek_type()`은 이제 맨 앞
1byte(type)만 봅니다(2026-08-11, version 제거로 확인할 값이 줄어듦). 테스트는
`gcc`만 있으면 됩니다:

```
cd tests && gcc -std=c99 -I../include test_ota_protocol.c -o t && ./t
```

## 해결된 항목 (v0.2에서 `plan_A.md` 채택으로 결론)

- [x] `type` 추가 — DATA/ACK/NACK 3종 → START/DATA/END/ACK/NACK 5종으로 확장
- [x] NACK 실패 사유 — `result_code`(`ota_result_t`, 12가지 사유) 필드로 해결
- [x] CRC16 채택 여부 — 유지하기로 함 (하드웨어 CRC와 중복이어도 비용 거의 없음)
- [x] 최대 payload 크기 — RF 담당이 확정한 60byte 기준 **47byte**로 정리
- [x] 바이트 순서 — **Little Endian**으로 명시 확정, 구조체 `memcpy` 대신 명시적 직렬화 함수 사용
- [x] **재전송 전략** — Selective-Repeat(실패한 seq만 콕 집어 재전송, Go-Back-N
      아님) + **고정 크기 배치**(슬라이딩 윈도우 아님 — 정해진 개수(`batchSize`)를
      딱 끊어서 보내고, 그 배치 전체가 끝나야 다음 배치로 넘어감). `batchSize`는
      가변 파라미터, **초기값 5**(2026-08-11, 3에서 상향)로 결정(청크 5개=235byte,
      ESP32 플래시 버퍼 4096byte 대비 여유 충분). `batchSize=1`이면 Stop-and-Wait과 동일 동작이라
      파라미터 하나로 언제든 비교 가능. **패킷 포맷 변경 불필요**: 새 "블록 ACK"
      패킷 타입 없이 기존 개별 ACK/NACK(seq 단위, `result_code`의
      `OTA_RESULT_BUSY`로 수신측 backpressure까지 커버)을 그대로 재사용. `batchSize`를
      나중에 크게 키워서 ACK 트래픽이 병목이 되면 그때 블록 요약 패킷을 최적화로
      검토(지금은 불필요). 세부 설계는 `gateway-ota`의 `docs/fsm-design.md` §6 참고.

- [x] **대상 기기 선택 방식** — 무작정 브로드캐스트로 START부터 쏘는 대신,
      `OTA_DISCOVER`(조회)로 먼저 "지금 OTA 대기중(MENU_OTA)인 기기 누구
      있나?"를 물어보고, `OTA_DISCOVER_ACK` 응답들을 모아 화면에 목록으로
      보여준 뒤 사용자가 직접 대상을 고르는 방식으로 결정. 조회 창은
      **고정 3초 + 재조회 버튼**(사용자가 계속 열어두는 방식 대비 구현 단순,
      놓친 기기는 버튼으로 재시도). 상세 비교와 확장 가능성은
      `docs/design-notes-ota-protocol-es.md` 참고. gateway-ota 쪽 상태 설계는
      `docs/fsm-design.md`의 `DISCOVERING`/`SELECTING` 상태 참고.

- [x] **`device_id` 크기 4byte → 3byte 축소** — 무전기 몇 대 규모에 4byte(약
      43억개)는 과했음. 와이어상으로만 3byte(0~16,777,215)로 줄이고
      화면엔 "AA-BB-CC"처럼 hex 3그룹으로 표시. C 구조체(`ota_discover_ack_fields_t.device_id`)
      자체는 여전히 `uint32_t`라 `OTA_START.target_device_id`에 대입 코드 한
      줄이면 충분(자동으로 zero-extend된 값과 동일) — `OTA_BROADCAST_DEVICE_ID`
      (4byte 전부 0xFF)와는 상위 byte가 항상 달라서 충돌 없음.

- [x] **`device_id`는 MAC 주소 뒤 3byte** — ESP32 MAC 주소(6byte) 중 앞
      3byte(OUI, 제조사 식별용)는 같은 제조사 칩끼리 전부 동일해 기기 구분에
      쓸모없고, 뒤 3byte(NIC 고유 부분)만으로 충분히 구분되므로 이 부분을
      그대로 `device_id`로 씀(2026-08-11).

- [x] **매직 넘버 도입 안 함 + `version` 필드 제거** — 원래 있던 매직 넘버
      검토 및 1byte `version` 필드를 둘 다 없애기로 결정. START/DATA/END/
      ACK/NACK는 `session_id`(세션마다 랜덤)가 매직 넘버보다 강한 필터가
      되고, DISCOVER/DISCOVER_ACK는 session_id가 없어 무방비지만 "조회"라는
      낮은 위험도 작업이라(최악의 경우도 재조회하면 그만) 문제되지 않음.
      `version` 필드도 같은 맥락에서 제거 — 애초에 v0.1→v0.2 전환 때도 값이
      한 번도 안 바뀌었던 걸 확인(보호 기능이 실제로 작동한 적이 없었음).
      모든 패킷이 1byte씩 줄어듦(`OTA_RESULT_INVALID_VERSION`도 결과 코드에서
      제거). 자세한 배경은 `docs/design-notes-ota-protocol-es.md` 참고.

- [x] **`OTA_PKT_DISCOVER`/`OTA_PKT_DISCOVER_ACK` 타입값(6, 7) 충돌 걱정 없음** —
      `plan_A.md` 원안엔 없던 값을 이번에 새로 붙였지만, ESP32와 게이트웨이가
      전부 이 `ota_protocol.h` 하나를 그대로 가져다 쓰는 **공유 단일 헤더
      구조**라서 애초에 "ESP32 쪽이 6, 7을 몰래 다른 용도로 이미 쓰고 있다"는
      상황 자체가 생길 수 없음(따로따로 각자 값을 정하는 구조가 아니라, 이
      enum 하나가 유일한 정의처). version 필드를 없앤 것과 같은 이유.

## 팀 합의 필요 항목 (남은 것)
- [ ] **DISCOVER_ACK 랜덤 백오프(충돌 회피)** — 여러 기기가 동시에
      `OTA_DISCOVER`를 받으면 응답이 겹쳐서 무선 충돌이 날 수 있음. 응답 전
      짧은 랜덤 지연을 두는 건 ESP32 펌웨어(수신측) 책임이라 이 헤더 범위
      밖이지만, 백오프 범위(예: 0~50ms)는 RF 담당과 확인 필요.
- [ ] `seq`/`total_chunks`를 32bit로 키운 것에 대한 최종 확인 — 한때 16bit
      (0~65,535개, ≈3.1MB)로 줄이는 걸 검토했으나, 
      "일단 여유 있게 32bit로 두고, 나중에 실제로 좁혀도 되는 게 확인되면 그때
      줄이자"로 정리해 32bit 유지. 

## 담당
팀원2 + 팀원3, 4 공동 관리

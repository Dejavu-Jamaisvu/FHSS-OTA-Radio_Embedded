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
| `total_chunks` | `uint16_t` | 2byte | 전체 청크 개수 (DATA에서만 의미 있음) |
| `payload_length` | `uint8_t` | 1byte | 패딩 없는 실제 데이터 길이 |
| `crc16` | `uint16_t` | 2byte | payload CRC-16/CCITT-FALSE |

- 최대 payload: **55byte** (CC1101 FIFO 64byte 기준, `64 - 9(헤더) = 55`)
- CRC: **CRC-16/CCITT-FALSE** (poly `0x1021`, init `0xFFFF`)

## API (`include/ota_protocol.h`)

- `ota_protocol_crc16(data, length)`
- `ota_protocol_encode_data(...)` — DATA 패킷(헤더+payload) 직렬화
- `ota_protocol_encode_control(...)` — ACK/NACK 패킷(헤더만) 직렬화
- `ota_protocol_decode(...)` — 수신 바이트열을 헤더+payload로 파싱, DATA면 CRC16까지 검증

## 사용 예시

```c
uint8_t packet[OTA_MAX_PACKET_SIZE];
size_t n = ota_protocol_encode_data(packet, sizeof(packet), seq, total_chunks, payload, payload_len);
/* n byte짜리 packet을 CC1101 TX FIFO에 그대로 write */
```

```c
ota_packet_header_t header;
const uint8_t *payload; size_t payload_len;
if (ota_protocol_decode(received, received_len, &header, &payload, &payload_len)) {
    /* header.type으로 DATA/ACK/NACK 분기 */
} else {
    /* CRC 불일치 등 -> NACK 응답 */
}
```

## 테스트

Qt도 ESP-IDF도 필요 없이 `gcc`만 있으면 검증됩니다.

```
cd tests
gcc -std=c99 -Wall -Wextra -I../include test_ota_protocol.c -o test_ota_protocol
./test_ota_protocol
```

DATA 왕복 인코딩/디코딩, 손상된 payload의 CRC 거부, ACK/NACK 인코딩, 최대 payload
초과 시 거부까지 확인합니다.

## 팀 합의 필요 항목

- [ ] `version` 값 운용 방식 (프로토콜 바뀌면 올리는지, 언제 올리는지)
- [ ] `type` 값 추가 필요 여부 (지금은 DATA/ACK/NACK 3종류뿐)
- [ ] NACK에 실패 사유(예: CRC 오류 vs 타임아웃)를 실어야 하는지 — 지금은 사유 없이 seq만 전달
- [ ] CRC16 채택 여부 — CC1101 하드웨어 CRC(`crc_ok`)와 애플리케이션 계층 CRC16을 둘 다 쓸지, 하나만 쓸지
- [ ] 최대 payload 55byte 확정 (`cc1101-radio-api.md`의 "최대 RF 프레임 초기값 61byte"와 정확히 맞는지 재확인)
- [ ] `seq`/`total_chunks`를 16bit로 둬도 충분한지 (최대 약 65,535개 청크 × 55byte ≈ 3.5MB)

## 담당
팀원2 + 팀원3, 4 공동 관리

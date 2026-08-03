# ota-protocol

`firmware-esp32`와 `gateway-ota`가 공유하는 OTA 패킷 규격 (순수 C 헤더).

## 핵심 기능
- 패킷 헤더: 타입 / 시퀀스번호 / 총 청크 수 / 페이로드 길이 / CRC / 버전
- 최대 청크 페이로드 ~54~56byte (CC1101 FIFO 64byte 제약 반영)
- ACK/NACK 메시지 포맷

## 담당
팀원2 + 팀원3, 4 공동 관리

# FHSS OTA Radio

주파수 호핑(FHSS) 기반 무전기 + RF 무선 펌웨어 업데이트(OTA) 시스템.

## 목적

- 재밍/도청에 강한 주파수 호핑 방식 음성 통신
- Wi-Fi/인터넷 없이 RF 통신으로 펌웨어 업데이트

## 구성

| | |
|---|---|
| 무전기 단말 | ESP32-S3-DevKitC-1 |
| OTA 게이트웨이 | Raspberry Pi 4B (Yocto) |
| RF | CC1101 (음성 FHSS + OTA 겸용, 단일 트랜시버) |
| 음성 코덱 | Speex Narrowband (8kHz) |

## 레포지토리

| 레포지토리 | 역할 |
|---|---|
| [`firmware-esp32`](https://github.com/fhss-ota-radio/firmware-esp32) | 무전기 단말 펌웨어 |
| [`ota-protocol`](https://github.com/fhss-ota-radio/ota-protocol) | OTA 패킷 규격 (공용 헤더) |
| [`kernel-cc1101-spi`](https://github.com/fhss-ota-radio/kernel-cc1101-spi) | RPi용 CC1101 커널 드라이버 |
| [`gateway-ota`](https://github.com/fhss-ota-radio/gateway-ota) | OTA 매니저 Qt 앱 |
| [`meta-walkie-talkie`](https://github.com/fhss-ota-radio/meta-walkie-talkie) | Yocto 빌드 레시피 |
| [`docs-architecture`](https://github.com/fhss-ota-radio/docs-architecture) | 설계 문서, 마일스톤 |

## 팀

PM/오디오 · OTA·부트로더 · 리눅스 드라이버 · Qt 앱 · FHSS 알고리즘, 5인.

## 문서

자세한 내용은 [`docs-architecture`](https://github.com/fhss-ota-radio/docs-architecture)의 `PROJECT_SPEC.md`(전체 명세), `FHSS_SYNC_DESIGN.md`(동기화 설계), `standard-github.md`(개발 컨벤션) 참고.

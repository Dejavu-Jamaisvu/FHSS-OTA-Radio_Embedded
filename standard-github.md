### 저장소(Repository) 관리 규정

조직명: `fhss-ota-radio`

#### 레포 목록

| 레포명 | 역할 | 담당 |
|---|---|---|
| `firmware-esp32` | ESP32-S3 무전기 단말 펌웨어 전체 | 팀원1, 2, 5 |
| `ota-protocol` | OTA 패킷 포맷/청크/재전송 규격 (양쪽 공유) | 팀원2 + 3, 4 공동 관리 |
| `kernel-cc1101-spi` | RPi용 CC1101 SPI 커널 드라이버 (LKM) | 팀원3, 4 |
| `gateway-ota` | OTA 매니저 Qt/C++ 앱 (BIN 분할·전송·재전송) | 팀원3, 4 |
| `meta-walkie-talkie` | Yocto 빌드 레시피 및 커스텀 커널 구성 | 팀원3, 4 |
| `docs-architecture` | 시스템 설계서, 프로토콜 정의서, 마일스톤 관리 | 팀원1(PM) 총괄, 전원 기여 |

`ota-protocol`을 별도 레포로 분리한 이유: ESP32 수신 로직(`firmware-esp32`)과 Qt 송신 로직(`gateway-ota`)이 동일한 패킷 구조체를 공유해야 하는데, 각자 레포에 따로 정의하면 스펙이 어긋날 수 있음. 헤더 하나(`ota_packet.h`)를 여기 두고 양쪽에서 git submodule로 참조.

#### `firmware-esp32` 내부 컴포넌트 구조

레포는 하나로 유지하되(단일 바이너리 빌드 특성상), ESP-IDF 컴포넌트 단위로 담당자 폴더를 분리해 충돌을 줄인다.

```
firmware-esp32/
├── main/                  # 통합 진입점 (컴포넌트 wiring, 전역 FSM)
├── components/
│   ├── audio_io/          # 팀원1 — I2S 마이크/스피커
│   ├── audio_codec/       # 팀원1 — Opus 인코더/디코더
│   ├── display_ui/        # 팀원1 — OLED 상태 표시
│   ├── ota_client/        # 팀원2 — OTA 수신, 파티션 전환, 롤백
│   ├── fhss_core/         # 팀원5 — 호핑 시퀀스, 동기화
│   └── rf_transport/      # 팀원5(+2) — nRF24/CC1101 저수준 SPI
└── docs/
```

브랜치 prefix는 담당 컴포넌트로 통일: `feature/audio-*`, `feature/ota-*`, `feature/fhss-*`, `feature/display-*`

### Git 브랜치 및 워크플로우 전략

모든 개발은 Git Flow 표준을 준수하며, `main` 브랜치의 안정성을 최우선으로 한다.

- **브랜치 계층 구조:**
    - `main`: 배포 가능한 최고 안정화 버전 (Release)
    - `develop`: 기능 통합 브랜치 (Integration)
    - `feature/*`: 개별 모듈 개발 브랜치 (e.g., `feature/fhss-hopping-sync`, `feature/ota-chunk-retry`)
    - `hotfix/*`: 긴급 버그 수정 브랜치

**규칙1: 원자적 커밋** — 하나의 커밋에는 '하나의 기능' 또는 '하나의 수정 사항'만

- **나쁜 예:** `feat: 오디오 드라이버 추가 및 버튼 로직 수정` (두 가지 기능 섞음)
- **좋은 예:**
    - `feat: I2S DMA 버퍼 설정 추가`
    - `fix: PTT 버튼 인터럽트 채터링 제거`

**규칙 2: 로컬은 자주, 공유는 정리해서**

- `feature/*` 브랜치 내에서는 아주 자주 커밋해도 상관없습니다. (작업 내용을 저장한다는 개념)
- 하지만 `develop`이나 `main` 브랜치에 합치기 전에는 Squash(여러 커밋을 하나로 합치기)를 사용해 불필요한 로그를 정리하세요.
- _중요:_ 컴파일이 안 되는 상태의 코드를 `develop`에 올리는 것은 금지입니다. 로컬에서 자주 커밋하되, 남들에게 보여줄 때는 '완성된 기능' 단위로 보여주세요.

**규칙 3: 커밋 메시지 표준화**

- `feat`: 새로운 기능 추가
- `fix`: 버그 수정
- `docs`: 문서 수정
- `style`: 코드 포맷팅 (로직 변경 없음)
- `refactor`: 코드 리팩토링

### 레포별 주요 구현 기능 명세

#### 1. `firmware-esp32`

**`audio_io`** (팀원1)
- I2S 마이크 입력 캡처 (8kHz 샘플링, DMA 버퍼)
- I2S 스피커 출력 재생
- PTT 버튼 인터럽트 기반 TX/RX 모드 전환 (디바운싱)
- 오디오 링버퍼 관리

**`audio_codec`** (팀원1)
- Opus Narrowband(8kHz, 6~8kbps) 인코더/디코더 컴포넌트 포팅
- 인코딩 레이턴시 측정 및 튜닝, 메모리 사용량 최적화

**`display_ui`** (팀원1)
- OLED(SSD1306 등) I2C 드라이버 초기화
- 배터리/채널/TX-RX 상태/OTA 진행률 표시
- `fhss_core`의 FSM 상태를 UI에 반영

**`ota_client`** (팀원2)
- Dual OTA 파티션 테이블 설계, 부트로더 커스터마이징
- `ota-protocol` 규격 기반 청크 수신·재조립
- 청크 CRC 검증, 누락분 NACK 재요청
- 이미지 무결성 검증 후 부팅 파티션 전환
- 업데이트 실패 시 이전 파티션 롤백

**`fhss_core`** (팀원5)
- 의사난수 기반 호핑 시퀀스 생성 및 시드 동기화
- TX/RX 동기화(비콘 또는 시간 동기)
- 채널 수·호핑 주기 파라미터 관리
- 충돌/재밍 감지 시 재동기화

**`rf_transport`** (팀원5 + 팀원2 지원)
- nRF24L01 저수준 SPI 드라이버 (음성용, `fhss_core`가 사용)
- CC1101 수신 인터페이스 (OTA 수신 경로, `ota_client`가 사용)

#### 2. `kernel-cc1101-spi` (팀원 3, 4)
- CC1101 SPI 디바이스 probe/remove (LKM)
- 레지스터 설정 ioctl 인터페이스 (주파수, 변조방식, 대역폭)
- TX/RX FIFO ↔ read/write syscall 매핑
- GDO0/GDO2 인터럽트 기반 수신 알림
- `/dev/cc1101` 캐릭터 디바이스 노드 제공
- 주소 필터링 비활성화 모드로 1:N 브로드캐스트 지원

#### 3. `gateway-ota` (팀원 3, 4)
- Qt GUI: `.bin` 파일 선택/업로드
- `ota-protocol` 규격대로 BIN 청크 분할 + CRC 첨부
- `/dev/cc1101`로 청크 전송, 진행률·로그 표시
- ESP32 ACK/NACK 수신 → 재전송 큐 관리
- 유니캐스트(특정 단말) / 브로드캐스트(1:N) 전송 모드
- 전송 완료·실패 리포트

#### 4. `ota-protocol` (팀원2 + 3, 4 공동)
- 패킷 헤더 구조체 (타입, 시퀀스 번호, 총 청크 수, 페이로드 길이, CRC)
- CC1101 FIFO(64byte) 제약 반영한 최대 청크 크기 상수 (페이로드 약 54~56byte)
- ACK/NACK 메시지 포맷, 프로토콜 버전 필드(호환성 체크)
- 순수 C 헤더로 제공 → ESP-IDF/Qt-C++ 양쪽에서 동일 구조체 include
- 시퀀스 다이어그램 포함 문서

#### 5. `meta-walkie-talkie` (팀원 3, 4)
- 커스텀 Yocto 레이어 및 bblayers 등록
- `kernel-cc1101-spi` 포함하는 커널 defconfig
- Qt 및 의존 라이브러리 레시피
- `gateway-ota` 자동 빌드·설치 레시피, 부팅 시 자동 실행 systemd 서비스
- 이미지 용량 최적화

#### 6. `docs-architecture` (팀원1 PM 총괄, 전원 기여)
- 시스템 아키텍처 다이어그램
- `ota-protocol` 스펙 링크/버전 이력
- FHSS 알고리즘 설계서 (호핑 파라미터, 동기화 방식)
- 오디오 코덱 선정 근거 (Opus vs Speex 비교)
- BOM/핀맵, 4주 마일스톤, 회의록

# AP Firmware Development History

## V3.31 양산 버전 대비 V4.0 P0List 개발 변경 이력

| 항목 | 내용 |
|---|---|
| 문서 작성일 | 2026-09-16 |
| 비교 기준 버전 | `ApToEthercat_V3.31_20250919/ApToEthercat_V3.31` |
| 비교 대상 버전 | `ApToEthercat_V4.0_260910_P0List` |
| 기준 버전의 용도 | 현재 양산 중인 AP 펌웨어 |
| 대상 버전의 용도 | V4.0 개발 및 양산 전 P0 항목 보완 버전 |
| 비교 범위 | AP 소스, EtherCAT/PDO 정의, ESP-NOW 송수신, 페어링·자동복구, I/O·Serial 데이터 경로, LED, 진단 기능, 빌드 환경 |
| 제외 범위 | Peer 펌웨어 자체 변경, 현장 성능의 최종 판정, LFC 응답 길이 정책 변경 |

> 이 문서는 파일명에 포함된 날짜와 현재 체크아웃을 기준으로 작성하였다. 현재 V4.0 저장소의 Git 이력은 단일 초기 커밋 이후 작업 트리 변경으로 구성되어 있으므로, 각 기능의 최초 적용 날짜를 Git 커밋으로 증명할 수는 없다. 아래의 "변경 확인"은 실제 소스 비교 결과이며, "효과/위험"은 해당 변경에 대한 공학적 판단이다.

---

## 1. 버전 계보

| 단계 | 식별 기준 | 성격 | 주요 내용 | 판정 |
|---|---|---|---|---|
| 1 | V3.31 / 2025-09-19 | 양산 기준선 | 기존 PDO 및 ESP-NOW 프레임, 16채널 순회, I/O·Serial cache, 자동 재페어링 | 양산 운용 중 |
| 2 | V4.0 / 2026-05-28 legacy source | V4.0 기능 개발 기준 | Peer 상태 LED, 원자적 pairing mask, 시험 로그, V4.00 식별 기능을 추가한 흔적 | 참고용 소스 보존 |
| 3 | V4.0 P0List / 2026-09-10 | 양산 전 구조 개선 | PlatformIO 구조, 모듈 분리, 송수신 Queue 정비, 수신 프레임 검증, 진단 CLI, 정적 LED task | 현재 개발 대상 |
| 4 | V4.0 P0List / 2026-09-16 | 현재 검토 상태 | 수신 Queue 계측, 자동복구 성공 후 상태 해제 보완, 다중 Peer timeout 구조 검토 | 개발 중, 양산 승인 전 |

---

## 2. 비교 결과 요약

### 2.1 호환성이 유지된 영역

| 확인 항목 | 비교 결과 | 의미 |
|---|---|---|
| EtherCAT PDO 정의 | `AP_SLAVE.h` SHA-256 동일 | WOS와 AP 사이의 25 Word 입·출력 PDO 배치는 변경되지 않음 |
| LAN9252/EasyCAT 라이브러리 | `EasyCAT.h` SHA-256 동일 | LAN9252 초기화 및 PDO 교환 라이브러리 자체는 변경되지 않음 |
| CRC 정의 | `common.h` SHA-256 동일 | CRC16-MODBUS 계산 규격은 변경되지 않음 |
| AP-Peer 무선 프레임 크기 | `WIFI_PACKET_MAX = 50` 유지 | 기존 Peer와의 프레임 크기 호환성 유지 |
| AP-Peer 무선 프레임 필드 | Group, Channel, Command, Type, Address, Length, Data, CRC 유지 | 양산 Peer 펌웨어의 프레임 형식을 변경하지 않음 |
| Command 값 | `0x01~0x06`, `0x81~0x86` 유지 | Pairing, I/O, Serial 명령/응답 코드 호환성 유지 |
| 무선 속도·대역폭 | HT40, `WIFI_PHY_RATE_5M_L` 유지 | 기본 RF 설정의 의도는 V3.31과 동일 |
| 일반/Pairing timeout 설정값 | Pairing 35 count, Paired 20 count 유지 | 명목 timeout count는 V3.31과 동일. 단, count는 실제 고정 ms가 아니라 `Wifi_Handle()` 서비스 횟수임 |

### 2.2 핵심 변경 방향

V4.0은 통신 규격을 새로 만든 버전이라기보다, V3.31의 WOS/PDO/Peer 호환성을 유지하면서 AP 내부의 다음 영역을 개선한 버전이다.

1. ESP-NOW 수신 callback과 실제 프레임 처리의 분리
2. 송신 대기열의 ring buffer 규칙 및 소비 시점 보정
3. 늦은 응답·잘못된 MAC·예상하지 않은 Command·손상 프레임 차단
4. I/O page와 Serial channel/page의 범위 방어
5. 자동 재페어링 상태의 종료·초기화 경로 보강
6. Peer별 연결 상태와 RSSI를 표시하는 LED 기능 추가
7. 현장 진단용 CLI와 RX Queue 통계 추가
8. 단일 Arduino sketch 중심 구조에서 PlatformIO 기반 모듈 구조로 변경

---

## 3. 개발자용 상세 변경 이력

| No. | 구분 | V3.31 양산 버전 | V4.0 P0List 변경 내용 | 주요 소스 위치 | 동작상 개선점 | 영향·주의사항 | 현재 상태 |
|---:|---|---|---|---|---|---|---|
| D-01 | 프로젝트 구조 | 주요 형식·상태·처리가 하나의 `.ino`에 집중됨 | 설정, 자료형, 전역 context, 채널 변환, board I/O, TX Queue, RX Queue, LED driver, CLI를 `include/`와 `src/`로 분리 | `platformio.ini`, `include/ap_*.h`, `src/*.ino`, `src/*.cpp` | 변경 영향 범위를 파일 단위로 추적하기 쉬워지고 빌드 입력이 명시됨 | 파일 간 의존관계가 늘어남. include 순서와 extern 소유권 관리 필요 | 적용 |
| D-02 | 빌드 환경 | Arduino IDE 계열 설정 사용. 확인된 core는 ESP32 Arduino 2.0.11, IDF 4.4.5, GCC 8.4.0 | PlatformIO `espressif32@6.4.0`, framework 2.0.11, GCC 8.4.0, `ESP32TimerInterrupt@2.3.0` 고정 | `platformio.ini` | 개발 PC마다 달라질 수 있던 framework/library 조건을 프로젝트 설정으로 고정 | `lib_extra_dirs`가 사용자 로컬 경로를 가리키므로 완전한 독립 재현성은 아직 아님 | 적용, 경로 정리 필요 |
| D-03 | 전역 데이터 구조 | `peer[]`, `peer_bak[]`, `RSSI[]`, `lPeerData`, `lSPeerData`, tick 변수가 전역으로 흩어짐 | AP peer/data/board/OS/Wi-Fi 상태를 `g_ap` context 아래로 그룹화 | `include/ap_context.h`, `include/ap_types.h` | 데이터 소유 영역을 구분하고 함수 인자로 전달할 기반 확보 | `wifi_state_machine`, `wifi_send`는 아직 별도 전역이며 context 이관이 완결되지 않음 | 부분 적용 |
| D-04 | 설정 관리 | debug, timeout, 기능 flag가 main sketch 내부 여러 위치에 존재 | 양산/시험 기능 flag와 protocol 상수를 `ap_config.h`로 집중 | `include/ap_config.h` | 빌드별 설정 확인이 쉬워짐 | `TEMP_*` 설정이 다수 남아 있어 release profile 확정 필요 | 적용, 정리 필요 |
| D-05 | PDO 호환성 | 25 Word 입·출력 구조 사용 | 동일한 `AP_SLAVE.h` 사용 | `include/AP_SLAVE.h` | 기존 WOS PDO와 직접 호환 | 새로운 상태 정보를 PDO에 추가하지 않았으므로 진단 정보는 CLI에 의존 | 호환 유지 |
| D-06 | AP-Peer protocol | 6-byte header + payload + 2-byte CRC, 최대 50 byte | 동일 프레임과 Command 체계 유지. transaction sequence byte는 추가하지 않음 | `include/AP_SLAVE.h`, `Wifi_Peer_Data_Set()` | 이미 설치된 Peer와 호환 | 동일 Peer의 동일 Command에 대한 매우 늦은 이전 응답과 새 응답을 wire 수준에서 완전히 구분할 수 없음 | 호환 유지, 잔여 위험 |
| D-07 | 송신 Queue 자료구조 | 직접 구현한 head/tail 계산. wrap 구간에서 full 판정이 모호하며, 조회 함수가 즉시 dequeue | one-slot-empty 규칙의 ring buffer 모듈로 분리하고 `peek` 후 조건 충족 시 `dequeue` | `include/wifi_queue.h`, `src/wifi_queue.ino` | Queue full 판정과 wrap 동작이 명확해짐. busy 상태 때문에 보내지 못하는 요청을 즉시 잃는 문제를 줄임 | 선두 항목이 busy이면 뒤 항목도 진행하지 못하는 head-of-line blocking은 남음 | 적용, 잔여 위험 |
| D-08 | 송신 대상 busy 검사 | Queue에서 항목을 먼저 꺼낸 뒤, 현재 `channel`의 busy를 검사 | Queue 선두의 실제 `getAddr`에 대해 `tx_busy[getAddr]`, `rx_busy[getAddr]`를 검사 | main `Wifi_Handle()` 송신부 | 다른 채널의 busy 상태로 잘못 판단하거나 요청을 잃을 가능성을 감소 | timeout 상태가 채널별이 아니므로 다중 in-flight 상황의 완전한 해결은 아님 | 적용 |
| D-09 | `esp_now_send()` 즉시 오류 | `ESP_ERR_ESPNOW_NOT_FOUND`와 기타 오류에서 busy/request 상태 정리가 없음 | transaction abort, TX/RX busy 해제, request 해제 및 오류 계수 경로 추가 | main `Wifi_Handle()` send error switch | 송신 API 자체가 실패했는데 응답 대기 상태로 남는 문제 방지 | MAC 계층의 최종 delivery status callback은 현재 등록하지 않음 | 적용, 관측성 확인 필요 |
| D-10 | ESP-NOW 수신 실행 위치 | `recv_cb()` 내부에서 검증, cache 복사, pairing/repair 상태 변경을 모두 수행 | callback은 frame과 수신 문맥만 고정 Queue에 복사하고, 본문 처리는 `Wifi_Rx_ProcessFrame()`으로 이동 | `recv_cb()`, `Wifi_Rx_ProcessFrame()`, `Wifi_Rx_Service()` | callback 점유시간과 공유 상태 동시 변경을 줄이고 처리 순서를 직렬화 | Queue가 가득 차면 신규 frame을 버리는 정책. 서비스 지연 시 backlog 감시 필요 | 적용 |
| D-11 | RX Queue 메모리 | 별도 수신 Queue 없음 | FreeRTOS 정적 Queue, depth 24, 현재 target의 item 72 byte, service budget 4 | `include/wifi_rx_queue.h`, `src/wifi_rx_queue.cpp`, `ap_config.h` | runtime heap 할당 없이 최대 메모리 사용량이 고정됨 | frame storage 약 1,728 byte와 통계/context 메모리 사용. 처리량은 budget과 loop 빈도에 좌우됨 | 적용 |
| D-12 | RX Queue 과부하 정책 | callback에서 즉시 처리하므로 명시적 full 정책 없음 | non-blocking enqueue, full 시 drop 후 이유·채널별 counter 기록 | `wifi_rx_queue_push()`, `wifi_rx_queue_note_drop()` | Wi-Fi task가 수신 처리 때문에 block되는 것을 방지하고 과부하를 계측 가능 | drop은 재전송을 직접 발생시키지 않으며 상위 timeout/재시도에 의존 | 적용 |
| D-13 | 수신 transaction 문맥 | `rx_busy[ch]`와 상태 구조만으로 응답 유효성을 판단 | peer epoch, request epoch, expected response command를 채널별 저장하고 Queue frame에 snapshot | `wifi_rx_transaction_*()` | 삭제 전 frame, 이전 요청 frame, 다른 Command 응답이 cache를 변경하는 범위를 축소 | wire sequence가 없으므로 새 요청 후 도착한 동일 Command의 오래된 응답은 완전 식별 불가 | 적용, protocol 한계 |
| D-14 | 수신 기본 길이 방어 | `memcpy(buff, data, len)` 전에 null/상한/최소 header 길이 검사가 충분하지 않음 | null, `len <= 0`, 최대 50 byte, 최소 10 byte, channel 범위를 먼저 검사 | `Wifi_Rx_ProcessFrame()` | 잘못된 길이에 의한 고정 buffer overflow 및 header/CRC underflow 방지 | 공통 최소 10 byte가 payload 없는 8 byte 응답까지 막을 수 있음. 특히 Pairing Cancel 응답은 handler가 payload를 사용하지 않으므로 실제 Peer 길이 확인 필요 | 적용, 호환성 검토 필요 |
| D-15 | 수신 출처 검증 | Group, channel, type, address, CRC를 검사하지만 등록 Peer MAC 검증 없음 | pairing 완료 후 등록 MAC과 callback source MAC 비교 | `Wifi_Rx_ProcessFrame()` | 동일 channel/type/address를 위조하거나 잘못 라우팅된 응답의 반영 방지 | 신규 pairing 전에는 MAC이 확정되지 않아 type/address 기반 식별 유지 | 적용 |
| D-16 | 예상 Command 검증 | 현재 요청 종류와 응답 Command의 직접 대응 검사가 약함 | AP 요청별 `0x81~0x86` 예상 응답을 계산하고 다른 응답을 drop | `WIFI_RX_STRICT_EXPECTED_COMMAND`, `wifi_rx_expected_response()` | 늦은 다른 종류의 응답이 cache·상태를 갱신하는 문제 감소 | 구형 Peer가 비표준 Command를 사용하면 응답이 거부될 수 있으므로 장치별 호환 시험 필요 | 적용 |
| D-17 | CRC 오류 후 callback 상태 | CRC 오류 return 경로에서 `doing_recv_cb`가 true로 남을 수 있는 구조 | CRC 오류를 포함한 검증 실패 경로에서 처리 중 flag 해제 | `Wifi_Rx_ProcessFrame()` CRC 분기 | 한 번의 손상 frame 후 `Wifi_Handle()`가 계속 return하는 고착 가능성 제거 | 모든 조기 return 경로의 flag 대칭성은 회귀 시험 필요 | 적용 |
| D-18 | 수신 통계 | frame 수신/폐기 사유를 누적해서 볼 방법이 제한적 | enqueue, dequeue, accept, invalid/full/stale/MAC/Command/protocol/CRC drop, channel별 drop, high-watermark 제공 | `wifi_rx_queue_stats_t`, CLI `rxq` | 현장 통신 단절을 원인별로 분류할 근거 확보 | counter overflow/초기화 정책은 장기 운전 기준으로 별도 정의 필요 | 적용 |
| D-19 | I/O page 범위 | WOS word의 page 값을 직접 cache index로 사용 | page 0 보정, Peer가 보고한 page 수 이하 제한, cache 범위 clamp | `Ethercat_Handle()`, `ap_channel_utils.h` | 비정상 page 값으로 잘못된 cache를 읽거나 쓰는 문제 방지 | 잘못된 요청을 오류로 보고하지 않고 보정하므로 진단 시 원래 입력값 확인 필요 | 적용 |
| D-20 | Serial channel/page 범위 | W17 nibble을 바탕으로 `tmpCh=11+ch`, page offset을 직접 계산 | channel 1~4, page 1~10 검증 후 변환·cache 접근 | `is_valid_serial_cmd_ch()`, `is_valid_serial_page()` | Serial cache 배열 범위를 벗어나는 접근 방지 | 잘못된 WOS 명령은 무시되며 별도 오류 PDO는 없음 | 적용 |
| D-21 | Serial pairing-bit 동작 | Pairing bit가 켜진 Serial Peer 처리에서 초기값 `tmpCh=0`을 참조하여 CH0 상태가 영향을 받을 수 있음 | 실제 `peer_channel` 기준으로 `request_serial`과 serial buffer를 준비 | `Ethercat_Handle()` Serial pairing branch | Serial Peer 요청이 대상 채널 상태에 기록됨 | 현재 자동 GET 기능이 켜져 있어 기존 현장 트래픽 주기와 비교 필요 | 적용, 현장 시험 필요 |
| D-22 | Pairing 상태 공유 | `peer[].pairFlag`를 여러 실행 문맥에서 직접 조회 | 원자적 bit mask snapshot을 별도로 유지하고 LED가 snapshot 사용 | `include/PairMask.h`, pairing/delete 경로 | LED task와 통신 경로 사이에서 일관된 16채널 pairing snapshot 제공 | `pairFlag`와 mask를 함께 갱신해야 하므로 누락 경로가 생기면 불일치 가능 | 적용 |
| D-23 | 자동복구 중 명시적 삭제 | 명시적 삭제 경로에서 backup flag가 일부 남을 수 있음 | `del_set` 경로에서 loss 및 `repair_itself` 상태 정리 | `del_peer()` | 사용자가 삭제한 Peer가 자동 재페어링으로 되살아나는 위험 감소 | 삭제 응답이 지연될 때 늦은 frame 처리와 함께 검증 필요 | 적용 |
| D-24 | 자동복구 성공 종료 | 정상 응답 후 `receiveLoss_cnt`만 0으로 만들고 `repair_itself`, `repair_cnt`가 남을 수 있음 | 재페어링 이후 정상 응답과 pair 상태가 확인되면 자동복구 flag와 retry count를 함께 초기화 | main `save_response` 처리부, 2026-09-16 주석 | 복구 성공 후에도 다음 timeout이 repair 분기로 이어지는 상태 잔류 방지 | 성공 판정이 실제 정상 응답 처리 완료 시점에만 실행되는지 다중 Peer 시험 필요 | 적용, 시험 필요 |
| D-25 | 자동복구 최종 실패 정리 | backup, peer, pairing bit 중심의 초기화 | PairMask, TX/RX busy, RX transaction, pending event, state machine까지 채널 단위 정리 | `Wifi_Handle()` auto-repair 종료부 | 제거된 Peer의 이전 상태가 새 pairing에 섞이는 범위 감소 | Queue 자체에서 해당 채널 item을 즉시 제거하지는 않고 epoch로 무효화 | 적용 |
| D-26 | Peer 상태 LED | 온보드 LED로 pairing 유무를 단순 toggle | 16채널 Peer 상태, RSSI 수준, disconnect, pairing 상태를 외부 LED로 표시 | `peer_chk_led.*`, `led_drv_*`, `PairMask.h` | 장비를 열지 않고 채널별 통신 상태를 빠르게 식별 | LED 표시가 통신 판정의 원인은 아니며, threshold와 표시 의미에 대한 작업자 교육 필요 | 적용 |
| D-27 | LED 구동 방식 | 별도 Peer LED 없음 | RMT 기반 비동기 WS2812 전송, driver service task와 LED task를 Core 0에 정적 생성 | `led_drv_rmt.ino`, `peer_chk_led.ino`, setup | bit-banging으로 통신 loop를 길게 점유하는 문제를 줄이고 LED 기능을 통신부에서 분리 | 두 정적 task stack이 BSS를 증가시킴. 실제 stack high-watermark 측정 필요 | 적용, 자원 검증 필요 |
| D-28 | 진단 인터페이스 | 단순 `r/w` 중심 Serial debug | 읽기 중심 CLI: PDO in/out, pairing, fault, peer/state/cache, 장치별 요약, RX Queue 통계 | `include/ap_debug_cli_impl.h` | 현장 로그를 재현 가능한 명령으로 수집 가능 | 장치별 출력은 일부 raw 중심이며 TODO가 남음. 쓰기 명령은 현재 build에서 비활성 | 적용 |
| D-29 | 양산 로그 정책 | SYSTEM, ECAT, Wi-Fi, MONITOR 로그가 기본 활성 | SYSTEM만 기본 활성, 상세 로그와 장기 RF CSV는 기본 비활성 | `ap_config.h` | Serial 출력으로 인한 타이밍 교란과 로그 폭주 감소 | 장애 순간 세부 로그가 없을 수 있으므로 CLI·counter 수집 절차 필요 | 적용 |
| D-30 | 송신 callback·Semaphore | 네 개 binary semaphore를 동적 생성하고 send callback에서 `send_sem`을 give하지만 take 경로는 없음 | 사용되지 않는 semaphore 생성과 send callback 등록 제거 | setup, `wl_init()` | 불필요한 runtime allocation과 callback 실행 제거 | ESP-NOW delivery callback 결과를 직접 기록하지 않음. 응답 timeout만으로 실패를 판단 | 적용, 진단 공백 검토 |
| D-31 | 초기화 지연 | 초기화 후 고정 1초 delay | LED task를 준비하고 고정 1초 delay 제거 | setup | 부팅 완료 시간 단축 | 전원/ESC/Peer가 기존 1초 여유에 의존하지 않는지 전원 반복 시험 필요 | 적용, 시험 필요 |
| D-32 | Timing 계측 | 별도 test point 운용이 제한적 | GPIO14 test point 옵션 추가, 현재 `OSCILLOSCOPE=1` | `ap_config.h`, board I/O | loop/EtherCAT 구간을 계측할 수 있는 기반 | 양산에서 필요 없으면 release 설정에서 비활성화 여부 확정 필요 | 적용 |
| D-33 | RSSI/FW의 PDO 제공 | 기존 PDO에 별도 page 정의 없음 | W17을 이용한 RSSI/FW 표시 코드가 존재하지만 `PLC_RSSI=0` | `Ethercat_Handle()`, `ap_config.h` | 향후 진단 확장 기반 | 현재는 비활성이고 Peer FW version 저장 경로도 완성되지 않았으므로 V4.0 기능으로 보고하면 안 됨 | 미적용 |
| D-34 | 다중 Peer timeout | `cnt_loop`, `setTime`, `peer_req`가 AP 전체에서 하나이며 `rx_busy`는 채널별 | 구조가 그대로 유지되고, 현재 전역 gate도 꺼져 있어 다른 채널 송신이 가능 | `Wifi_Handle()`, `TEMP_QUEUE_GLOBAL_PEER_REQ_GATE=0` | 없음. V4.0의 현재 핵심 잔여 문제 | 늦은 응답이 다른 채널 request를 종료하거나, timeout 지연·누락·오귀속·잘못된 disconnect 누적 가능 | **미해결, 다중 Peer 양산 전 조치 필요** |
| D-35 | timeout 시간 기준 | 1 ms ISR이 boolean latch를 세우고 loop가 소비할 때 count 증가 | 기본 구조 유지 | `TimerHandler0()`, loop, `Wifi_Handle()` | V3.31 호환 동작 유지 | loop가 늦으면 tick가 누적되지 않으므로 20/35 count를 고정 20/35 ms로 해석하면 안 됨 | 미해결, 측정 필요 |

---

## 4. 보고용 변경 이력

> 아래 표는 소스 파일명과 내부 변수명을 제외하고, 기존 양산 버전 대비 변경 목적과 사업·품질 영향을 중심으로 정리하였다.

| No. | 수정사항 | 수정 이유 | 개선된 점 | 기존 상태를 유지했을 때 가능한 부작용 | 현재 판단 |
|---:|---|---|---|---|---|
| R-01 | 개발 구조 및 빌드 조건 표준화 | 개발 PC와 작업 방식에 따라 빌드 조건이 달라지는 문제를 줄이기 위해 | 동일한 도구·라이브러리 조건으로 재빌드하고 기능별 변경 범위를 추적하기 쉬워짐 | 인수인계 후 동일 소스를 빌드해도 결과가 달라지거나 수정 영향 파악이 어려울 수 있음 | 개선 완료, 로컬 라이브러리 경로 정리 필요 |
| R-02 | 무선 수신과 데이터 처리 분리 | 여러 Peer 응답이 짧은 시간에 들어올 때 수신 함수가 긴 처리를 직접 수행하는 부담을 줄이기 위해 | 수신 순간에는 데이터를 보관하고, 본 처리를 순서대로 수행하여 동시성 위험을 낮춤 | 수신 처리 중 다른 응답이 겹치면 상태와 cache 변경 순서가 꼬이거나 무선 task가 지연될 수 있음 | 적용 완료, 과부하 시험 필요 |
| R-03 | 수신 과부하 계측 추가 | 통신 단절이 무선 유실인지 내부 처리 포화인지 구분하기 위해 | 대기 수, 최대 적체, 정상 처리 수, 원인별 폐기 수를 현장에서 확인 가능 | 단절이 발생해도 원인을 확인하지 못하고 재현 의존적인 분석이 반복됨 | 적용 완료 |
| R-04 | 늦거나 잘못된 응답 차단 | 삭제한 Peer의 늦은 응답, 다른 종류의 응답, 다른 송신원의 응답이 현재 데이터를 덮지 않게 하기 위해 | 요청 종류와 Peer 상태가 일치하는 응답만 반영하는 범위가 확대됨 | 이전 응답이 새 상태로 오인되어 잘못된 cache, pairing 완료, timeout 해제가 발생할 수 있음 | 개선 완료, 동일 종류의 매우 늦은 응답은 protocol 한계 존재 |
| R-05 | 손상·비정상 프레임 방어 강화 | 잘못된 길이와 범위 값으로 메모리 영역을 잘못 접근하는 위험을 줄이기 위해 | 짧거나 큰 패킷, 잘못된 채널, 손상된 데이터가 상태를 변경하기 전에 차단됨 | 메모리 손상, 비정상 재부팅, 잘못된 장치 데이터 반영 가능 | 개선 적용. 데이터가 없는 정상 응답까지 차단하지 않는지 호환 시험 필요 |
| R-06 | 송신 대기열 동작 보정 | 대기 중인 요청이 busy 조건 때문에 실제 송신 전에 사라지거나 wrap 구간에서 덮이는 문제를 줄이기 위해 | 보낼 수 있는 상태가 확인된 뒤 요청을 제거하고, 가득 찬 상태를 명확하게 판정 | 일부 채널 요청이 이유 없이 누락되어 데이터 갱신 정지나 불규칙한 timeout이 발생 가능 | 개선 완료, 선두 정체 문제는 남음 |
| R-07 | 송신 실패 상태 정리 | 송신 시작 자체가 실패한 경우에도 응답 대기 상태가 남지 않도록 하기 위해 | 실패한 요청의 busy 상태를 즉시 해제하여 다음 통신 기회를 확보 | 실제로는 송신되지 않았지만 계속 응답을 기다려 해당 채널이 멈출 수 있음 | 개선 완료 |
| R-08 | I/O·Serial 범위 보호 | 잘못된 page 또는 channel 명령이 내부 저장 범위를 벗어나지 않게 하기 위해 | 유효한 범위만 cache와 통신 요청에 반영 | 다른 장치의 데이터가 손상되거나 예측하기 어려운 오동작·재부팅 가능 | 개선 완료 |
| R-09 | Serial Peer 요청 대상 보정 | 특정 조건에서 Serial 요청이 실제 대상이 아닌 첫 번째 채널 상태에 기록될 수 있는 문제를 해결하기 위해 | 요청한 Serial Peer 기준으로 읽기 동작이 예약됨 | Serial 데이터가 갱신되지 않거나 다른 채널의 상태가 영향을 받을 수 있음 | 개선 완료, WOS 연동 시험 필요 |
| R-10 | 자동 재연결 종료 조건 보강 | 재연결 성공 후에도 복구 중 상태가 남아 후속 timeout에서 다시 복구 절차로 들어가는 것을 막기 위해 | 정상 응답이 확인되면 복구 상태와 재시도 횟수가 정상 상태로 돌아옴 | 연결이 복구됐는데도 재연결·삭제가 반복되거나 현장 통신이 불안정해질 수 있음 | 코드 반영 완료, 다중 Peer 시험 필요 |
| R-11 | 명시적 삭제와 자동복구 구분 강화 | 사용자가 삭제한 장치가 자동복구 대상으로 남지 않게 하기 위해 | 의도한 삭제와 통신 단절에 의한 복구를 구분하여 처리 | 삭제한 장치가 다시 연결되거나 이전 상태가 남을 수 있음 | 개선 완료 |
| R-12 | Peer별 상태 LED 추가 | 현장에서 노트북과 로그 없이도 연결·단절·신호 상태를 빠르게 확인하기 위해 | 16개 채널의 연결 상태와 신호 수준을 육안으로 확인 가능 | 문제 채널을 찾기 위해 장비를 연결하고 긴 로그를 수집해야 함 | 기능 적용 완료, 표시 기준 교육 필요 |
| R-13 | LED 처리의 통신 경로 분리 | LED 갱신 때문에 주 통신 처리 시간이 길어지는 것을 방지하기 위해 | 전용 하드웨어와 별도 작업 단위로 LED를 갱신하여 통신 영향 축소 | LED 갱신 시점마다 통신 주기가 흔들릴 수 있음 | 개선 완료, 작업 여유 측정 필요 |
| R-14 | 현장 진단 명령 확대 | 통신 상태, cache, pairing, 오류 누적을 한 번에 확인하기 위해 | 재현이 어려운 장애에서도 정해진 명령으로 상태를 수집 가능 | 로그가 부족해 부품 교체나 재부팅 후에만 정상화 여부를 판단하게 됨 | 적용 완료, 일부 출력 보완 필요 |
| R-15 | 기본 로그 축소 | 연속 출력이 통신 주기와 현장 로그 가독성에 미치는 영향을 줄이기 위해 | 정상 운전 중 출력 부담 감소 | 상세 로그를 상시 켜두면 처리 시간 증가와 중요한 로그 유실 가능 | 적용 완료, 장애 수집 절차 필요 |
| R-16 | WOS 및 기존 Peer 호환 유지 | 이미 설치된 장비의 통신 규격을 변경하기 어렵기 때문에 | 기존 PDO와 무선 프레임을 유지하면서 AP 내부 안전성을 개선 | 프레임 규격을 변경하면 기존 현장 장비와 직접 호환되지 않음 | 호환 유지 |
| R-17 | 정적 메모리 사용 확대 | 운전 중 동적 메모리 실패와 단편화 가능성을 줄이기 위해 | 수신 대기열과 LED 작업 메모리가 부팅 시 고정되어 장기 운전 예측성이 향상 | 동적 할당 실패 시 간헐적 기능 정지 가능 | 방향 타당, RAM 여유 확인 필요 |
| R-18 | 다중 Peer timeout 구조 개선 필요 | 여러 장치의 요청 상태가 하나의 timeout 상태를 공유하면 장치 간 간섭이 발생할 수 있기 때문에 | 채널별로 바꾸면 한 장치의 지연·늦은 응답이 다른 장치의 실패 판정에 영향을 주지 않음 | timeout 누락·지연·잘못된 장치의 단절 누적·불필요한 삭제/재연결 가능 | **아직 미수정. 다중 Peer 양산 전 필수 검토** |

---

## 5. 빌드·메모리 비교

### 5.1 빌드 환경

| 항목 | V3.31 | V4.0 P0List | 비고 |
|---|---:|---:|---|
| ESP32 Arduino framework | 2.0.11 | 2.0.11 | 동일 계열 |
| ESP-IDF 기반 버전 | 4.4.5 | framework 2.0.11 기반 | 동일 계열 |
| Xtensa GCC | 8.4.0 | 8.4.0 | 동일 |
| CPU | 240 MHz | 240 MHz | 동일 |
| Flash | 4 MB, QIO, 80 MHz | 4 MB, QIO, 80 MHz | 동일 |
| Arduino loop core | Core 1 | Core 1 | 동일 설정 |
| Arduino event core | Core 1 | Core 1 | 동일 설정 |
| 프로젝트 도구 | Arduino IDE 계열 | PlatformIO | 관리 방식 변경 |

### 5.2 기존 산출물 기준 크기 비교

| 영역 | V3.31 산출물 | V4.0 P0List 산출물 | 증가량 | 증가율 |
|---|---:|---:|---:|---:|
| ELF text | 634,762 byte | 648,958 byte | +14,196 byte | +2.24% |
| ELF data | 119,624 byte | 123,396 byte | +3,772 byte | +3.15% |
| ELF bss | 23,985 byte | 39,617 byte | +15,632 byte | +65.17% |
| ELF 합계 | 778,371 byte | 811,971 byte | +33,600 byte | +4.32% |
| Application binary | 738,720 byte | 756,688 byte | +17,968 byte | +2.43% |

V4.0의 BSS 증가는 정적 RX Queue, 채널별 transaction/statistics, LED용 정적 task stack과 상태 구조가 주요 원인이다. 이는 runtime heap 의존도를 낮추는 대신 항상 점유하는 RAM을 늘리는 선택이다.

> 주의: V3.31 폴더의 binary/ELF 이름은 `ApToEthercat_V3.3`이고 생성 시각은 2025-09-09이다. 2025-09-19 소스와 산출물이 완전히 동일한 revision이라는 별도 서명 정보는 없다. 따라서 위 수치는 방향과 규모를 보는 참고값이며, 최종 양산 비교는 두 소스를 동일한 toolchain으로 clean build한 산출물로 다시 확정해야 한다.

---

## 6. 현재 V4.0의 기능 설정 상태

| 기능 | 현재 값 | 실동작 해석 | Release 결정 필요사항 |
|---|---:|---|---|
| 수신 Queue depth | 24 | 최대 24개 frame slot | burst 시험 후 확정 |
| 한 loop의 RX 처리 budget | 4 | loop 한 번에 최대 4 frame 처리 | EtherCAT 주기와 backlog 동시 측정 |
| 예상 응답 Command 검사 | ON | 요청과 다른 Command 응답 폐기 | 전체 Peer 호환 시험 |
| 일반 timeout 시 `rx_busy` 즉시 해제 | OFF | 기존처럼 최종 disconnect 조건까지 늦은 응답 허용 | 다중 Peer timeout 재설계와 함께 확정 |
| 전역 request gate | OFF | 대상 채널이 busy가 아니면 다른 채널도 송신 가능 | 채널별 timeout 적용 전에는 상태 혼선 위험 |
| pending event mode | OFF | callback worker가 기존 response flag를 직접 갱신 | 현 설정을 기준으로 회귀 시험 |
| Serial pairing-bit 자동 GET | ON | Serial Peer pairing bit가 켜진 경우 GET 예약 가능 | WOS polling 의도와 비교 |
| RSSI/FW PDO 출력 | OFF | 기존 PDO 호환 우선 | 기능 미완료 상태로 유지 또는 코드 제거 결정 |
| 상세 RF 장기 로그 | OFF | 정상 운전 로그 부담 없음 | 검증 firmware에서만 선택 사용 |
| Debug CLI | ON | 읽기 진단 사용 가능 | 양산 접근 절차 확정 |
| CLI PDO write | OFF | 진단 중 WOS 데이터 변경 차단 | 양산은 OFF 유지 권고 |
| RMT LED driver | ON | 외부 Peer LED 사용 | LED 없는 HW variant 확인 |
| Oscilloscope test point | ON | GPIO14 timing 계측 기능 포함 | 양산 pin 충돌 여부 확인 후 결정 |

---

## 7. 현재 남은 양산 전 항목

| 우선순위 | 항목 | 현재 위험 | 권고 방향 | wire protocol 변경 여부 |
|---|---|---|---|---|
| P0 | 다중 Peer timeout 소유권 | timeout count, 제한값, request flag가 AP 전체에서 하나라 채널별 응답 상태와 불일치 | 채널별 active/deadline/request 종류를 보유하고 응답·timeout이 해당 채널만 종료하도록 변경 | 없음 |
| P0 | timeout과 `rx_busy` 종료 정책 | 늦은 응답 허용을 위해 busy를 유지하지만, 전역 timeout과 결합하면 채널이 오래 잠길 수 있음 | 채널별 deadline 적용 후 timeout owner의 transaction과 busy만 정리 | 없음 |
| P1 | 동일 Command 늦은 응답 | wire sequence가 없어 같은 Peer의 이전 IO GET 응답과 새 IO GET 응답을 완전히 식별 불가 | 채널당 동시 요청 1개 보장, guard time/요청 수명 정책 검토 | sequence 추가 시에는 있음. 현재는 추가 불가 조건 |
| P1 | 송신 Queue 선두 정체 | 선두 채널이 busy이면 뒤 채널이 준비되어도 전송 불가 | 제한된 skip/rotation 또는 채널별 pending bit 구조 검토 | 없음 |
| P1 | ESP-NOW delivery status 관측 | 현재 송신 callback을 등록하지 않아 링크 계층 성공/실패 통계 없음 | callback에서는 최소 counter만 기록하고 응답 timeout과 분리해 관측 | 없음 |
| P1 | 공통 최소 수신 길이 10 byte | payload가 없는 8 byte 정상 응답까지 처리 전에 폐기할 가능성. Pairing Cancel이 우선 확인 대상 | 공통 header+CRC 최소값과 Command별 payload 최소값을 분리해 검증 | 없음 |
| P1 | 장치별 protocol 회귀 | 엄격한 Command/MAC/길이 검증이 기존 비표준 Peer 응답을 거부할 가능성 | DIW, CDA, S-Damper, D40A, D4SL, IFC/LFC, Manometer, Level Sensor 전수 시험 | 없음 |
| P2 | 정적 task stack 적정성 | LED task 8,000, driver task 2,048의 실제 여유 미측정 | stack high-watermark 측정 후 축소 또는 근거 기록 | 없음 |
| P2 | 장기 counter 정책 | 통계 누적값의 clear/overflow 정책 미정 | CLI clear 명령 또는 saturating counter 검토 | 없음 |
| P2 | 빌드 warning 정리 | Timer library의 의도적 warning과 `LOW`/`HIGH` 재정의 warning이 남아 실제 신규 warning 식별을 방해 | 공통 헤더의 중복 macro 제거 및 허용 warning 기준 기록 | 없음 |
| P2 | 소스 주석 인코딩 | 기존 한글 주석 일부가 깨져 인수인계 가독성이 낮음 | 기능 변경과 분리하여 UTF-8 정리 | 없음 |
| P2 | 미완성·비활성 기능 정리 | RSSI/FW PDO, pending mode 등 시험 코드가 release source에 공존 | release 설정표를 고정하고 불필요한 분기 제거 | 선택에 따라 다름 |

---

## 8. 권장 검증 순서

| 단계 | 검증 내용 | 합격 기준 |
|---:|---|---|
| 1 | 동일 toolchain clean build 및 binary/ELF 크기 기록 | warning/error 검토, Flash/RAM 여유 기록 |
| 2 | Peer 1대 기본 회귀 | Pairing, IO GET/SET, Serial GET/SET, 명시적 삭제, 자동복구 정상 |
| 3 | 장치 종류별 회귀 | 각 장치의 DI/DO/SI/SO 의미와 page 수에 맞게 WOS 값 일치 |
| 4 | 다중 Peer 정상 부하 | 2/4/8/16대에서 cache 갱신, Queue high-watermark, scan 주기 측정 |
| 5 | 응답 유실·지연 주입 | 유실 채널만 timeout/복구되고 다른 채널의 disconnect count가 증가하지 않음 |
| 6 | 삭제 직후 늦은 응답 | 삭제된 Peer frame이 cache·pairing 상태를 되살리지 않음 |
| 7 | 재페어링 성공 | 최초 정상 응답 후 복구 flag와 retry count가 0으로 유지됨 |
| 8 | RX burst/Queue full 시험 | Queue full 발생 여부와 drop 후 상위 timeout 복구 확인 |
| 9 | 24시간 이상 장기 운전 | Queue 적체, counter 증가, heap 감소, task stack 부족, 재부팅 없음 |
| 10 | 전원 반복·WOS 재연결 | Alias, PDO, Peer 상태가 매 부팅 동일하게 복구됨 |

### 8.1 현재 체크아웃 빌드 확인

| 항목 | 결과 |
|---|---|
| 실행일 | 2026-09-16 |
| 명령 | `pio run -e ap_v4_0_20260528` |
| 결과 | SUCCESS |
| RAM | 63,088 / 327,680 byte, 19.3% |
| Flash | 750,941 / 1,310,720 byte, 57.3% |
| 확인된 warning | `ESP32TimerInterrupt`의 `#warning USING_ESP32_TIMERINTERRUPT`; `common.h`의 `LOW`, `HIGH` macro 재정의 |

빌드 성공은 소스와 현재 toolchain의 컴파일·링크 가능성만 입증한다. 다중 Peer timeout, 늦은 응답, Queue full, 자동복구 및 장치별 protocol 호환성은 실제 AP/WOS/Peer를 사용한 시험으로 별도 입증해야 한다.

---

## 9. 결론

V4.0 P0List는 V3.31의 WOS PDO와 AP-Peer 무선 protocol을 유지하면서, 수신 동시성·프레임 검증·Queue 정확성·복구 상태 정리·현장 진단성을 크게 보강하였다. 따라서 기존 설치 장비와의 호환성을 유지한 내부 안정화 방향은 타당하다.

다만 현재 코드는 다중 Peer의 응답 대기 상태는 채널별인데 timeout의 소유권은 전역인 혼합 구조다. 이 항목은 V4.0 변경 이력상 "개선 완료"가 아니라 "양산 전 미해결"로 관리해야 한다. 다음 변경에서는 wire protocol을 바꾸지 않고 채널별 request context와 deadline을 도입하는 방안을 우선 검토한다.

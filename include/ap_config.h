#pragma once

#include <Arduino.h>

#define RF_TEST_SERIAL_LOG      0       // 0=disable and restore normal serial output, 1=long-run RF CSV log.

#define DEBUG_SERIAL_SYSTEM     1       // 시스템 공통 Serial 디버그 로그 사용 여부.
#define DEBUG_SERIAL_ECAT       0       // EtherCAT/PLC 데이터 경로 Serial 디버그 로그 사용 여부.
#define DEBUG_SERIAL_WIFI       0       // ESP-NOW 패킷/상태 Serial 디버그 로그 사용 여부.
#define DEBUG_SERIAL_MONITOR    0       // 런타임 모니터링/타임아웃 Serial 디버그 로그 사용 여부.
#define DEBUG_SERIAL_PEER       0       // Peer 페어링/재시도 Serial 디버그 로그 사용 여부.

#define TEMP_SERIAL_TIMEOUT_EXTEND 1    // TEMP: serial GET/SET 응답 대기 count만 임시 확장.
#define TEMP_SERIAL_SET_TIME_PAIRD 20  // TEMP: S-Damper serial 응답 지연 확인용 paired timeout count.

#define TEMP_CLEAR_RX_BUSY_ON_REQ_TIMEOUT 0 
// 0: 일반 timeout 후에도 rx_busy/transaction을 유지하여 늦은 응답을 허용.
// 1: 일반 timeout 즉시 transaction과 rx_busy를 해제하여 늦은 응답을 폐기.
#define TEMP_RX_EVENT_PENDING_MODE 0        //! TEMP: 0=legacy callback sets response flags, 1=current pending event consume. // pending 키면 너무 필터링 함 0으로 원복 
//* 위 pending도 완전한 해결책은 아닌듯? event를 남기기전에 cb에서 cache 직접 변경한느 
 
#define TEMP_QUEUE_GLOBAL_PEER_REQ_GATE 0   
// 0: V3.31 호환 방향. 전역 peer_req를 다음 송신의 Gate로 사용하지 않음.
// 1: AP 전체에서 한 번에 하나의 request만 응답/timeout 완료까지 허용.

#define TEMP_SERIAL_PAIRBIT_AUTO_GET 1      //! TEMP: 0=disable paired serial pairing_bit auto GET, 1=current auto GET.

// ESP-NOW RX callback -> Core 1 frame worker.
// 16개 channel의 정상 응답 1개씩과 짧은 duplicate burst를 수용하도록 24개로 고정한다.
#define WIFI_RX_QUEUE_DEPTH              24
#define WIFI_RX_SERVICE_BUDGET           4
#define WIFI_RX_STRICT_EXPECTED_COMMAND  1


#define AP_CLI                  1       //! CLI 기능 사용 여부

#define FUNC_PAIRED_VERIFY      1       // 동작 중 paired peer 정보 검증 기능 사용 여부.
#define FUNC_REPAIRD_AUTO       1       // peer loss 이후 자동 repair pairing 기능 사용 여부.

#if FUNC_REPAIRD_AUTO == 1
#define SETUP_REPAIR_MAX        10      // 자동 repair request 최대 시도 횟수.
#define SETUP_RECEVIELOSS_MAX   19      // peer backup/status를 정리하기 전 receive-loss 허용 횟수.
#endif

#define SETUP_DISCONNECT_MAX    10      // disconnect/repair 처리로 넘어가기 전 연속 request timeout 허용 횟수.

#define DIW_FLOW                0x01    // Peer 장치 type ID.
#define CDA_FLOW                0x02    // Peer 장치 type ID.
#define SMART_DAMPER            0x03    // Peer 장치 type ID.
#define X_RAY                   0x04    // Peer 장치 type ID.
#define D40A                    0x05    // Peer 장치 type ID.
#define D4SL                    0x06    // Peer 장치 type ID.
#define TIC                     0x07    // Peer 장치 type ID.
#define LMFC                    0x08    // Peer 장치 type ID.
#define MANOMETER               0x09    // Peer 장치 type ID.
#define LCT                     0x0A    // Peer 장치 type ID.

#define AP_PAIRING_REQ          0x01    // AP 명령: peer pairing 요청.
#define AP_PAIRING_CANCEL       0x02    // AP 명령: peer pairing 취소/삭제 요청.
#define AP_IO_GET               0x03    // AP 명령: peer IO 데이터 읽기 요청.
#define AP_IO_SET               0x04    // AP 명령: peer IO 데이터 쓰기 요청.
#define AP_SERIAL_SET           0x05    // AP 명령: peer serial 데이터 쓰기 요청.
#define AP_SERIAL_GET           0x06    // AP 명령: peer serial 데이터 읽기 요청.

#define PEER_PAIRING_OK         0x81    // Peer 응답: pairing 요청 정상 처리.
#define PEER_PAIRING_CANCEL     0x82    // Peer 응답: pairing 취소/삭제 정상 처리.
#define PEER_IO_GET             0x83    // Peer 응답: IO 데이터 반환.
#define PEER_IO_SET             0x84    // Peer 응답: IO 쓰기 완료.
#define PEER_SERIAL_SET         0x85    // Peer 응답: serial 쓰기 완료.
#define PEER_SERIAL_GET         0x86    // Peer 응답: serial 데이터 반환.

#define ALIAS_REG               0x0012  // EtherCAT alias register 기준 word.
#define ALIAS_REG_H             0x0012  // EtherCAT alias register high word.
#define ALIAS_REG_L             0x0013  // EtherCAT alias register low word.

#define SW                      0       // 로터리/setup 스위치 입력 핀.
#define LED                     2       // 보드 상태 LED 핀.
#define MUXInput1               36      // 16채널 MUX 입력 핀.
#define NRESET                  4       // 외부 reset 출력 핀.
#define MUX_16EN1               32      // 16채널 MUX enable 핀.
#define MUX_SEL0                27      // MUX 주소 선택 bit 0.
#define MUX_SEL1                26      // MUX 주소 선택 bit 1.
#define MUX_SEL2                33      // MUX 주소 선택 bit 2.
#define MUX_SEL3                25      // MUX 주소 선택 bit 3.

#define PLC_RSSI                0       // V4.0 compatibility: do not write RSSI/FW version into W17/data16.
#define PLC_PAIRING_STATUS      1       // pairing status bitfield를 PLC 데이터 영역에 반영할지 여부.

#define MONITOR_WORD_IDX        15      // AP/peer 상태를 표시하는 PLC monitor word index.
#define FW_VERSION_INIT         0x03FF  // firmware version 미확인 시 기본값.
#define RSSI_INIT               0x03    // RSSI 미갱신 시 기본 상태값.
#define AP_VER                  400u    // AP firmware version 값.
#define AP_VER_10BIT            ((uint16_t)(AP_VER & 0x03FFu)) // AP version을 10bit로 제한한 값.

#define OSCILLOSCOPE            1       // 타이밍 측정용 test point 핀 toggle 사용 여부.

#if OSCILLOSCOPE == 1
#define TEST_POINT              14      // 오실로스코프 측정용 GPIO 핀.
#define TP_HIGH()               digitalWrite(TEST_POINT, HIGH) // test point HIGH 출력.
#define TP_LOW()                digitalWrite(TEST_POINT, LOW)  // test point LOW 출력.
#endif

#define SET_TIME_PAIRING        35      // Wifi_Handle()에서 사용하는 pairing request timeout 기준 count.
#define SET_TIME_PAIRD          20      // Wifi_Handle()에서 사용하는 paired peer 일반 request timeout 기준 count.

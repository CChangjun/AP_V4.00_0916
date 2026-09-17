#pragma once
#include <Arduino.h>
#include <esp_now.h>
#include <atomic>
#include "ap_types.h"

// Wi-Fi runtime type은 ap_wifi_runtime.h에서 관리한다.
// 이 파일은 AP 공용 context와 전역 context 선언만 제공한다.

typedef struct _ap_peer_ctx_t
{
    peer_t peer[MAX_PEER];              // channel별 현재 paired peer table.
    peer_bak_t peer_bak[MAX_PEER];      // delete/repair 흐름에서 사용하는 backup peer table.
    peer_debug_t peer_debug[MAX_PEER];  // channel별 디버그 counter/latch.
    peer_packet_t peer_packet[MAX_PEER]; // channel별 마지막 packet 요약 정보.
    int rssi[MAX_PEER];                 // channel별 마지막 RSSI 값.
} ap_peer_ctx_t;

typedef struct _ap_data_ctx_t
{
    uint8_t pairing_buffer_cnt;         // 처리 대기 중인 pairing buffer entry 수.
    uint16_t rx_pairing_status;         // PLC로 내보내는 peer pairing status bitfield.
    //! 기존 문제; serial device 전송 시,IPeerData를 사용하여 I/O 1word를 보내기에, ch12~14 serial IO word 접근까지 포함.
    uint16_t lPeerData[MAX_PEER][7]; // IO/serial 공용 1word cache.
    uint16_t lSPeerData[MAX_SERIAL_PEER + 1][81]; // Serial data cache. V4.0 keeps serial ch1~ch4 plus legacy spare.

} ap_data_ctx_t;

typedef struct _ap_board_ctx_t
{
    uint16_t iAddr;                     // Rotary/MUX로 읽은 board address 값.
    uint8_t iAddr1;                     // board address low byte.
    uint8_t iAddr2;                     // board address high byte.
} ap_board_ctx_t;

typedef struct _ap_os_ctx_t //! 얘는 미정, 구현만 해놓음
{
    portMUX_TYPE pMUX;                  // ISR/critical section 보호용 mux.
    SemaphoreHandle_t send_sem;         // send 경로 semaphore.
    SemaphoreHandle_t recv_sem;         // receive 경로 semaphore.
    SemaphoreHandle_t tx_sem;           // TX semaphore.
    SemaphoreHandle_t rx_sem;           // RX semaphore.
    sys_tick_t gSysTick;                // 주기 tick counter와 flag.
    unsigned long preTime;              // 경과 시간 계산용 이전 timestamp.
    unsigned long pTime;                // 경과 시간 계산용 현재/작업 timestamp.
} ap_os_ctx_t;

// wifi runtime은 1차 단계에서는 비워 두고, 다음 단계에서 옮rlf 예정ㅇ
typedef struct _ap_wifi_ctx_t
{
    esp_now_peer_info_t peerInfo;       // ESP-NOW peer 등록 정보.
    uint8_t broadcast_addr[6];          // ESP-NOW broadcast MAC address.
    bool pairing_req_flag;              // 전역 pairing request flag.
    bool pairing_init_flag;             // pairing 초기화 flag.
} ap_wifi_ctx_t;

typedef struct _ap_app_ctx_t
{
    ap_peer_ctx_t peer;                 // peer 관련 runtime context.
    ap_data_ctx_t data;                 // PLC/AP 공유 데이터 context.
    ap_board_ctx_t board;               // board IO/address context.
    ap_os_ctx_t os;                     // OS/tick/synchronization context.
    ap_wifi_ctx_t wifi;                 // ESP-NOW setup/runtime context.
} ap_app_ctx_t;

extern ap_app_ctx_t g_ap;

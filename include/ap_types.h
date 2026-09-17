#pragma once
#include <Arduino.h>

// 기존 main.cpp의 MAX 계열 상수를 공통 type header에서 관리한다.
#define MAX_PEER 16             // IO/serial peer를 포함한 전체 peer channel 수.
#define MAX_IO_BUFF 4           // PLC/AP 데이터 교환에 사용하는 IO buffer 수.
#define MAX_SERIAL_BUFF 70      // Serial 데이터 buffer word 수.
#define MAX_SERIAL_PEER 4       // V4.0 compatibility: serial peer channels ch12~ch15.
#define MAX_IO_PEER 12          // IO peer channel 수.
#define MAX_PAIRING_BUFF_SIZE 16 // Pairing buffer entry 수.

typedef struct _peer_t
{
    uint8_t mac[6];             // Peer ESP-NOW MAC address.
    uint16_t typeAddr;          // peer type/address를 합친 값. high byte=type, low byte=address.
    bool pairFlag;              // 해당 channel이 paired 상태이면 true.
    uint8_t io_usage;           // peer가 보고/설정한 IO 사용 mode.
    uint8_t io_page;            // IO page 수 또는 현재 page 정보.
    uint8_t serial;             // 해당 peer의 serial 기능/status flag.
} peer_t;

typedef struct _peer_bak_t
{
    uint8_t mac[6];             // delete/repair 흐름에서 사용하는 backup MAC.
    uint16_t typeAddr;          // auto-repair pairing에 사용하는 backup type/address.
    uint8_t io_usage;           // backup IO 사용 mode.
    uint8_t io_page;            // backup IO page 정보.
    uint8_t serial;             // backup serial 기능/status.
    uint8_t repair_cnt;         // auto-repair 재시도 counter.
    uint8_t del_cnt;            // peer delete counter.
    bool repair_itself;         // AP가 자동 repair pairing을 시도 중이면 true.
    bool del_set;               // delete 처리 요청/set 상태이면 true.
    uint8_t receiveLoss_cnt;    // repair/final clear 판단에 사용하는 연속 receive-loss counter.
    uint32_t wait;              // peer backup 흐름의 wait/time 관리값.
} peer_bak_t;

typedef struct _peer_debug_t
{
    uint8_t channel;            // 디버그 대상 channel index.
    uint32_t zero;              // zero/empty 상태 관찰용 디버그 counter.
    uint32_t del_cnt;           // delete event 디버그 counter.
    uint32_t wait;              // wait 상태 디버그 counter.
    bool paired;                // paired 상태 관찰용 디버그 latch.
    uint32_t retry_pair_cnt;    // pairing retry event 디버그 counter.
} peer_debug_t;

typedef struct _peer_packet_t
{
    bool paired;                // monitor/PLC 경로로 내보내는 packet 기준 paired 상태.
    int8_t rssi;                // peer packet에서 얻은 마지막 RSSI 값.
    uint16_t fw_version;        // packet에서 읽은 peer firmware version.
    uint8_t peer_channel;       // packet에 포함된 peer channel.
} peer_packet_t;

typedef struct _sys_tick_t
{
    bool f_ms1;                 // loop()에서 소비하는 1ms ISR tick latch.
    uint16_t cnt_ms2;           // ms2 flag 생성용 divider counter.
    uint16_t cnt_ms5;           // ms5 flag 생성용 divider counter.
    uint16_t cnt_ms10;          // ms10 flag 생성용 divider counter.
    uint16_t cnt_ms25;          // ms25 flag 생성용 divider counter.
    uint16_t cnt_ms50;          // ms50 flag 생성용 divider counter.
    uint16_t cnt_ms100;         // ms100 flag 생성용 divider counter.
    uint16_t cnt_ms250;         // ms250 flag 생성용 divider counter.
    uint16_t cnt_ms500;         // ms500 flag 생성용 divider counter.
    uint16_t cnt_ms750;         // ms750 flag 생성용 divider counter.
    uint16_t cnt_sec1;          // sec1 flag 생성용 divider counter.

    union
    {
        uint16_t wf;
        struct
        {
            uint16_t ms2   : 1; // 주기 task flag.
            uint16_t ms5   : 1; // 주기 task flag.
            uint16_t ms10  : 1; // 주기 task flag.
            uint16_t ms25  : 1; // 주기 task flag.
            uint16_t ms50  : 1; // 주기 task flag.
            uint16_t ms100 : 1; // 주기 task flag.
            uint16_t ms250 : 1; // 주기 task flag.
            uint16_t ms500 : 1; // 주기 task flag.
            uint16_t ms750 : 1; // 주기 task flag.
            uint16_t sec1  : 1; // 주기 task flag.
            uint16_t sec2  : 1; // 예약된 주기 task flag.
            uint16_t sec3  : 1; // 예약된 주기 task flag.
            uint16_t sec4  : 1; // 예약된 주기 task flag.
            uint16_t sec5  : 1; // 예약된 주기 task flag.
            uint16_t sec10 : 1; // 예약된 주기 task flag.
            uint16_t min1  : 1; // 예약된 주기 task flag.
        } bf;
    } flag;
} sys_tick_t;

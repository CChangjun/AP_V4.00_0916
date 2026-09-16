#pragma once

#include <Arduino.h>
#include "esp_wifi.h"
#include "AP_SLAVE.h"
#include "ap_types.h"
#include "wifi_queue.h"

#define WIFI_SEND_BUF_MAX WIFI_PACKET_MAX // ESP-NOW TX payload buffer 최대 크기.

typedef struct _wifi_state_machine_t
{
    struct
    {
        bool request;           // 해당 채널의 pairing request 대기 상태.
        bool response;          // pairing response 수신 후 처리 대기 상태.
        bool add;               // pairing add 흐름 진행 중.
        bool del;               // pairing delete/cancel 흐름 진행 중.
        bool state;             // peer response에서 받은 pairing 결과 latch.

        uint8_t retry;          // pairing 재시도 counter.
        uint16_t timeout;       // pairing state timeout counter.
    } pairing;

    struct
    {
        bool rw;                // peer transaction read/write 구분용 예약 flag.
        bool response;          // 일반 peer response 수신 후 저장 처리 대기 상태.
        bool response_type_serial; // 마지막 일반 response가 serial 데이터인지 표시.
        bool update_io;         // PLC IO 출력 변경으로 AP_IO_SET 전송 필요.
        bool update_serial;     // PLC serial 출력 변경으로 AP_SERIAL_SET 전송 필요.
        bool request_serial;    // serial 데이터 읽기 요청으로 AP_SERIAL_GET 전송 필요.

        uint8_t state;          // 해당 peer의 WIFI_STATE_MACHINE 상태.
        uint8_t *pMac;          // esp_now_send()에 사용하는 peer MAC 포인터.
        uint8_t device_type;    // typeAddr high byte에서 추출한 peer 장치 type.
        uint8_t device_addr;    // typeAddr low byte에서 추출한 peer 장치 address.
        uint8_t channel;        // packet에 기록되는 AP peer channel index.
        uint8_t send_cmd;       // 해당 peer에 보낼 마지막/다음 AP command.
        uint8_t txBuf[WIFI_SEND_BUF_MAX]; // ESP-NOW TX packet buffer.
        uint16_t txLen;         // txBuf에서 실제 전송할 byte 길이.
        uint8_t repair_counter; // peer state 내부 repair counter 예약 필드.
        uint8_t repair_state;   // peer state 내부 repair state 예약 필드.
        uint32_t repair_timeout; // peer state 내부 repair timeout 예약 필드.

        uint16_t set_io_data;   // peer로 보낼 IO 출력 word 또는 response 비교 기준값.
        uint16_t set_serial_Buf[WIFI_SEND_BUF_MAX]; // peer로 보낼 serial 출력 word buffer.
        uint16_t bak_send;      // 마지막 전송 데이터 word backup.
        uint32_t cnt_disconnect; // disconnect 판단용 연속 request timeout counter.
    } peer;

} wifi_state_machine_t;

typedef struct _wifi_send_t
{
    wifi_queue_t queue;         // ESP-NOW 전송 대기 peer channel FIFO.

    bool tx_busy[MAX_PEER];     // 채널별 송신 진행 중 flag.
    bool rx_busy[MAX_PEER];     // 송신 성공 후 채널별 응답 대기 flag.

    uint8_t peer_addr;          // 현재 round-robin 대상 peer channel.
    bool peer_req;              // AP request cycle 활성 flag. 현재 구조에서는 한 번에 하나만 true.

    bool doing_recv_cb;         // Core 1 RX frame worker가 기존 packet 처리 본문을 실행 중인지 표시.
    bool doing_handle;          // Wifi_Handle() 실행 중인지 표시.

    uint8_t paired_cnt;         // 현재 paired 상태인 peer 수.

    uint8_t rf_set_channel;     // 설정된 WiFi primary channel.
    uint8_t rf_get_channel;     // runtime/readback WiFi primary channel.
    uint16_t board_version;     // AP board/firmware version word.
    uint8_t rf_set_group;       // AP/peer packet filter에 사용하는 RF group ID.

    wifi_second_chan_t rf_sencond_channel; // 설정된 WiFi secondary channel.

} wifi_send_t;

typedef struct _wifi_rx_event_t
{
    volatile bool pending;      // RX frame worker가 Wifi_Handle()에 넘길 event가 있음을 표시.
    volatile bool pairing_response; // event가 pairing response임을 표시.
    volatile bool normal_response; // event가 일반 IO/serial response임을 표시.
    volatile bool serial_response; // event가 serial response data임을 표시.
    volatile bool pairing_cancel; // event가 pairing cancel response임을 표시.
    volatile uint8_t cmd;       // RX frame worker가 캡처한 response command byte.
} wifi_rx_event_t;

#pragma once

#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "AP_SLAVE.h"
#include "ap_types.h"
#include "ap_config.h"

// recv_cb()에서 복사한 한 개의 ESP-NOW frame과, 수신 순간의 AP transaction 문맥이다.
// 문맥 세대값을 함께 저장하여 peer 삭제/재요청 후 처리되는 늦은 frame을 폐기한다.
typedef struct _wifi_rx_frame_t
{
    uint32_t peer_epoch;
    uint32_t request_epoch;
    TickType_t received_tick;
    uint8_t src_mac[6];
    uint8_t data[WIFI_PACKET_MAX];
    uint8_t len;
    uint8_t channel;
    uint8_t expected_response_cmd;
} wifi_rx_frame_t;

typedef enum _wifi_rx_drop_reason_t
{
    WIFI_RX_DROP_INVALID_FRAME = 0,
    WIFI_RX_DROP_QUEUE_FULL,
    WIFI_RX_DROP_STALE_TRANSACTION,
    WIFI_RX_DROP_SOURCE_MAC,
    WIFI_RX_DROP_UNEXPECTED_COMMAND,
    WIFI_RX_DROP_PROTOCOL,
    WIFI_RX_DROP_CRC,
    WIFI_RX_DROP_REASON_COUNT
} wifi_rx_drop_reason_t;

typedef struct _wifi_rx_queue_stats_t
{
    uint32_t enqueued;
    uint32_t dequeued;
    uint32_t accepted;
    uint32_t dropped[WIFI_RX_DROP_REASON_COUNT];
    uint32_t dropped_by_channel[MAX_PEER];
    UBaseType_t waiting;
    UBaseType_t high_watermark;
} wifi_rx_queue_stats_t;

bool wifi_rx_queue_init(void);
bool wifi_rx_queue_push(const uint8_t *src_mac, const uint8_t *data, int len);
bool wifi_rx_queue_pop(wifi_rx_frame_t *frame);

// AP request의 생명주기 관리. begin 이후 같은 세대의 첫 정상 응답만 consume된다.
void wifi_rx_transaction_begin(uint8_t channel, uint8_t request_cmd);
void wifi_rx_transaction_abort(uint8_t channel);
void wifi_rx_peer_invalidate(uint8_t channel);
bool wifi_rx_frame_is_current(const wifi_rx_frame_t *frame);
bool wifi_rx_transaction_consume(const wifi_rx_frame_t *frame);

void wifi_rx_queue_note_accepted(uint8_t channel);
void wifi_rx_queue_note_drop(wifi_rx_drop_reason_t reason, uint8_t channel);
void wifi_rx_queue_get_stats(wifi_rx_queue_stats_t *stats);


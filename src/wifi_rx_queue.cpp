#include "wifi_rx_queue.h"

#include <string.h>

static_assert(WIFI_RX_QUEUE_DEPTH >= MAX_PEER,
              "RX queue must hold at least one response per peer channel");
static_assert(WIFI_RX_SERVICE_BUDGET > 0,
              "RX service budget must be greater than zero");

typedef struct _wifi_rx_transaction_t
{
    uint32_t peer_epoch;
    uint32_t request_epoch;
    uint8_t expected_response_cmd;
    bool active;
} wifi_rx_transaction_t;

static StaticQueue_t s_rx_queue_control;
static uint8_t s_rx_queue_storage[WIFI_RX_QUEUE_DEPTH * sizeof(wifi_rx_frame_t)];
static QueueHandle_t s_rx_queue = nullptr;
static portMUX_TYPE s_rx_guard = portMUX_INITIALIZER_UNLOCKED;
static wifi_rx_transaction_t s_transaction[MAX_PEER] = {};
static wifi_rx_queue_stats_t s_stats = {};

static uint8_t wifi_rx_expected_response(uint8_t request_cmd)
{
    switch (request_cmd)
    {
    case AP_PAIRING_REQ:    return PEER_PAIRING_OK;
    case AP_PAIRING_CANCEL: return PEER_PAIRING_CANCEL;
    case AP_IO_GET:         return PEER_IO_GET;
    case AP_IO_SET:         return PEER_IO_SET;
    case AP_SERIAL_SET:     return PEER_SERIAL_SET;
    case AP_SERIAL_GET:     return PEER_SERIAL_GET;
    default:                return 0;
    }
}

static uint8_t wifi_rx_channel_from_raw(const uint8_t *data, int len)
{
    if ((data == nullptr) || (len <= WIFI_PACKET_CHANNEL)) return 0xFF;
    return data[WIFI_PACKET_CHANNEL];
}

bool wifi_rx_queue_init(void)
{
    portENTER_CRITICAL(&s_rx_guard);
    memset(s_transaction, 0, sizeof(s_transaction));
    memset(&s_stats, 0, sizeof(s_stats));
    for (uint8_t channel = 0; channel < MAX_PEER; ++channel)
    {
        // 0은 초기화되지 않은 snapshot과 구분하기 위해 사용하지 않는다.
        s_transaction[channel].peer_epoch = 1;
        s_transaction[channel].request_epoch = 1;
    }
    portEXIT_CRITICAL(&s_rx_guard);

    if (s_rx_queue == nullptr)
    {
        s_rx_queue = xQueueCreateStatic(
            WIFI_RX_QUEUE_DEPTH,
            sizeof(wifi_rx_frame_t),
            s_rx_queue_storage,
            &s_rx_queue_control);
    }
    else
    {
        xQueueReset(s_rx_queue);
    }

    return s_rx_queue != nullptr;
}

//enQueue
bool wifi_rx_queue_push(const uint8_t *src_mac, const uint8_t *data, int len)
{
    const uint8_t channel = wifi_rx_channel_from_raw(data, len);

    if ((s_rx_queue == nullptr) || (src_mac == nullptr) || (data == nullptr) ||
        (len <= 0) || (len > WIFI_PACKET_MAX) || (channel >= MAX_PEER))
    {
        wifi_rx_queue_note_drop(WIFI_RX_DROP_INVALID_FRAME, channel);
        return false;
    }

    wifi_rx_frame_t frame = {};
    frame.len = (uint8_t)len;
    frame.channel = channel;
    frame.received_tick = xTaskGetTickCount();
    memcpy(frame.src_mac, src_mac, sizeof(frame.src_mac));
    memcpy(frame.data, data, frame.len);

    portENTER_CRITICAL(&s_rx_guard);
    frame.peer_epoch = s_transaction[channel].peer_epoch;
    frame.request_epoch = s_transaction[channel].request_epoch;
    frame.expected_response_cmd = s_transaction[channel].active
                                    ? s_transaction[channel].expected_response_cmd
                                    : 0;
    portEXIT_CRITICAL(&s_rx_guard);

    // 삭제된 peer의 늦은 응답이나 AP가 요청하지 않은 unsolicited frame은 Queue를 점유시키지 않는다.
    if (frame.expected_response_cmd == 0)
    {
        wifi_rx_queue_note_drop(WIFI_RX_DROP_STALE_TRANSACTION, channel);
        return false;
    }

    // ISR용 API가 아닌 non-blocking send를 사용해쑈음 
    if (xQueueSend(s_rx_queue, &frame, 0) != pdPASS)
    {
        wifi_rx_queue_note_drop(WIFI_RX_DROP_QUEUE_FULL, channel);
        return false;
    }
    //uxQueueMessagesWaiting ← 대기중인 메시지 개수 반환 
    const UBaseType_t waiting = uxQueueMessagesWaiting(s_rx_queue);
    portENTER_CRITICAL(&s_rx_guard);
    ++s_stats.enqueued;
    if (waiting > s_stats.high_watermark) s_stats.high_watermark = waiting;
    portEXIT_CRITICAL(&s_rx_guard);
    return true;
}

//deQueue
bool wifi_rx_queue_pop(wifi_rx_frame_t *frame)
{
    if ((s_rx_queue == nullptr) || (frame == nullptr)) return false;
    if (xQueueReceive(s_rx_queue, frame, 0) != pdPASS) return false;

    portENTER_CRITICAL(&s_rx_guard);
    ++s_stats.dequeued;
    portEXIT_CRITICAL(&s_rx_guard);
    return true;
}

void wifi_rx_transaction_begin(uint8_t channel, uint8_t request_cmd)
{
    if (channel >= MAX_PEER) return;

    portENTER_CRITICAL(&s_rx_guard);
    ++s_transaction[channel].request_epoch;
    s_transaction[channel].expected_response_cmd = wifi_rx_expected_response(request_cmd);
    s_transaction[channel].active = true;
    portEXIT_CRITICAL(&s_rx_guard);
}

void wifi_rx_transaction_abort(uint8_t channel)
{
    if (channel >= MAX_PEER) return;

    portENTER_CRITICAL(&s_rx_guard);
    ++s_transaction[channel].request_epoch;
    s_transaction[channel].expected_response_cmd = 0;
    s_transaction[channel].active = false;
    portEXIT_CRITICAL(&s_rx_guard);
}
//! epoch 값을 증가시키면 ,같은 peer의 repairing 루트의 경우, epoch값이 변경되어 peer resp를 버리나? 
void wifi_rx_peer_invalidate(uint8_t channel)
{
    if (channel >= MAX_PEER) return;

    portENTER_CRITICAL(&s_rx_guard);
    ++s_transaction[channel].peer_epoch;
    ++s_transaction[channel].request_epoch;
    s_transaction[channel].expected_response_cmd = 0;
    s_transaction[channel].active = false;
    portEXIT_CRITICAL(&s_rx_guard);
} 
bool wifi_rx_frame_is_current(const wifi_rx_frame_t *frame)
{
    if ((frame == nullptr) || (frame->channel >= MAX_PEER)) return false;

    bool current;
    portENTER_CRITICAL(&s_rx_guard);
    const wifi_rx_transaction_t &transaction = s_transaction[frame->channel];
    current = transaction.active &&
              (frame->peer_epoch == transaction.peer_epoch) &&
              (frame->request_epoch == transaction.request_epoch) &&
              (frame->expected_response_cmd != 0) &&
              (frame->expected_response_cmd == transaction.expected_response_cmd);
    portEXIT_CRITICAL(&s_rx_guard);
    return current;
}

bool wifi_rx_transaction_consume(const wifi_rx_frame_t *frame)
{
    if ((frame == nullptr) || (frame->channel >= MAX_PEER)) return false;

    bool consumed = false;
    portENTER_CRITICAL(&s_rx_guard);
    wifi_rx_transaction_t &transaction = s_transaction[frame->channel];
    if (transaction.active &&
        (frame->peer_epoch == transaction.peer_epoch) &&
        (frame->request_epoch == transaction.request_epoch) &&
        (frame->expected_response_cmd == transaction.expected_response_cmd))
    {
        transaction.active = false;
        transaction.expected_response_cmd = 0;
        ++transaction.request_epoch;
        consumed = true;
    }
    portEXIT_CRITICAL(&s_rx_guard);
    return consumed;
}

void wifi_rx_queue_note_accepted(uint8_t channel)
{
    (void)channel;
    portENTER_CRITICAL(&s_rx_guard);
    ++s_stats.accepted;
    portEXIT_CRITICAL(&s_rx_guard);
}

void wifi_rx_queue_note_drop(wifi_rx_drop_reason_t reason, uint8_t channel)
{
    if (reason >= WIFI_RX_DROP_REASON_COUNT) return;

    portENTER_CRITICAL(&s_rx_guard);
    ++s_stats.dropped[reason];
    if (channel < MAX_PEER) ++s_stats.dropped_by_channel[channel];
    portEXIT_CRITICAL(&s_rx_guard);
}

void wifi_rx_queue_get_stats(wifi_rx_queue_stats_t *stats)
{
    if (stats == nullptr) return;

    portENTER_CRITICAL(&s_rx_guard);
    memcpy(stats, &s_stats, sizeof(*stats));
    portEXIT_CRITICAL(&s_rx_guard);
    stats->waiting = (s_rx_queue != nullptr) ? uxQueueMessagesWaiting(s_rx_queue) : 0;
}


#pragma once

#include <Arduino.h>

#define WIFI_SEND_QUEUE_MAX 50  // Ring buffer slot 수. 실제 저장 가능 개수는 1개 적음.

typedef struct _wifi_queue_t
{
    uint8_t head;               // dequeue/read index.
    uint8_t tail;               // enqueue/write index.
    uint8_t item[WIFI_SEND_QUEUE_MAX]; // queue에 저장된 peer channel 번호.
} wifi_queue_t;

void wifi_queue_init(wifi_queue_t *queue);
bool wifi_queue_is_empty(const wifi_queue_t *queue);
bool wifi_queue_is_full(const wifi_queue_t *queue);
bool wifi_queue_enqueue(wifi_queue_t *queue, uint8_t item);
bool wifi_queue_peek(const wifi_queue_t *queue, uint8_t *item);
bool wifi_queue_dequeue(wifi_queue_t *queue);

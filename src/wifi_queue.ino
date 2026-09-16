#include "wifi_queue.h"

static uint8_t wifi_queue_next_index(uint8_t index)
{
    ++index;
    if (index >= WIFI_SEND_QUEUE_MAX)   index = 0;

    return index;
}

void wifi_queue_init(wifi_queue_t *queue)
{
    if (queue == nullptr)   return;

    queue->head = 0;
    queue->tail = 0;
}

bool wifi_queue_is_empty(const wifi_queue_t *queue)
{
    if (queue == nullptr)   return true;

    return queue->head == queue->tail;
}

bool wifi_queue_is_full(const wifi_queue_t *queue)
{
    if (queue == nullptr)   return false;

    return wifi_queue_next_index(queue->tail) == queue->head;
}

bool wifi_queue_enqueue(wifi_queue_t *queue, uint8_t item)
{
    if ((queue == nullptr) || wifi_queue_is_full(queue))
    {
        return false;
    }

    queue->item[queue->tail] = item;
    queue->tail = wifi_queue_next_index(queue->tail);

    return true;
}

bool wifi_queue_peek(const wifi_queue_t *queue, uint8_t *item)
{
    if ((queue == nullptr) || (item == nullptr) || wifi_queue_is_empty(queue))
    {
        return false;
    }

    *item = queue->item[queue->head];
    return true;
}

bool wifi_queue_dequeue(wifi_queue_t *queue)
{
    if ((queue == nullptr) || wifi_queue_is_empty(queue))
    {
        return false;
    }

    queue->head = wifi_queue_next_index(queue->head);
    return true;
}

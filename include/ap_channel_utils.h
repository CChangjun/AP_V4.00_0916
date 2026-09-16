#pragma once

#include <Arduino.h>
#include "ap_context.h"

static constexpr uint8_t IO_PAGE_DATA_IDX_MAX = 6;
static constexpr uint8_t SERIAL_PEER_CH_MIN = 12;
static constexpr uint8_t SERIAL_PEER_CH_MAX = SERIAL_PEER_CH_MIN + MAX_SERIAL_PEER - 1;
static constexpr uint8_t SERIAL_CMD_CH_MIN = 1;
static constexpr uint8_t SERIAL_CMD_CH_MAX = MAX_SERIAL_PEER;
static constexpr uint8_t SERIAL_PAGE_WORDS = 8;

static inline bool is_io_peer_channel(uint8_t channel)
{
    return channel < MAX_IO_PEER;
}

static inline bool is_serial_peer_channel(uint8_t channel)
{
    return (channel >= SERIAL_PEER_CH_MIN) && (channel <= SERIAL_PEER_CH_MAX);
}

static inline bool is_valid_serial_cmd_ch(uint8_t serial_ch)
{
    return (serial_ch >= SERIAL_CMD_CH_MIN) && (serial_ch <= SERIAL_CMD_CH_MAX);
}

static inline uint8_t serial_cmd_ch_to_peer_channel(uint8_t serial_ch)
{
    return (uint8_t)(SERIAL_PEER_CH_MIN + serial_ch - 1);
}

static inline uint8_t serial_peer_channel_to_index(uint8_t peer_channel)
{
    return (uint8_t)(peer_channel - SERIAL_PEER_CH_MIN);
}

static inline bool is_valid_serial_page(uint8_t page)
{
    if (page == 0) return false;

    const uint16_t start_word = (uint16_t)(page - 1) * SERIAL_PAGE_WORDS;
    const uint16_t serial_word_count = sizeof(g_ap.data.lSPeerData[0]) / sizeof(g_ap.data.lSPeerData[0][0]);
    return (start_word + SERIAL_PAGE_WORDS) <= serial_word_count;
}

static inline uint8_t clamp_io_page_count(uint8_t page_count)
{
    return (page_count > IO_PAGE_DATA_IDX_MAX) ? IO_PAGE_DATA_IDX_MAX : page_count;
}

static inline uint8_t clamp_copy_len(uint8_t requested_len, size_t max_len)
{
    return (requested_len > max_len) ? (uint8_t)max_len : requested_len;
}

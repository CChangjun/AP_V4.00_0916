/*
todo lfc, S-damper etc.. 세부 CLI log 추가 수정 필요해보임. 
todo  현재는 세부 cmd 입력 시, raw data 내뱉는데, 이거 파싱해서 이쁘게 뜨게 끔. 

todo fault 기능 中, master Output buf에서 pairing bit 어떻게 설정해놨는지 확인 후에, fault 기능 추가 수정 필요해보임.

*/

#pragma once

#include <stdlib.h>
#include <string.h>

#define AP_DBG_ENABLE            1
#define AP_DBG_CMD_BUF_LEN       96
#define AP_DBG_LOCKED_POLL_MS    50
#define AP_DBG_RX_BUDGET_LOCKED  16
#define AP_DBG_RX_BUDGET_ACTIVE  32
#define AP_DBG_IDLE_TIMEOUT_MS   (5UL * 60UL * 1000UL)
#define AP_DBG_ALLOW_WRITE       0
#define AP_DBG_FAULT_RSSI_WARN   (-80)
#define AP_DBG_IN_WORDS          (TOT_BYTE_NUM_ROUND_IN / 2)
#define AP_DBG_OUT_WORDS         (TOT_BYTE_NUM_ROUND_OUT / 2)
#define AP_DBG_MAX_PEER          MAX_PEER
#define AP_DBG_MAX_SERIAL_PEER   MAX_SERIAL_PEER
#define AP_DBG_LPEER_ROWS        MAX_PEER

//! 순서 조절했음. 
#ifndef AP_VER
#error "AP_VER must be defined before including ap_debug_cli_impl.h"
#endif

#define AP_DBG_DIW_FLOW      0x01
#define AP_DBG_CDA_FLOW      0x02
#define AP_DBG_SMART_DAMPER  0x03
#define AP_DBG_X_RAY         0x04
#define AP_DBG_D40A          0x05
#define AP_DBG_D4SL          0x06
#define AP_DBG_TIC           0x07
#define AP_DBG_LMFC          0x08
#define AP_DBG_MANOMETER     0x09
#define AP_DBG_LCT           0x0A

static char s_cmd_buf[AP_DBG_CMD_BUF_LEN];
static uint8_t s_cmd_pos = 0;
static bool s_dbg_cli_enabled = false;
static bool s_dbg_write_enabled = false;
static uint32_t s_dbg_last_activity_ms = 0;
static uint32_t s_dbg_next_locked_poll_ms = 0;

static void ap_debug_cli_disable(void);

static uint16_t *dbg_in_words(void)
{
    return (uint16_t *)&EASYCAT.BufferIn.Cust.pairing_bit;
}

static uint16_t *dbg_out_words(void)
{
    return (uint16_t *)&EASYCAT.BufferOut.Cust.pairing_bit;
}

static bool dbg_parse_u32(const char *s, uint32_t *out)
{
    if ((s == nullptr) || (*s == '\0') || (out == nullptr)) return false;

    char *end_ptr = nullptr;
    unsigned long v = strtoul(s, &end_ptr, 0);
    if ((end_ptr == s) || (*end_ptr != '\0')) return false;

    *out = (uint32_t)v;
    return true;
}

static const char *dbg_type_name(uint8_t type)
{
    switch (type)
    {
    case AP_DBG_DIW_FLOW:     return "DIW";
    case AP_DBG_CDA_FLOW:     return "CDA";
    case AP_DBG_SMART_DAMPER: return "SDAMP";
    case AP_DBG_X_RAY:        return "XRAY";
    case AP_DBG_D40A:         return "D40A";
    case AP_DBG_D4SL:         return "D4SL";
    case AP_DBG_TIC:          return "TIC";
    case AP_DBG_LMFC:         return "LFC";
    case AP_DBG_MANOMETER:    return "MANO";
    case AP_DBG_LCT:          return "LCT";
    default:                  return "UNKNOWN";
    }
}

static bool dbg_type_matches_alias(uint8_t actual_type, const char *alias)
{
    if (alias == nullptr) return false;

    if (strcmp(alias, "d40a") == 0) return actual_type == AP_DBG_D40A;
    if (strcmp(alias, "d4sl") == 0) return actual_type == AP_DBG_D4SL;
    if (strcmp(alias, "lfc")  == 0) return actual_type == AP_DBG_LMFC;

    if (strcmp(alias, "sdamp") == 0) return actual_type == AP_DBG_SMART_DAMPER;
    if (strcmp(alias, "smart_damper") == 0) return actual_type == AP_DBG_SMART_DAMPER;

    if (strcmp(alias, "tic")  == 0) return actual_type == AP_DBG_TIC;
    if (strcmp(alias, "lct")  == 0) return actual_type == AP_DBG_LCT;
    if (strcmp(alias, "mano") == 0) return actual_type == AP_DBG_MANOMETER;
    if (strcmp(alias, "diw")  == 0) return actual_type == AP_DBG_DIW_FLOW;
    if (strcmp(alias, "cda")  == 0) return actual_type == AP_DBG_CDA_FLOW;
    return false;
}

static void dbg_print_help(void)
{
    Serial.println();
    Serial.println("===== AP DEBUG CLI =====");
    Serial.println("help");
    Serial.println("ri [start] [count]         : BufferIn dump");
    Serial.println("ro [start] [count]         : BufferOut dump");
#if AP_DBG_ALLOW_WRITE
    Serial.println("wi <idx> <value>           : write BufferIn word");
    Serial.println("wo <idx> <value>           : write BufferOut word");
#else
    Serial.println("wi/wo                      : disabled in this build");
#endif
    Serial.println("pair                       : paired channel summary");
    Serial.println("pair all                   : all channel summary");
    Serial.println("fault                      : abnormal channel summary");
    Serial.println("peer <ch>                  : peer dump");
    Serial.println("ws <ch>                    : wifi state dump");
    Serial.println("rxq                        : ESP-NOW RX queue/drop summary");
    Serial.println("lpd <ch>                   : lPeerData dump");
    Serial.println("lsp <serial_ch> <page>     : lSPeerData page dump");
    Serial.println("lfc <ch>                   : LFC summary");
    Serial.println("sdamp <ch>                 : Smart Damper summary");
    Serial.println("d40a <ch>                  : D40A summary");
    Serial.println("d4sl <ch>                  : D4SL summary");
#if AP_DBG_ALLOW_WRITE
    Serial.println("write on <ver>             : unlock wi/wo");
    Serial.println("write off                  : lock wi/wo");
#endif
    Serial.println("exit                       : disable CLI");
    Serial.println("========================");
    Serial.println();
}

static void dbg_dump_words(const char *title, const uint16_t *ptr, uint8_t total, uint8_t start, uint8_t count)
{
    if (ptr == nullptr) return;
    if (start >= total)
    {
        Serial.printf("[DBG] invalid start=%u (max=%u)\r\n", start, (uint8_t)(total - 1));
        return;
    }

    uint8_t end = start + count;
    if (end > total) end = total;

    Serial.printf("\r\n[DBG] %s start=%u count=%u\r\n", title, start, (uint8_t)(end - start));
    for (uint8_t i = start; i < end; i++)
    {
        Serial.printf("  [%02u] = 0x%04X (%u)\r\n", i, ptr[i], ptr[i]);
    }
}

static void dbg_dump_rx_queue(void)
{
    wifi_rx_queue_stats_t stats = {};
    wifi_rx_queue_get_stats(&stats);

    Serial.printf("\r\n[DBG] RXQ depth=%u waiting=%u high=%u item=%u bytes\r\n",
                  (unsigned)WIFI_RX_QUEUE_DEPTH,
                  (unsigned)stats.waiting,
                  (unsigned)stats.high_watermark,
                  (unsigned)sizeof(wifi_rx_frame_t));
    Serial.printf("  enqueued=%lu dequeued=%lu accepted=%lu\r\n",
                  (unsigned long)stats.enqueued,
                  (unsigned long)stats.dequeued,
                  (unsigned long)stats.accepted);
    Serial.printf("  drop invalid=%lu full=%lu stale=%lu mac=%lu cmd=%lu protocol=%lu crc=%lu\r\n",
                  (unsigned long)stats.dropped[WIFI_RX_DROP_INVALID_FRAME],
                  (unsigned long)stats.dropped[WIFI_RX_DROP_QUEUE_FULL],
                  (unsigned long)stats.dropped[WIFI_RX_DROP_STALE_TRANSACTION],
                  (unsigned long)stats.dropped[WIFI_RX_DROP_SOURCE_MAC],
                  (unsigned long)stats.dropped[WIFI_RX_DROP_UNEXPECTED_COMMAND],
                  (unsigned long)stats.dropped[WIFI_RX_DROP_PROTOCOL],
                  (unsigned long)stats.dropped[WIFI_RX_DROP_CRC]);

    bool any_channel_drop = false;
    for (uint8_t channel = 0; channel < MAX_PEER; ++channel)
    {
        if (stats.dropped_by_channel[channel] == 0) continue;
        any_channel_drop = true;
        Serial.printf("  ch%02u drop=%lu\r\n",
                      (unsigned)(channel + 1u),
                      (unsigned long)stats.dropped_by_channel[channel]);
    }
    if (!any_channel_drop) Serial.println("  channel drop: none");
}

static void dbg_write_word(uint16_t *ptr, uint8_t total, uint8_t idx, uint16_t value, const char *title)
{
    if (ptr == nullptr) return;
    if (idx >= total)
    {
        Serial.printf("[DBG] %s idx out of range: %u\r\n", title, idx);
        return;
    }

    ptr[idx] = value;
    Serial.printf("[DBG] %s[%u] <= 0x%04X (%u)\r\n", title, idx, value, value);
}

static void dbg_dump_pair_summary(bool show_all)
{
    const uint16_t plc_pair = EASYCAT.BufferOut.Cust.pairing_bit;
    const uint16_t ap_pair  = g_ap.data.rx_pairing_status;
    uint8_t shown = 0;
    uint8_t active_request_count = 0;

    for (uint8_t ch = 0; ch < AP_DBG_MAX_PEER; ch++)
    {
        if (wifi_send.request_timeout[ch].active) active_request_count++;
    }

    Serial.printf("\r\n[DBG] pair summary%s\r\n", show_all ? " all" : "");
    Serial.printf("  plc_pair = 0x%04X\r\n", plc_pair);
    Serial.printf("  ap_pair  = 0x%04X\r\n", ap_pair);
    Serial.printf("  rf_group=%u set_ch=%u get_ch=%u active_req=%u\r\n",
                    wifi_send.rf_set_group,
                    wifi_send.rf_set_channel,
                    wifi_send.rf_get_channel,
                    active_request_count);

    for (uint8_t ch = 0; ch < AP_DBG_MAX_PEER; ch++)
    {
        if (!show_all && !g_ap.peer.peer[ch].pairFlag) continue;

        Serial.printf("  ch=%02u pair=%u type=0x%02X(%s) addr=0x%02X io_page=%u serial=%u rxBusy=%u txBusy=%u req=%u tmo=%u/%u RSSI=%d\r\n",
                        ch,
                        g_ap.peer.peer[ch].pairFlag ? 1 : 0,
                        (uint8_t)(g_ap.peer.peer[ch].typeAddr >> 8),
                        dbg_type_name((uint8_t)(g_ap.peer.peer[ch].typeAddr >> 8)),
                        (uint8_t)(g_ap.peer.peer[ch].typeAddr & 0x00FF),
                        g_ap.peer.peer[ch].io_page,
                        g_ap.peer.peer[ch].serial,
                        wifi_send.rx_busy[ch] ? 1 : 0,
                        wifi_send.tx_busy[ch] ? 1 : 0,
                        wifi_send.request_timeout[ch].active ? 1 : 0,
                        (unsigned)wifi_send.request_timeout[ch].count,
                        (unsigned)wifi_send.request_timeout[ch].limit,
                        g_ap.peer.rssi[ch]);
        shown++;
    }

    if (shown == 0)
    {
        Serial.println("  NO paired channel (;;)");
    }
}

static void dbg_dump_fault_summary(void)
{
    const uint16_t plc_pair = EASYCAT.BufferOut.Cust.pairing_bit;
    const uint16_t ap_pair = g_ap.data.rx_pairing_status;
    uint8_t shown = 0;

    Serial.println();
    Serial.println("[DBG] fault summary");

    for (uint8_t ch = 0; ch < AP_DBG_MAX_PEER; ch++)
    {
        const bool plc_paired = bitRead(plc_pair, ch);
        const bool ap_paired = bitRead(ap_pair, ch);
        const bool pair_flag = g_ap.peer.peer[ch].pairFlag;
        const bool pair_mismatch = (plc_paired != ap_paired) || (pair_flag != ap_paired); //! master에서 pairing bit 끄는 것 같던데? 이거 삭제해야ㅐ할 듯
        const bool bad_rssi = pair_flag && (g_ap.peer.rssi[ch] <= AP_DBG_FAULT_RSSI_WARN);
        const bool loss = (g_ap.peer.peer_bak[ch].receiveLoss_cnt > 0);
        const bool disconnecting = (wifi_state_machine[ch].peer.cnt_disconnect > 0);
        const bool repairing = g_ap.peer.peer_bak[ch].repair_itself;

        if (!pair_mismatch && !bad_rssi && !loss && !disconnecting && !repairing) continue;

        Serial.printf("  ch=%02u plc=%u ap=%u pair=%u type=%s addr=0x%02X rssi=%d loss=%u disc=%lu repair=%u rxBusy=%u txBusy=%u\r\n",
                        ch,
                        plc_paired ? 1 : 0,
                        ap_paired ? 1 : 0,
                        pair_flag ? 1 : 0,
                        dbg_type_name((uint8_t)(g_ap.peer.peer[ch].typeAddr >> 8)),
                        (uint8_t)(g_ap.peer.peer[ch].typeAddr & 0x00FF),
                        g_ap.peer.rssi[ch],
                        g_ap.peer.peer_bak[ch].receiveLoss_cnt,
                        (unsigned long)wifi_state_machine[ch].peer.cnt_disconnect,
                        repairing ? 1 : 0,
                        wifi_send.rx_busy[ch] ? 1 : 0,
                        wifi_send.tx_busy[ch] ? 1 : 0);
        shown++;
    }

    if (shown == 0)
    {
        Serial.println("  no fault");
    }
}

static void dbg_dump_peer(uint8_t ch)
{
    if (ch >= AP_DBG_MAX_PEER)
    {
        Serial.printf("[DBG] invalid ch=%u\r\n", ch);
        return;
    }

    const uint8_t type = (uint8_t)(g_ap.peer.peer[ch].typeAddr >> 8);
    const uint8_t addr = (uint8_t)(g_ap.peer.peer[ch].typeAddr & 0x00FF);

    Serial.printf("\r\n[DBG] peer[%u]\r\n", ch);
    Serial.printf("  typeAddr    = 0x%04X\r\n", g_ap.peer.peer[ch].typeAddr);
    Serial.printf("  type        = 0x%02X (%s)\r\n", type, dbg_type_name(type));
    Serial.printf("  addr        = 0x%02X\r\n", addr);
    Serial.printf("  pairFlag    = %u\r\n", g_ap.peer.peer[ch].pairFlag ? 1 : 0);
    Serial.printf("  io_usage    = %u\r\n", g_ap.peer.peer[ch].io_usage);
    Serial.printf("  io_page     = %u\r\n", g_ap.peer.peer[ch].io_page);
    Serial.printf("  serial      = %u\r\n", g_ap.peer.peer[ch].serial);
    Serial.printf("  RSSI        = %d\r\n", g_ap.peer.rssi[ch]);
    Serial.printf("  mac         = %02X:%02X:%02X:%02X:%02X:%02X\r\n",
                    g_ap.peer.peer[ch].mac[0], g_ap.peer.peer[ch].mac[1], g_ap.peer.peer[ch].mac[2],
                    g_ap.peer.peer[ch].mac[3], g_ap.peer.peer[ch].mac[4], g_ap.peer.peer[ch].mac[5]);

    Serial.printf("  bak.repair_cnt      = %u\r\n", g_ap.peer.peer_bak[ch].repair_cnt);
    Serial.printf("  bak.del_cnt         = %u\r\n", g_ap.peer.peer_bak[ch].del_cnt);
    Serial.printf("  bak.repair_itself   = %u\r\n", g_ap.peer.peer_bak[ch].repair_itself ? 1 : 0);
    Serial.printf("  bak.del_set         = %u\r\n", g_ap.peer.peer_bak[ch].del_set ? 1 : 0);
    Serial.printf("  bak.receiveLoss_cnt = %u\r\n", g_ap.peer.peer_bak[ch].receiveLoss_cnt);
    Serial.printf("  bak.wait            = %lu\r\n", (unsigned long)g_ap.peer.peer_bak[ch].wait);
}

static void dbg_dump_wifi_state(uint8_t ch)
{
    if (ch >= AP_DBG_MAX_PEER)
    {
        Serial.printf("[DBG] invalid ch=%u\r\n", ch);
        return;
    }

    Serial.printf("\r\n[DBG] wifi_state_machine[%u]\r\n", ch);
    
    Serial.printf("  pairing.request   = %u\r\n", wifi_state_machine[ch].pairing.request ? 1 : 0);
    Serial.printf("  pairing.response  = %u\r\n", wifi_state_machine[ch].pairing.response ? 1 : 0);
    Serial.printf("  pairing.add       = %u\r\n", wifi_state_machine[ch].pairing.add ? 1 : 0);
    Serial.printf("  pairing.del       = %u\r\n", wifi_state_machine[ch].pairing.del ? 1 : 0);
    Serial.printf("  pairing.state     = %u\r\n", wifi_state_machine[ch].pairing.state ? 1 : 0);
    Serial.printf("  pairing.retry     = %u\r\n", wifi_state_machine[ch].pairing.retry);
    Serial.printf("  pairing.timeout   = %u\r\n", wifi_state_machine[ch].pairing.timeout);

    Serial.printf("  peer.state               = %u\r\n", wifi_state_machine[ch].peer.state);
    Serial.printf("  peer.response            = %u\r\n", wifi_state_machine[ch].peer.response ? 1 : 0);
    Serial.printf("  peer.response_type_serial= %u\r\n", wifi_state_machine[ch].peer.response_type_serial ? 1 : 0);
    Serial.printf("  peer.update_io           = %u\r\n", wifi_state_machine[ch].peer.update_io ? 1 : 0);
    Serial.printf("  peer.update_serial       = %u\r\n", wifi_state_machine[ch].peer.update_serial ? 1 : 0);
    Serial.printf("  peer.request_serial      = %u\r\n", wifi_state_machine[ch].peer.request_serial ? 1 : 0);
    Serial.printf("  peer.channel             = %u\r\n", wifi_state_machine[ch].peer.channel);
    Serial.printf("  peer.device_type         = 0x%02X\r\n", wifi_state_machine[ch].peer.device_type);
    Serial.printf("  peer.device_addr         = 0x%02X\r\n", wifi_state_machine[ch].peer.device_addr);
    Serial.printf("  peer.send_cmd            = 0x%02X\r\n", wifi_state_machine[ch].peer.send_cmd);
    Serial.printf("  peer.txLen               = %u\r\n", wifi_state_machine[ch].peer.txLen);
    Serial.printf("  peer.set_io_data         = 0x%04X\r\n", wifi_state_machine[ch].peer.set_io_data);
    Serial.printf("  peer.cnt_disconnect      = %lu\r\n", (unsigned long)wifi_state_machine[ch].peer.cnt_disconnect);

    Serial.printf("  txBusy                   = %u\r\n", wifi_send.tx_busy[ch] ? 1 : 0);
    Serial.printf("  rxBusy                   = %u\r\n", wifi_send.rx_busy[ch] ? 1 : 0);
}

static void dbg_dump_lpeer(uint8_t ch)
{
    if (ch >= AP_DBG_LPEER_ROWS)
    {
        Serial.printf("[DBG] invalid lPeerData ch=%u\r\n", ch);
        return;
    }

    Serial.printf("\r\n[DBG] lPeerData[%u]\r\n", ch);
    for (uint8_t i = 0; i < 7; i++)
    {
        Serial.printf("  [%u] = 0x%04X (%u)\r\n", i, g_ap.data.lPeerData[ch][i], g_ap.data.lPeerData[ch][i]);
    }
}

static void dbg_dump_lspage(uint8_t serial_ch, uint8_t page)
{
    if ((serial_ch == 0) || (serial_ch > AP_DBG_MAX_SERIAL_PEER))
    {
        Serial.printf("[DBG] invalid serial_ch=%u\r\n", serial_ch);
        return;
    }

    if (page == 0)
    {
        Serial.println("[DBG] page must be 1..N");
        return;
    }

    const uint8_t row = (uint8_t)(serial_ch - 1);
    const uint8_t base = (uint8_t)((page - 1) * 8);

    if ((base + 7) >= 81)
    {
        Serial.printf("[DBG] page overflow base=%u\r\n", base);
        return;
    }

    Serial.printf("\r\n[DBG] lSPeerData[row=%u page=%u]\r\n", row, page);
    for (uint8_t i = 0; i < 8; i++)
    {
        Serial.printf("  [%u] = 0x%04X (%u)\r\n",
                        (uint8_t)(base + i),
                        g_ap.data.lSPeerData[row][base + i],
                        g_ap.data.lSPeerData[row][base + i]);
    }
}

static void dbg_dump_peer_summary_by_alias(const char *alias, uint8_t ch)
{
    if (ch >= AP_DBG_MAX_PEER)
    {
        Serial.printf("[DBG] invalid ch=%u\r\n", ch);
        return;
    }

    const uint8_t type = (uint8_t)(g_ap.peer.peer[ch].typeAddr >> 8);
    if (!dbg_type_matches_alias(type, alias))
    {
        Serial.printf("[DBG] warning: ch=%u actual type=0x%02X(%s), alias=%s mismatch\r\n",
                        ch, type, dbg_type_name(type), alias);
    }

    Serial.printf("\r\n[DBG][%s] ch=%u summary\r\n", alias, ch);
    Serial.printf("  pairFlag=%u rx_pair_bit=%u plc_pair_bit=%u\r\n",
                    g_ap.peer.peer[ch].pairFlag ? 1 : 0,
                    bitRead(g_ap.data.rx_pairing_status, ch),
                    bitRead(EASYCAT.BufferOut.Cust.pairing_bit, ch));

    Serial.printf("  typeAddr=0x%04X type=%s addr=0x%02X RSSI=%d\r\n",
                    g_ap.peer.peer[ch].typeAddr,
                    dbg_type_name(type),
                    (uint8_t)(g_ap.peer.peer[ch].typeAddr & 0x00FF),
                    g_ap.peer.rssi[ch]);

    Serial.printf("  io_usage=%u io_page=%u serial=%u\r\n",
                    g_ap.peer.peer[ch].io_usage,
                    g_ap.peer.peer[ch].io_page,
                    g_ap.peer.peer[ch].serial);

    Serial.printf("  out_word=0x%04X in_word=0x%04X set_io_data=0x%04X\r\n",
                    dbg_out_words()[1 + ch],
                    dbg_in_words()[1 + ch],
                    wifi_state_machine[ch].peer.set_io_data);

    Serial.printf("  pair(req/rsp/add/del/state)=%u/%u/%u/%u/%u\r\n",
                    wifi_state_machine[ch].pairing.request ? 1 : 0,
                    wifi_state_machine[ch].pairing.response ? 1 : 0,
                    wifi_state_machine[ch].pairing.add ? 1 : 0,
                    wifi_state_machine[ch].pairing.del ? 1 : 0,
                    wifi_state_machine[ch].pairing.state ? 1 : 0);

    Serial.printf("  peer(update_io/update_serial/request_serial/response/state)=%u/%u/%u/%u/%u\r\n",
                    wifi_state_machine[ch].peer.update_io ? 1 : 0,
                    wifi_state_machine[ch].peer.update_serial ? 1 : 0,
                    wifi_state_machine[ch].peer.request_serial ? 1 : 0,
                    wifi_state_machine[ch].peer.response ? 1 : 0,
                    wifi_state_machine[ch].peer.state);

    Serial.printf("  txBusy=%u rxBusy=%u cnt_disconnect=%lu repair=%u loss=%u\r\n",
                    wifi_send.tx_busy[ch] ? 1 : 0,
                    wifi_send.rx_busy[ch] ? 1 : 0,
                    (unsigned long)wifi_state_machine[ch].peer.cnt_disconnect,
                    g_ap.peer.peer_bak[ch].repair_itself ? 1 : 0,
                    g_ap.peer.peer_bak[ch].receiveLoss_cnt);

    dbg_dump_lpeer(ch);
}

static void dbg_process_line(char *line)
{
    if ((line == nullptr) || (*line == '\0')) return;

    char *save_ptr = nullptr;
    char *cmd = strtok_r(line, " ", &save_ptr);
    if (cmd == nullptr) return;

    if (strcmp(cmd, "exit") == 0)
    {
        ap_debug_cli_disable();
        return;
    }

    if (strcmp(cmd, "help") == 0)
    {
        dbg_print_help();
        return;
    }

    if (strcmp(cmd, "fault") == 0)
    {
        dbg_dump_fault_summary();
        return;
    }

    if (strcmp(cmd, "rxq") == 0)
    {
        dbg_dump_rx_queue();
        return;
    }

#if AP_DBG_ALLOW_WRITE
    if (strcmp(cmd, "write") == 0)
    {
        char *a = strtok_r(nullptr, " ", &save_ptr);
        char *b = strtok_r(nullptr, " ", &save_ptr);
        uint32_t version = 0;

        if ((a != nullptr) && (strcmp(a, "off") == 0))
        {
            s_dbg_write_enabled = false;
            Serial.println("[DBG] write commands locked");
            return;
        }

        if ((a != nullptr) && (strcmp(a, "on") == 0) &&
            (b != nullptr) && dbg_parse_u32(b, &version) &&
            (version == (uint32_t)AP_VER))
        {
            s_dbg_write_enabled = true;
            Serial.println("[DBG] write commands unlocked");
            return;
        }

        Serial.printf("[DBG] write=%u\r\n", s_dbg_write_enabled ? 1 : 0);
        Serial.printf("[DBG] usage: write on %lu / write off\r\n", (unsigned long)AP_VER);
        return;
    }
#else
    if (strcmp(cmd, "write") == 0)
    {
        Serial.println("[DBG] write commands disabled in this build");
        return;
    }
#endif

    if (strcmp(cmd, "pair") == 0)
    {
        char *a = strtok_r(nullptr, " ", &save_ptr);

        if (a == nullptr)
        {
            dbg_dump_pair_summary(false);
            return;
        }

        if (strcmp(a, "all") == 0)
        {
            dbg_dump_pair_summary(true);
            return;
        }

        Serial.println("[DBG] usage: pair / pair all");
        return;
    }

    if (strcmp(cmd, "ri") == 0)
    {
        uint8_t start = 0;
        uint8_t count = AP_DBG_IN_WORDS;

        char *a = strtok_r(nullptr, " ", &save_ptr);
        char *b = strtok_r(nullptr, " ", &save_ptr);

        uint32_t v = 0;
        if ((a != nullptr) && dbg_parse_u32(a, &v)) start = (uint8_t)v;
        if ((b != nullptr) && dbg_parse_u32(b, &v)) count = (uint8_t)v;

        dbg_dump_words("BufferIn", dbg_in_words(), AP_DBG_IN_WORDS, start, count);
        return;
    }

    if (strcmp(cmd, "ro") == 0)
    {
        uint8_t start = 0;
        uint8_t count = AP_DBG_OUT_WORDS;

        char *a = strtok_r(nullptr, " ", &save_ptr);
        char *b = strtok_r(nullptr, " ", &save_ptr);

        uint32_t v = 0;
        if ((a != nullptr) && dbg_parse_u32(a, &v)) start = (uint8_t)v;
        if ((b != nullptr) && dbg_parse_u32(b, &v)) count = (uint8_t)v;

        dbg_dump_words("BufferOut", dbg_out_words(), AP_DBG_OUT_WORDS, start, count);
        return;
    }

    if ((strcmp(cmd, "wi") == 0) || (strcmp(cmd, "wo") == 0))
    {
#if AP_DBG_ALLOW_WRITE
        if (!s_dbg_write_enabled)
        {
            Serial.printf("[DBG] write locked. usage: write on %lu\r\n", (unsigned long)AP_VER);
            return;
        }
#else
        Serial.println("[DBG] wi/wo disabled in this build");
        return;
#endif

        char *a = strtok_r(nullptr, " ", &save_ptr);
        char *b = strtok_r(nullptr, " ", &save_ptr);

        uint32_t idx = 0;
        uint32_t value = 0;

        if ((a == nullptr) || (b == nullptr) ||
            !dbg_parse_u32(a, &idx) || !dbg_parse_u32(b, &value))
        {
            Serial.println("[DBG] usage: wi <idx> <value> / wo <idx> <value>");
            return;
        }

        if (strcmp(cmd, "wi") == 0)
            dbg_write_word(dbg_in_words(), AP_DBG_IN_WORDS, (uint8_t)idx, (uint16_t)value, "BufferIn");
        else
            dbg_write_word(dbg_out_words(), AP_DBG_OUT_WORDS, (uint8_t)idx, (uint16_t)value, "BufferOut");

        return;
    }

    if (strcmp(cmd, "peer") == 0)
    {
        char *a = strtok_r(nullptr, " ", &save_ptr);
        uint32_t ch = 0;
        if ((a == nullptr) || !dbg_parse_u32(a, &ch))
        {
            Serial.println("[DBG] usage: peer <ch>");
            return;
        }
        dbg_dump_peer((uint8_t)ch);
        return;
    }

    if (strcmp(cmd, "ws") == 0)
    {
        char *a = strtok_r(nullptr, " ", &save_ptr);
        uint32_t ch = 0;
        if ((a == nullptr) || !dbg_parse_u32(a, &ch))
        {
            Serial.println("[DBG] usage: ws <ch>");
            return;
        }
        dbg_dump_wifi_state((uint8_t)ch);
        return;
    }

    if (strcmp(cmd, "lpd") == 0)
    {
        char *a = strtok_r(nullptr, " ", &save_ptr);
        uint32_t ch = 0;
        if ((a == nullptr) || !dbg_parse_u32(a, &ch))
        {
            Serial.println("[DBG] usage: lpd <ch>");
            return;
        }
        dbg_dump_lpeer((uint8_t)ch);
        return;
    }

    if (strcmp(cmd, "lsp") == 0)
    {
        char *a = strtok_r(nullptr, " ", &save_ptr);
        char *b = strtok_r(nullptr, " ", &save_ptr);

        uint32_t ch = 0;
        uint32_t page = 0;

        if ((a == nullptr) || (b == nullptr) ||
            !dbg_parse_u32(a, &ch) || !dbg_parse_u32(b, &page))
        {
            Serial.println("[DBG] usage: lsp <serial_ch> <page>");
            return;
        }

        dbg_dump_lspage((uint8_t)ch, (uint8_t)page);
        return;
    }

    if ((strcmp(cmd, "lfc") == 0) ||
        (strcmp(cmd, "sdamp") == 0) ||
        (strcmp(cmd, "smart_damper") == 0) ||
        (strcmp(cmd, "d40a") == 0) ||
        (strcmp(cmd, "d4sl") == 0) ||
        (strcmp(cmd, "tic") == 0) ||
        (strcmp(cmd, "lct") == 0) ||
        (strcmp(cmd, "mano") == 0) ||
        (strcmp(cmd, "diw") == 0) ||
        (strcmp(cmd, "cda") == 0))
    {
        char *a = strtok_r(nullptr, " ", &save_ptr);
        uint32_t ch = 0;
        if ((a == nullptr) || !dbg_parse_u32(a, &ch))
        {
            Serial.printf("[DBG] usage: %s <ch>\r\n", cmd);
            return;
        }

        dbg_dump_peer_summary_by_alias(cmd, (uint8_t)ch);
        return;
    }

    Serial.printf("[DBG] unknown cmd: %s\r\n", cmd);
    Serial.println("[DBG] type 'help' for command list");
}

static void ap_debug_cli_clear_buf(void)
{
    s_cmd_pos = 0;
    memset(s_cmd_buf, 0, sizeof(s_cmd_buf));
}

static void ap_debug_cli_touch(void)
{
    s_dbg_last_activity_ms = millis();
}

static bool ap_debug_cli_is_unlock_cmd(char *line)
{
    if ((line == nullptr) || (*line == '\0')) return false;

    char *save_ptr = nullptr;
    char *tok0 = strtok_r(line, " ", &save_ptr);
    char *tok1 = strtok_r(nullptr, " ", &save_ptr);
    char *tok2 = strtok_r(nullptr, " ", &save_ptr);
    char *extra = strtok_r(nullptr, " ", &save_ptr);

    if ((tok0 == nullptr) || (strcmp(tok0, "ap") != 0)) return false;
    if ((tok1 == nullptr) || (strcmp(tok1, "dbg") != 0)) return false;

    if ((tok2 == nullptr) || (extra != nullptr))
    {
        Serial.printf("[DBG] usage: ap dbg %lu\r\n", (unsigned long)AP_VER);
        return false;
    }

    uint32_t version = 0;
    if (!dbg_parse_u32(tok2, &version))
    {
        Serial.printf("[DBG] invalid AP debug CLI version: %s\r\n", tok2);
        Serial.printf("[DBG] usage: ap dbg %lu\r\n", (unsigned long)AP_VER);
        return false;
    }

    if (version != (uint32_t)AP_VER)
    {
        Serial.printf("[DBG] AP debug CLI unlock denied: board AP_VER=%lu, typed=%lu\r\n",
                      (unsigned long)AP_VER,
                      (unsigned long)version);
        Serial.printf("[DBG] usage: ap dbg %lu\r\n", (unsigned long)AP_VER);
        return false;
    }

    return true;
}

static void ap_debug_cli_enable(void)
{
    s_dbg_cli_enabled = true;
    s_dbg_write_enabled = false;
    ap_debug_cli_touch();
    ap_debug_cli_clear_buf();
    Serial.printf("[DBG] AP debug CLI enabled... (AP_VER=%lu)\r\n", (unsigned long)AP_VER);
    Serial.printf("[DBG] START AP CLI mode ::: ");
    Serial.println("type 'help' for command list, 'exit' to disable");
}

static void ap_debug_cli_disable(void)
{
    s_dbg_cli_enabled = false;
    s_dbg_write_enabled = false;
    ap_debug_cli_clear_buf();
    Serial.println("[DBG] AP debug CLI disabled");
}

static void ap_debug_cli_init(void)
{
    s_dbg_cli_enabled = false;
    s_dbg_write_enabled = false;
    s_dbg_last_activity_ms = 0;
    ap_debug_cli_clear_buf();
}

static void ap_debug_cli_process_serial_line(void)
{
    if (s_dbg_cli_enabled)
    {
        dbg_process_line(s_cmd_buf);
        return;
    }

    if (ap_debug_cli_is_unlock_cmd(s_cmd_buf))
    {
        ap_debug_cli_enable();
    }
}

static void ap_debug_cli_service(void)
{
#if AP_DBG_ENABLE
    if (s_dbg_cli_enabled)
    {
        const uint32_t now_ms = millis();
        if ((uint32_t)(now_ms - s_dbg_last_activity_ms) >= AP_DBG_IDLE_TIMEOUT_MS)
        {
            Serial.println("[DBG] AP debug CLI idle timeout");
            ap_debug_cli_disable();
            return;
        }
    }

    if (!s_dbg_cli_enabled)
    {
        const uint32_t now_ms = millis();
        if ((int32_t)(now_ms - s_dbg_next_locked_poll_ms) < 0)
        {
            return;
        }
        s_dbg_next_locked_poll_ms = now_ms + AP_DBG_LOCKED_POLL_MS;
    }

    uint8_t rx_budget = s_dbg_cli_enabled ? AP_DBG_RX_BUDGET_ACTIVE : AP_DBG_RX_BUDGET_LOCKED;
    while ((rx_budget > 0) && (Serial.available() > 0))
    {
        rx_budget--;
        char c = (char)Serial.read();
        if (s_dbg_cli_enabled)
        {
            ap_debug_cli_touch();
        }

        if (c == '\r') continue;

        if (c == '\n')
        {
            s_cmd_buf[s_cmd_pos] = '\0';
            if (s_cmd_pos > 0)
            {
                ap_debug_cli_process_serial_line();
            }
            s_cmd_pos = 0;
            continue;
        }

        if (s_cmd_pos < (AP_DBG_CMD_BUF_LEN - 1))
        {
            s_cmd_buf[s_cmd_pos++] = c;
        }
        else
        {
            s_cmd_buf[AP_DBG_CMD_BUF_LEN - 1] = '\0';
            if (s_dbg_cli_enabled)
            {
                Serial.println("[DBG] command too long");
            }
            s_cmd_pos = 0;
        }
    }
#endif
}

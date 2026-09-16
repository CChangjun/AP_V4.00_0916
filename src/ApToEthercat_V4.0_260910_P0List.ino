/**
//^ Q. 1
del_peer()에서..
else
{
    bitWrite(g_ap.data.rx_pairing_status, index, 0);
    g_ap.peer.peer_bak[index].del_set = false;
    g_ap.peer.peer_bak[index].receiveLoss_cnt = 0;
//!    g_ap.peer.peer_bak[index].repair_itself = false; <<-- 얘를 추가해야하지 않을까 싶기도 하고
}
정상적인 의도 삭제 경로에서는 Ethercat_Handle()에서
먼저 peer_bak[channel]를 memset 0 하고,
그 다음 del_set = true를 세팅한 뒤
WIFI_STATE_PAIRING_DEL로 넘기는데,
이 경로라면 repair_itself도 이미 0으로 초기화된 상태라서,
나중에 del_peer()의 del_set == true 분기에서 따로 repair_itself = false를 안 내려도 문제는 없을 것 같음.
근데,
ex) Wifi_Handle()의 예외 처리 경로에서는
g_ap.peer.peer[channel].pairFlag = false;
PairMask_Set(channel, false);
del_peer(channel); 순서로 들어가는데,
이 경로에서는 peer_bak[channel]를 먼저 초기화하지 않음.
//!따라서 이전에 auto-repair 시도 중이었다면 peer_bak[channel].repair_itself가 남아 있을 수도 있음
//!그러면 del_peer() 끝부분의 if (g_ap.peer.peer_bak[index].repair_itself == true)에 걸려서
//!의도치 않게 다시 WIFI_STATE_PAIRING_ADD로 들어갈 수 있지 않을까..
근데 어차피 loss cnt인가 있어서, 그거 종료 전까지, 실제로는 의도적으로 삭제했는데 불필요한 리페어링 재시도 할 듯

 */
/**
 * 2025-01-17 : JHD
 * - 전체적인 구조, 처리 방식 모두를 변경했기 때문에 이전 개발 로그 필요 없어서 지웠음
 * - AP가 마스터, Peer가 Slave가 되는 통신 방식으로 변경
 *
 *
 */

#include <WiFi.h>
#include <esp_now.h>
#include "esp_wifi.h"

#define CUSTOM
#include "AP_SLAVE.h"
#include "EasyCAT.h"
#include <SPI.h>

#include <atomic>
#include "PairMask.h"

EasyCAT EASYCAT(SS);

///////////////////////// TIMER INTERRUPT /////////////////////////////////////
// These define's must be placed at the beginning before #include "TimerInterrupt_Generic.h"
// _TIMERINTERRUPT_LOGLEVEL_ from 0 to 4
#define _TIMERINTERRUPT_LOGLEVEL_ 4
#define TIMER0_INTERVAL_MS 1
// To be included only in main(), .ino with setup() to avoid `Multiple Definitions` Linker Error
#include "ESP32TimerInterrupt.h"
/////////////////////////////////////////////////////////////////////////////////
#include "common.h"
#include "peer_chk_led.h"
#include "ap_types.h"
#include "ap_context.h"
#include "ap_config.h"
#include "wifi_rx_queue.h"
#include "wifi_queue.h"
#include "ap_wifi_runtime.h"
#include "ap_channel_utils.h"
#include "ap_board_io.h"
ap_app_ctx_t g_ap = {};
#if ATOMIC == 1
std::atomic<uint32_t> g_pairMask{0};
#endif
esp_now_peer_info_t peerInfo;

static ap_led_task_arg_t g_led_task_arg;
static TaskHandle_t g_led_task_handle = NULL;
static TaskHandle_t g_led_drv_service_task = NULL;

static StaticTask_t g_led_task_tcb;
static StackType_t  g_led_task_stack[8000];

static StaticTask_t g_led_drv_service_task_tcb;
static StackType_t  g_led_drv_service_task_stack[2048];

uint8_t broadcast_addr[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}; // broadcast Mac address

uint8_t debug_out = 0;

#if RF_TEST_SERIAL_LOG == 1
static uint32_t g_rf_tx_ok_total[MAX_PEER] = {};
static uint32_t g_rf_rx_ok_total[MAX_PEER] = {};
static uint32_t g_rf_timeout_total[MAX_PEER] = {};
static uint32_t g_rf_disconnect_total[MAX_PEER] = {};
static uint32_t g_rf_send_fail_total[MAX_PEER] = {};
static uint32_t g_rf_rx_drop_total[MAX_PEER] = {};
static uint32_t g_rf_last_disconnect_ms[MAX_PEER] = {};
#endif

void Initialize_PDO();

ESP32Timer ITimer0(0);
void Tick_Handle(void);

wifi_state_machine_t wifi_state_machine[MAX_PEER];


wifi_send_t wifi_send;

#if AP_CLI
#include "ap_debug_cli_impl.h"
#endif

static wifi_rx_event_t g_rx_event[MAX_PEER] = {};


void Ethercat_Handle(void);
void Wifi_Peer_Data_Set(uint8_t peer_addr, uint8_t query, uint16_t *tx_data, uint8_t wlen);
void Wifi_Peer_State_Set(WIFI_STATE_MACHINE state, uint8_t addr, uint8_t cmd, uint16_t typeAddr, uint16_t wLen);
void Wifi_Peer_MacAddr_Set(uint8_t addr, uint8_t *mac);
void Wifi_Peer_Rotation(void);
void Wifi_Handle(void);
static void Wifi_Rx_ProcessFrame(const wifi_rx_frame_t &rx_frame);
static void Wifi_Rx_Service(void);


#if PLC_RSSI

//#define GET_RSSI_STATE(rssi)\
//    ((rssi) > GREEN_Boundary ? RSSI_STATE_GREEN : RSSI_STATE_YELLOW)

static inline uint8_t rssi_to_2bit(int rssi_dbm, bool paired)
{
    if (!paired)    return 0;           // 페어링 x

    if (rssi_dbm >= -68)    return 3;   // 좋음
    if (rssi_dbm >= -76)    return 2;   // 보통 //76아래로는 로스나고 끊기기 시작
    //! 조건이 peer chk led랑 동일해야할 듯
    //! else로 묶어서 처리하고 분기
    return 1;                           // 안좋음
}
#endif

// promiscuous_rx call back 함수, recev packet 분석, RSSI값확보
typedef struct
{
    unsigned frame_ctrl : 16;
    unsigned duration_id : 16;
    uint8_t addr1[6]; // src address
    uint8_t addr2[6]; // recv address
    uint8_t addr3[6]; // broadcast address
    uint8_t addr4[6]; // non
    unsigned sequence_ctrl : 16;
} wifi_ieee80211_mac_hdr_t;

typedef struct
{
    wifi_ieee80211_mac_hdr_t hdr;
    uint8_t payload[0];
} wifi_ieee80211_packet_t;

void promiscuous_rx_cb(void *buf, wifi_promiscuous_pkt_type_t type)
{
    if (type != WIFI_PKT_MGMT)
        return;

    const wifi_promiscuous_pkt_t *ppkt = (wifi_promiscuous_pkt_t *)buf;
    const wifi_ieee80211_packet_t *ipkt = (wifi_ieee80211_packet_t *)ppkt->payload;
    const wifi_ieee80211_mac_hdr_t *hdr = &ipkt->hdr;

    for (int i = 0; i < MAX_PEER; i++)
    {
        if (memcmp(g_ap.peer.peer[i].mac, hdr->addr2, 6) == 0)
        {
            g_ap.peer.rssi[i] = ppkt->rx_ctrl.rssi;
            //Serial.printf("RSSI[%02d] = %02d, ", i,g_ap.peer.rssi[i]);
            break;
        }
    }
}

#if RF_TEST_SERIAL_LOG == 1
static inline void rf_log_rx_drop(uint8_t ch)
{
    if (ch < MAX_PEER)
    {
        g_rf_rx_drop_total[ch]++;
    }
}

static void Log_RF_Summary_1s(void)
{
    static bool header_printed = false;
    static uint32_t prev_tx_ok_total[MAX_PEER] = {};
    static uint32_t prev_rx_ok_total[MAX_PEER] = {};
    const uint32_t mask_snap = PairMask_Snapshot();
    const uint32_t now_ms = millis();
    uint8_t paired_count = 0;
    uint8_t rx_busy_count = 0;
    uint8_t tx_busy_count = 0;
    uint32_t sum_packet_loss = 0;
    uint32_t sum_tx_1s = 0;
    uint32_t sum_rx_1s = 0;
    uint32_t sum_tx_total = 0;
    uint32_t sum_rx_total = 0;
    uint32_t sum_timeout_total = 0;
    uint32_t sum_disconnect_total = 0;
    uint32_t sum_send_fail_total = 0;
    uint32_t sum_rx_drop_total = 0;
    uint32_t sum_repair_count = 0;
    uint32_t last_disconnect_ms = 0;

    if (!header_printed)
    {
        Serial.print(F("row,channel,rssi,packet_loss,tx_1s,rx_1s,tx_total,rx_total,timeout_total,disconnect_total,send_fail_total,rx_drop_total,repair_count,paired,rx_busy,tx_busy,last_disconnect_ms,uptime_ms\r\n"));
        header_printed = true;
    }

    for (uint8_t ch = 0; ch < MAX_PEER; ch++)
    {
        const bool paired = PairMask_Test(mask_snap, ch);
        const uint8_t packet_loss = g_ap.peer.peer_bak[ch].receiveLoss_cnt;
        const uint8_t repair_count = g_ap.peer.peer_bak[ch].repair_cnt;
        const int rssi = g_ap.peer.rssi[ch];
        const uint32_t tx_1s = g_rf_tx_ok_total[ch] - prev_tx_ok_total[ch];
        const uint32_t rx_1s = g_rf_rx_ok_total[ch] - prev_rx_ok_total[ch];

        if (paired) paired_count++;
        if (wifi_send.rx_busy[ch]) rx_busy_count++;
        if (wifi_send.tx_busy[ch]) tx_busy_count++;

        sum_packet_loss += packet_loss;
        sum_tx_1s += tx_1s;
        sum_rx_1s += rx_1s;
        sum_tx_total += g_rf_tx_ok_total[ch];
        sum_rx_total += g_rf_rx_ok_total[ch];
        sum_timeout_total += g_rf_timeout_total[ch];
        sum_disconnect_total += g_rf_disconnect_total[ch];
        sum_send_fail_total += g_rf_send_fail_total[ch];
        sum_rx_drop_total += g_rf_rx_drop_total[ch];
        sum_repair_count += repair_count;
        if (g_rf_last_disconnect_ms[ch] > last_disconnect_ms)
        {
            last_disconnect_ms = g_rf_last_disconnect_ms[ch];
        }

        if (!paired &&
            tx_1s == 0 &&
            rx_1s == 0 &&
            packet_loss == 0 &&
            g_rf_tx_ok_total[ch] == 0 &&
            g_rf_rx_ok_total[ch] == 0 &&
            g_rf_timeout_total[ch] == 0 &&
            g_rf_disconnect_total[ch] == 0 &&
            g_rf_send_fail_total[ch] == 0 &&
            g_rf_rx_drop_total[ch] == 0 &&
            repair_count == 0 &&
            rssi == -127)
        {
            continue;
        }

        Serial.printf("CH,%u,%d,%u,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%u,%u,%u,%u,%lu,%lu\r\n",
                      ch,
                      rssi,
                      packet_loss,
                      (unsigned long)tx_1s,
                      (unsigned long)rx_1s,
                      (unsigned long)g_rf_tx_ok_total[ch],
                      (unsigned long)g_rf_rx_ok_total[ch],
                      (unsigned long)g_rf_timeout_total[ch],
                      (unsigned long)g_rf_disconnect_total[ch],
                      (unsigned long)g_rf_send_fail_total[ch],
                      (unsigned long)g_rf_rx_drop_total[ch],
                      (unsigned int)repair_count,
                      (unsigned int)(paired ? 1 : 0),
                      (unsigned int)(wifi_send.rx_busy[ch] ? 1 : 0),
                      (unsigned int)(wifi_send.tx_busy[ch] ? 1 : 0),
                      (unsigned long)g_rf_last_disconnect_ms[ch],
                      (unsigned long)now_ms);

        prev_tx_ok_total[ch] = g_rf_tx_ok_total[ch];
        prev_rx_ok_total[ch] = g_rf_rx_ok_total[ch];
    }

    Serial.printf("TOTAL,255,0,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%u,%u,%u,%lu,%lu\r\n",
                  (unsigned long)sum_packet_loss,
                  (unsigned long)sum_tx_1s,
                  (unsigned long)sum_rx_1s,
                  (unsigned long)sum_tx_total,
                  (unsigned long)sum_rx_total,
                  (unsigned long)sum_timeout_total,
                  (unsigned long)sum_disconnect_total,
                  (unsigned long)sum_send_fail_total,
                  (unsigned long)sum_rx_drop_total,
                  (unsigned long)sum_repair_count,
                  (unsigned int)paired_count,
                  (unsigned int)rx_busy_count,
                  (unsigned int)tx_busy_count,
                  (unsigned long)last_disconnect_ms,
                  (unsigned long)now_ms);
}
#endif

// delete peer from pairing info and index
void del_peer(uint8_t index)
{
    if (index >= MAX_PEER) return;

    // 이 시점 이전에 callback Queue에 들어간 frame은 더 이상 이 peer의 응답이 아니다.
    // Queue 전체를 비우지 않고 channel 세대값만 바꿔 다른 peer의 정상 응답은 보존한다.
    wifi_rx_peer_invalidate(index);
    wifi_send.tx_busy[index] = false;
    wifi_send.rx_busy[index] = false;

#if FUNC_REPAIRD_AUTO == 1
    if (g_ap.peer.peer[index].pairFlag)
    {
        //! peer에는 등록되어 있지만 PLC에 등록되어 있지 않다면 강제 삭제라고 정리함
        if (!g_ap.peer.peer_bak[index].del_set) //! peer가 삭제될 때 del set여부에 따라(false면 자체 repair, true면 의도 삭제이므로 repair진행x)
        {
            g_ap.peer.peer_bak[index].repair_itself = true;
            g_ap.peer.peer_bak[index].repair_cnt = 0;
            // backup
            g_ap.peer.peer_bak[index].typeAddr = g_ap.peer.peer[index].typeAddr;

            // retry data request와 repair후에도 지속적으로 페어링만 되고 데이타를 못 받았을 경우
            // 최대 20sec(변경 필요) 넘어서면 바로 repair_itself를 cancel하고 ecat에 disconnect report

#if DEBUG_SERIAL_MONITOR == 1
            Serial.printf("[MSG]WIFI::MODULE::DEL PEER[%d]::Itself retry repair, typeAddr=%04x, cnt=%d\r\n",
            index,
            g_ap.peer.peer_bak[index].typeAddr,
            g_ap.peer.peer_bak[index].del_cnt);
#endif
        }
        else
        {
            bitWrite(g_ap.data.rx_pairing_status, index, 0);
            g_ap.peer.peer_bak[index].del_set = false;
            g_ap.peer.peer_bak[index].receiveLoss_cnt = 0;
            g_ap.peer.peer_bak[index].repair_itself = false; //!추가 해놓음 260326 CJL

        }
    }
#endif
    g_ap.peer.peer[index].pairFlag = false;

#if ATOMIC == 1
    PairMask_Set(index, false);
#endif

    esp_now_del_peer(g_ap.peer.peer[index].mac);
    memset(&g_ap.peer.peer[index], 0, sizeof(g_ap.peer.peer[index]));
    wifi_state_machine[index].pairing.del = false;

#if FUNC_REPAIRD_AUTO == 1
    if (g_ap.peer.peer_bak[index].repair_itself == true)
    {
        memset(&wifi_state_machine[index], 0, sizeof(wifi_state_machine[index]));
        g_ap.peer.peer[index].typeAddr = g_ap.peer.peer_bak[index].typeAddr;
        Wifi_Peer_State_Set(WIFI_STATE_PAIRING_ADD, index, AP_PAIRING_REQ, g_ap.peer.peer_bak[index].typeAddr, 0);
    }
#endif

#if DEBUG_SERIAL_WIFI == 1
    Serial.printf("[MSG]WIFI::MODULE::DEL PEER[%d]::Removed from peer list\r\n", index);
#endif
}

bool pairing_register(uint8_t idx, uint16_t channel)
{
    // esp_err_t pairing_result;
    memcpy(&peerInfo.peer_addr, g_ap.peer.peer[idx].mac, 6);
    peerInfo.channel = channel;
    peerInfo.encrypt = false;
    esp_err_t pairing_result = esp_now_add_peer(&peerInfo);

    if (pairing_result == ESP_OK)
    {
        g_ap.peer.peer[idx].pairFlag = true;
#if ATOMIC == 1
        PairMask_Set(idx, true); //! 추가 TEST
#endif

#if FUNC_PAIRED_VERIFY == 0
        bitWrite(g_ap.data.rx_pairing_status, idx, 1); // 페어드 이후 데이타 요청이 성공 했을때 SET
#endif

#if DEBUG_SERIAL_WIFI == 1
        Serial.printf("[MSG]WIFI::MODULE::REGISTER[%2d] : OK\n", idx);
#endif
        // 페어링 완료

        return true;
    }
    else if (pairing_result == ESP_ERR_ESPNOW_EXIST)
    {
        g_ap.peer.peer[idx].pairFlag = true;
#if ATOMIC == 1
        PairMask_Set(idx, true); //! 추가 TEST
#endif

#if FUNC_PAIRED_VERIFY == 0
        bitWrite(g_ap.data.rx_pairing_status, idx, 1); // 페어드 이후 데이타 요청이 성공 했을때 SET
#endif

#if DEBUG_SERIAL_WIFI == 1
        Serial.printf("[MSG]WIFI::MODULE::REGISTER[%2d] : Already Paired\n", idx);
#endif
        return true; // 버그수정, 추가 했음 : HDJung
    }
    else
    {
#if DEBUG_SERIAL_WIFI == 1
        Serial.printf("[MSG]WIFI::MODULE::REGISTER[%2d] : Failed\n", idx);
#endif
        return false;
    }
}

#if 0
void rssi_display()
{
    static unsigned int cnt = 0;
    static unsigned long lastTime = 0;
    uint16_t tx_p_status = EASYCAT.BufferOut.Cust.pairing_bit;  // 녹색

    for (uint8_t i = 0; i < MAX_PEER; i++)
    {
        if (g_ap.peer.peer[i].pairFlag == true)
        {
            if ( bitRead(tx_p_status, i) == 0 )     //! 여기 g_ap.peer.rssi[@]로 치환해야 함
            {
                Serial.printf("RSSI[%02d] = %02d, ", i, RSSI[i]);
            }
            else
            {
                Serial.printf("RSSI[%02d] = %02d(PairingBit OFf), ", i, RSSI[i]);
            }
        }
    }

    Serial.printf("\n");

}
#endif


void Ethercat_Handle(void)
{
    // ethercAT 값 Scan 또는 pairing 해야할 목록 업데이트
    // 흐름 요약:
    // 1) PLC BufferOut 값을 읽어 현재 채널의 pairing/IO/serial 요청을 판단한다.
    // 2) 실제 ESP-NOW 송신은 여기서 하지 않고, state machine에 요청 flag만 세운다.
    // 3) Wifi_Handle()가 이 flag를 소비하여 AP_* packet을 만든 뒤 전송한다.
    static uint8_t peer_channel = 0;

    static uint16_t tx_pairStatus;

    static uint16_t *tx_ptr;
    static uint16_t *rx_ptr;

    static uint16_t tx_serial;   // 녹색, Serial 영역 Data1, Ch, Page 영역
    static uint16_t *serial_ptr; // 녹색, Serial 영역 Data2 ~, 시리얼 데이터 영역

    static uint8_t tx_w_ch;
    static uint8_t tx_w_page;
    static uint8_t tx_r_ch;
    static uint8_t tx_r_page;

    uint8_t tmpCh = 0;

    bool req_pair = false;

    uint8_t device_id, device_type, page;

    tx_pairStatus = EASYCAT.BufferOut.Cust.pairing_bit; // PLC에서 보낸 pairing데이터
    tx_ptr = (uint16_t *)&EASYCAT.BufferOut.Cust.data1; // 녹색 plc에서 보낸 데이터
    rx_ptr = (uint16_t *)&EASYCAT.BufferIn.Cust.data1;  // 적색 mcu 데이터
    // rx

    EASYCAT.BufferIn.Cust.pairing_bit = g_ap.data.rx_pairing_status; // 적색; rx_pairing status update    //mcu쪽 정보를 plc가 읽어갈 수 있게

//! GATE : 메인제어기 통신 → PLC_RSSI 
#if PLC_RSSI 

    uint16_t w_out = tx_ptr[MONITOR_WORD_IDX];
    uint8_t page_sel = (w_out >> 12) & 0x0F;

    uint8_t rssi2 = 0;
    uint16_t fw10 = 0;
//* ==================== page 처리 ====================
    if (page_sel == 0)
    {
        // AP 정보
        rssi2 = 0x03; // page:0일 때 AP이므로, rssi는 1로 채움
        fw10 = AP_VER_10BIT;
    }
    else
    {
        uint8_t idx = (uint8_t)(page_sel - 1);

        if (idx >= MAX_PEER)
        {
            rssi2 = RSSI_INIT;
            fw10  = FW_VERSION_INIT;
        }
        else
        {
            rssi2 = rssi_to_2bit(g_ap.peer.rssi[idx], g_ap.peer.peer[idx].pairFlag);
            fw10  = (uint16_t)(g_ap.peer.peer_packet[idx].fw_version & 0x03FF);
        }
    }
//* ==================================================

    //~ 15:12 page, 11:10 RSSI, 09:00 FW version
    uint16_t w_in =
        ((uint16_t)(page_sel & 0x0F) << 12) |
        ((uint16_t)(rssi2   & 0x03) << 10)  |
        ((uint16_t)(fw10    & 0x03FF));
    rx_ptr[MONITOR_WORD_IDX] = w_in;
                                                        //W16 1 word parsing
#endif

    if (g_ap.os.gSysTick.flag.bf.sec1)
    {
#if DEBUG_SERIAL_ECAT == 1
        Serial.printf("[MSG]ECAT::Request Pair List = 0x%04x\r\n", tx_pairStatus);
#endif
    }
    if (bitRead(tx_pairStatus, peer_channel))   req_pair = true;

    if (req_pair)
    {
        // PLC pairing_bit가 켜진 채널이다.
        // tx_ptr[peer_channel]의 high byte는 device type, low byte는 device id로 해석한다.
        device_type = (uint8_t)(tx_ptr[peer_channel] >> 8);
        device_id = (uint8_t)(tx_ptr[peer_channel] & 0x00ff);
        // 페어링이 안되어 있고, device id와 device type가 있을 경우(id, type은 ap slave map상의 용어)
#if PLC_PAIRING_STATUS == 0

        Serial.printf(
            "[PAIR DEBUG] ch=%d tx_word=0x%04X type=0x%02X addr=0x%02X pairing_bit=%d\n",
            peer_channel,
            tx_ptr[peer_channel],
            device_type,
            device_id,
            bitRead(tx_pairStatus, peer_channel)
        );

#endif
        if (!g_ap.peer.peer[peer_channel].pairFlag && device_type && device_id)
        {
            if (wifi_state_machine[peer_channel].pairing.request == false) // 요청중이지 않을때만 전송
            {
                // 미페어링 상태에서 PLC가 type/id를 내려주면 신규 페어링 요청으로 본다.
                // Wifi_Peer_State_Set()에서 pairing.request/add가 set되고 Wifi_Handle()가 전송한다.
                g_ap.peer.peer[peer_channel].typeAddr = tx_ptr[peer_channel];

                Wifi_Peer_State_Set(WIFI_STATE_PAIRING_ADD, peer_channel, AP_PAIRING_REQ, g_ap.peer.peer[peer_channel].typeAddr, 0);

#if DEBUG_SERIAL_ECAT == 1
                Serial.printf("[MSG]ECAT::Request Pair[%d]::New Add::TypeAddress=0x%04x\r\n", peer_channel, g_ap.peer.peer[peer_channel].typeAddr);
#endif
            }
        }
        else if (g_ap.peer.peer[peer_channel].pairFlag && device_type && device_id)
        {
            // 페어링 성공 했고, 데이타는 아직 안 받았을때 : 데이타 요청 처리를 해야 함.
            if (is_io_peer_channel(peer_channel))
            {
#if 0
                if( g_ap.peer.peer[peer_channel].io_page )  //read and page
                {
                    page = 1;
                    g_ap.data.lPeerData[peer_channel][0] = 0; //0은 page가 없을때의 데이타 영역. 쓰레기 값이 들어갈 수 있음
                }else   //read and no page
                {
                    page = 0;

                    wifi_state_machine[peer_channel].peer.set_io_data = 0;
                }
#endif
            }
            //! 코드 흐름 상 tmpCh가 아직 설정 안됨. 이쪽 블록으로 들어오기 전에는 값이 안바뀔 수도 있을 것 같아서 tmpCh -> peer_channel
            //! tmpch로 했을때는 안떴는데, 변경 후 EMT에서 확인해보니 0x7ff로 계속 뜸. 확실히 tmpch가 이 타이밍엔 setting이 안됐었음. 버그성을 수정했는데 사이드 이펙트 터짐 ㅜ
            else if (is_serial_peer_channel(peer_channel) && g_ap.peer.peer[peer_channel].serial)
            {
#if TEMP_SERIAL_PAIRBIT_AUTO_GET == 1
                if (!wifi_state_machine[peer_channel].peer.update_serial && !wifi_state_machine[peer_channel].peer.request_serial) // Serial Device 12-15
                {
                    // 이미 paired된 serial 채널에서 PLC pairing_bit가 유지되면 serial 데이터 읽기 요청을 예약한다.
                    // 실제 AP_SERIAL_GET packet 생성은 Wifi_Handle()의 request_serial 분기에서 진행된다.
                    wifi_state_machine[peer_channel].peer.request_serial = true;

                    memset(&wifi_state_machine[peer_channel].peer.set_serial_Buf[1], 0, 16);
                    // device comm command

                    wifi_state_machine[peer_channel].peer.set_serial_Buf[1] = 0;

                }
#endif
            }
        }
        else if (g_ap.peer.peer[peer_channel].pairFlag && !tx_ptr[peer_channel]) // peer는 등록되어 있고, ethercat에는 없을 경우
        {
            if (wifi_state_machine[peer_channel].pairing.request == false) // 요청중이지 않을때만 전송
            {
                // AP에는 paired 상태가 남아 있는데 PLC가 해당 data word를 0으로 내리면 삭제 요청으로 본다.
                // 이후 Wifi_Handle()가 AP_PAIRING_CANCEL을 보내고 응답을 받으면 del_peer()를 수행한다.
#if FUNC_REPAIRD_AUTO == 1
                memset(&g_ap.peer.peer_bak[peer_channel], 0, sizeof(g_ap.peer.peer_bak[peer_channel]));
                g_ap.peer.peer_bak[peer_channel].del_set = true;
#endif
                Wifi_Peer_State_Set(WIFI_STATE_PAIRING_DEL, peer_channel, AP_PAIRING_CANCEL, 0, 0);
#if DEBUG_SERIAL_ECAT == 1
                Serial.printf("[MSG]ECAT::Request Pair[%d]::Del\r\n", peer_channel);
#endif
            }
        }
        else
        {
            // 예외 무시
        }
    }
    else
    {
        if (g_ap.peer.peer[peer_channel].pairFlag) // paired 일때
        {
            // PLC pairing_bit가 꺼진 paired 채널은 일반 IO/serial 데이터 교환 대상으로 처리한다.
            // 이 구간에서 update_io/update_serial/request_serial flag가 set될 수 있다.
#if 0
            switch( wifi_state_machine[peer_channel].peer.pairedStart_Proc )
            {
                case 0:
                    wifi_state_machine[peer_channel].peer.pairedStart_Proc=1;
                    if( ++peer_channel>=MAX_PEER ) peer_channel=0;

                break;
            }
#endif

            //! 20250105 : Serial Deviceeh io를 사용할 수 있기 때문에 15까지 검색해서 해야 하는데, 시스템에서 아직 듀얼로는 사용 안하니까
            //! 추후에 시스템과 협의해서 작업 필요. 코드는 만들어서 넣어놨음..검증필요!!!!
            if (is_io_peer_channel(peer_channel))
            {
                if ((tx_ptr[peer_channel] & BIT15) && (g_ap.peer.peer[peer_channel].io_page)) // write and page
                {
                    // IO page 장치에서 PLC가 BIT15를 세우면 쓰기 요청이다.
                    // update_io를 세워두면 Wifi_Handle()가 AP_IO_SET으로 변환해서 보낸다.
                    page = (tx_ptr[peer_channel] & 0x7000) >> 12; //* PDO 상 12-14bit page 할당됨
                    // Write
                    if (page == 0) page = 1;
                    if (page > g_ap.peer.peer[peer_channel].io_page) page = g_ap.peer.peer[peer_channel].io_page;
                    page = clamp_io_page_count(page);

                    if (!wifi_state_machine[peer_channel].peer.update_io)
                    {
                        wifi_state_machine[peer_channel].peer.update_io = true;
                        wifi_state_machine[peer_channel].peer.set_io_data = tx_ptr[peer_channel];
                    }

                    g_ap.data.lPeerData[peer_channel][0] = 0; // 0은 page가 없을때의 데이타 영역. 쓰레기 값이 들어갈 수 있음
                }
                else if (g_ap.peer.peer[peer_channel].io_page) // read and page
                {
                    // IO page 장치에서 BIT15가 없으면 읽기 흐름이다.
                    // 현재 page cache를 PLC 입력 영역에 올리고, 주기 AP_IO_GET은 Wifi_Handle() 기본 분기에서 수행된다.
                    page = (tx_ptr[peer_channel] & 0x7000) >> 12;
                    if (page == 0) page = 1;
                    if (page > g_ap.peer.peer[peer_channel].io_page) page = g_ap.peer.peer[peer_channel].io_page;
                    page = clamp_io_page_count(page);

                    g_ap.data.lPeerData[peer_channel][0] = 0; // 0은 page가 없을때의 데이타 영역. 쓰레기 값이 들어갈 수 있음
                }
                else // read and no page
                {
                    // page가 없는 IO 장치는 PLC word를 set_io_data에 유지한다.
                    // 쓰기 flag가 없더라도 AP_IO_GET packet payload에 이 1word가 같이 실린다.
                    page = 0;

                    wifi_state_machine[peer_channel].peer.set_io_data = tx_ptr[peer_channel]; // page가 없을때도 쓰기 데이타를 계속 보내야 함
                }
                memcpy(&rx_ptr[peer_channel], (const uint16_t *)&g_ap.data.lPeerData[peer_channel][page], 2);
            }
            else if (is_serial_peer_channel(peer_channel) && g_ap.peer.peer[peer_channel].serial)
            {
                // IO와 SERIAL은 같은 채널 동기화 시켜서 처리 : 그래야지 무선으로 data를 한 번에 보낼 수 있음

                tx_serial = EASYCAT.BufferOut.Cust.com1; //* PDO 상 Serial Device Comm Command W17
                tx_w_ch = (tx_serial & 0xF000) >> 12; //1~4 범위
                tx_w_page = (tx_serial & 0x0F00) >> 8;
                tx_r_ch = (tx_serial & 0x00F0) >> 4;
                tx_r_page = tx_serial & 0x000F;

                const bool serial_write_req = (tx_w_ch != 0) && (tx_w_page != 0);
                const bool serial_read_req = (tx_r_ch != 0) && (tx_r_page != 0);

                // 기존 WOS/AP 흐름 유지: write/read command는 서로 독립적으로 처리한다.
                // Safety guard만 추가하여 ch/page overflow 및 invalid command를 차단한다.
                if (serial_write_req && is_valid_serial_cmd_ch(tx_w_ch) && is_valid_serial_page(tx_w_page))
                {
                    // W17 write nibble이 유효하면 PLC com1~com8을 serial write buffer에 복사한다.
                    // update_serial=true가 되면 Wifi_Handle()가 AP_SERIAL_SET으로 peer에 보낸다.
                    tmpCh = serial_cmd_ch_to_peer_channel(tx_w_ch);

                    if (!wifi_state_machine[tmpCh].peer.update_serial) // Serial Device 12-15
                    {
                        // write
                        // 채널 요청 정보가 있을때
                        wifi_state_machine[tmpCh].peer.update_serial = true;
                        wifi_state_machine[tmpCh].peer.request_serial = false;
                        memcpy(&wifi_state_machine[tmpCh].peer.set_serial_Buf[1], &EASYCAT.BufferOut.Cust.com1, 16);
                    }
                }

                if (serial_read_req && is_valid_serial_cmd_ch(tx_r_ch) && is_valid_serial_page(tx_r_page))
                {
                    // W17 read nibble이 유효하면 먼저 기존 serial cache를 PLC 입력 com1~com8에 올린다.
                    // 동시에 request_serial=true를 예약하여 다음 무선 응답으로 cache를 갱신한다.
                    tmpCh = serial_cmd_ch_to_peer_channel(tx_r_ch);
                    // read
                    serial_ptr = (uint16_t *)&EASYCAT.BufferIn.Cust.com1;
                    //peer data를 com1-8로 덮어쑈ㅡㅁ
                    memcpy(&serial_ptr[0], (const uint16_t *)&g_ap.data.lSPeerData[tx_r_ch - 1][((tx_r_page - 1) * 8) + 0], 2); //* com1-8
                    memcpy(&serial_ptr[1], (const uint16_t *)&g_ap.data.lSPeerData[tx_r_ch - 1][((tx_r_page - 1) * 8) + 1], 2);
                    memcpy(&serial_ptr[2], (const uint16_t *)&g_ap.data.lSPeerData[tx_r_ch - 1][((tx_r_page - 1) * 8) + 2], 2);
                    memcpy(&serial_ptr[3], (const uint16_t *)&g_ap.data.lSPeerData[tx_r_ch - 1][((tx_r_page - 1) * 8) + 3], 2);
                    memcpy(&serial_ptr[4], (const uint16_t *)&g_ap.data.lSPeerData[tx_r_ch - 1][((tx_r_page - 1) * 8) + 4], 2);
                    memcpy(&serial_ptr[5], (const uint16_t *)&g_ap.data.lSPeerData[tx_r_ch - 1][((tx_r_page - 1) * 8) + 5], 2);
                    memcpy(&serial_ptr[6], (const uint16_t *)&g_ap.data.lSPeerData[tx_r_ch - 1][((tx_r_page - 1) * 8) + 6], 2);
                    memcpy(&serial_ptr[7], (const uint16_t *)&g_ap.data.lSPeerData[tx_r_ch - 1][((tx_r_page - 1) * 8) + 7], 2);

                    if (!wifi_state_machine[tmpCh].peer.update_serial && !wifi_state_machine[tmpCh].peer.request_serial) // Serial Device 12-15
                    {
                        wifi_state_machine[tmpCh].peer.request_serial = true;

                        memset(&wifi_state_machine[tmpCh].peer.set_serial_Buf[1], 0, 16);
                        // device comm command
                        wifi_state_machine[tmpCh].peer.set_serial_Buf[1] = tx_serial;
                    }
                }
                if (tmpCh)
                {
                    //! i/o 1word 같이 보냄
                    wifi_state_machine[tmpCh].peer.set_serial_Buf[0] = tx_ptr[tmpCh];
                    memcpy(&rx_ptr[tmpCh], (const uint16_t *)&g_ap.data.lPeerData[tmpCh][0], 2);
                }
            }
        }
    }

    if (++peer_channel >= MAX_PEER) peer_channel = 0;
}

void recv_cb(const uint8_t *src_mac, const uint8_t *data, int len)
{
    // Wi-Fi task(Core 0)에서는 고정 크기 Queue에 frame과 transaction snapshot만 복사하며,
    // Queue full/invalid frame은 내부 counter에 기록하고, callback에서는 block/log를 하지 않는다. -> cb에서 하던거 다 쪼갬 0911
    wifi_rx_queue_push(src_mac, data, len);
    //! return false로 넘겨놨음 
}

static void Wifi_Rx_ProcessFrame(const wifi_rx_frame_t &rx_frame)
{
    // Arduino loop task(Core 1)에서 실행한다. 기존 recv_cb()의 검증/cache 갱신 본문을 유지하되,
    // 처리 전에 수신 당시 request/peer 세대값이 아직 유효한지 먼저 확인한다.
    if (rx_frame.channel >= MAX_PEER)
    {
        wifi_rx_queue_note_drop(WIFI_RX_DROP_INVALID_FRAME, rx_frame.channel);
        return;
    }
    if (!wifi_rx_frame_is_current(&rx_frame))
    {
        wifi_rx_queue_note_drop(WIFI_RX_DROP_STALE_TRANSACTION, rx_frame.channel);
        return;
    }

    const uint8_t *src_mac = rx_frame.src_mac;
    const uint8_t *data = rx_frame.data;
    const int len = rx_frame.len;

    uint8_t peer_channel = 0, packet_len = 0, cnt, data_len;
    word_big_endian_t crc16;
    uint16_t readData;

    static uint8_t buff[WIFI_PACKET_MAX];
    uint8_t page = 0;
    uint8_t exist_cnt = 0, i;

    bool rx_pairing_response = false;
    bool rx_normal_response = false;
    bool rx_serial_response = false;
    bool rx_pairing_cancel = false;

    if (data == nullptr)
    {
    #if DEBUG_SERIAL_WIFI == 1
        Serial.printf("[MSG]WIFI::CALLBAK::NULL DATA\r\n");
    #endif
        return;
    }

    if (len <= 0)
    {
    #if DEBUG_SERIAL_WIFI == 1
        Serial.printf("[MSG]WIFI::CALLBAK::LEN INVALID=%d\r\n", len);
    #endif
        return;
    }

    if (len > WIFI_PACKET_MAX)
    {
    #if DEBUG_SERIAL_WIFI == 1
        Serial.printf("[MSG]WIFI::CALLBAK::LEN OVER=%d MAX=%d\r\n", len, WIFI_PACKET_MAX);
    #endif
        return;
    }

    if (len < 10)
    {
    #if DEBUG_SERIAL_WIFI == 1
        Serial.printf("[MSG]WIFI::CALLBAK::LEN UNDER=%d\r\n", len);
    #endif
        return;
    }

    packet_len = (uint8_t)len;

    memcpy(buff, data, packet_len);

    peer_channel = buff[WIFI_PACKET_CHANNEL];
    if (peer_channel >= MAX_PEER)
    {
    #if DEBUG_SERIAL_WIFI == 1
        Serial.printf("[MSG]WIFI::CALLBAK::OVER PEER CHANNEL=%u\r\n", peer_channel);
    #endif
        return;
    }

    if (wifi_send.rf_set_group != buff[WIFI_PACKET_GROUP])
    {
        wifi_rx_queue_note_drop(WIFI_RX_DROP_PROTOCOL, peer_channel);
#if RF_TEST_SERIAL_LOG == 1
        rf_log_rx_drop(peer_channel);
#endif
#if DEBUG_SERIAL_WIFI == 1
        Serial.printf("[MSG]WIFI::CALLBAK::Miss match group::AP=%d, Peer=%d\r\n", wifi_send.rf_set_group, buff[WIFI_PACKET_GROUP]);
#endif
        return;
    }

    if (packet_len != buff[WIFI_PACKET_LENGTH])
    {
        wifi_rx_queue_note_drop(WIFI_RX_DROP_PROTOCOL, peer_channel);
#if RF_TEST_SERIAL_LOG == 1
        rf_log_rx_drop(peer_channel);
#endif
#if DEBUG_SERIAL_WIFI == 1
        Serial.printf("[MSG]WIFI::CALLBAK::Err packet Length::AP=%d, Peer=%d\r\n", packet_len, buff[WIFI_PACKET_LENGTH]);
#endif
        return;
    }

    if (!wifi_send.rx_busy[peer_channel])
    {
        wifi_rx_queue_note_drop(WIFI_RX_DROP_STALE_TRANSACTION, peer_channel);
#if RF_TEST_SERIAL_LOG == 1
        rf_log_rx_drop(peer_channel);
#endif
        // AP가 해당 채널의 응답을 기다리는 중이 아니면 현재 transaction과 무관한 packet으로 본다.
        // 해당 채널에 대해서 wifi handle이 처리 중
        // if( peer_channel == wifi_state_machine[peer_channel].peer.channel )
#if DEBUG_SERIAL_WIFI == 1
        Serial.printf("[MSG]WIFI::CALLBAK::Channel[%d] is not rxBusy\r\n", peer_channel);
#endif
        return;
    }
    wifi_send.doing_recv_cb = true; //! recv flag set ########################################


    if (peer_channel != wifi_state_machine[peer_channel].peer.channel)
    {
        wifi_rx_queue_note_drop(WIFI_RX_DROP_PROTOCOL, peer_channel);
#if RF_TEST_SERIAL_LOG == 1
        rf_log_rx_drop(peer_channel);
#endif
        // 요청 packet에 기록했던 channel과 응답 channel이 다르면 다른 transaction 응답으로 보고 버린다.
#if DEBUG_SERIAL_WIFI == 1
        Serial.printf("[MSG]WIFI::CALLBAK::Miss match channel::Response=%d vs Request=%d\r\n", peer_channel, wifi_state_machine[peer_channel].peer.channel);
#endif
        wifi_send.doing_recv_cb = false;
        return;
    }
    if (buff[WIFI_PACKET_TYPE] != wifi_state_machine[peer_channel].peer.device_type)
    {
        wifi_rx_queue_note_drop(WIFI_RX_DROP_PROTOCOL, peer_channel);
#if RF_TEST_SERIAL_LOG == 1
        rf_log_rx_drop(peer_channel);
#endif
        // 같은 channel이라도 device type이 다르면 PLC가 의도한 peer 응답이 아니다.
#if DEBUG_SERIAL_WIFI == 1
        Serial.printf("[MSG]WIFI::CALLBAK::Miss match type::Response=%d vs Request=%d\r\n", buff[WIFI_PACKET_TYPE], wifi_state_machine[peer_channel].peer.device_type);
#endif
        wifi_send.doing_recv_cb = false;
        return;
    }
    if (buff[WIFI_PACKET_ADDRESS] != wifi_state_machine[peer_channel].peer.device_addr)
    {
        wifi_rx_queue_note_drop(WIFI_RX_DROP_PROTOCOL, peer_channel);
#if RF_TEST_SERIAL_LOG == 1
        rf_log_rx_drop(peer_channel);
#endif
        // address까지 일치해야 현재 요청에 대한 응답으로 인정한다.
#if DEBUG_SERIAL_WIFI == 1
        Serial.printf("[MSG]WIFI::CALLBAK::Miss match address::Response=%d vs Request=%d\r\n", buff[WIFI_PACKET_ADDRESS], wifi_state_machine[peer_channel].peer.device_addr);
#endif
        wifi_send.doing_recv_cb = false;
        return;
    }

    // Pairing 전에는 MAC을 아직 모르므로 Type/Address로 찾지만, paired 이후 응답은
    // 현재 channel에 등록된 MAC과 일치해야 한다. 삭제 후 같은 channel 재사용 시의 오인식을 막는다.
    if (g_ap.peer.peer[peer_channel].pairFlag &&
        (memcmp(src_mac, g_ap.peer.peer[peer_channel].mac, sizeof(g_ap.peer.peer[peer_channel].mac)) != 0))
    {
        wifi_rx_queue_note_drop(WIFI_RX_DROP_SOURCE_MAC, peer_channel);
#if DEBUG_SERIAL_WIFI == 1
        Serial.printf("[MSG]WIFI::RX[%d]::SOURCE MAC MISMATCH\r\n", peer_channel);
#endif
        wifi_send.doing_recv_cb = false;
        return;
    }

    // CRC 체크
    crc16.flag.wd = crc16_modbus(CRC16_MODBUS_INIT_CODE, buff, packet_len - 2);
    if ((crc16.flag.bf.low != buff[packet_len - 2]) || (crc16.flag.bf.hi != buff[packet_len - 1]))
    {
        wifi_rx_queue_note_drop(WIFI_RX_DROP_CRC, peer_channel);
#if RF_TEST_SERIAL_LOG == 1
        rf_log_rx_drop(peer_channel);
#endif
#if DEBUG_SERIAL_WIFI == 1
        Serial.printf("[MSG]WIFI::CALLBAK::Miss match crc16::low=0x%02x hi=0x%02x vs packet[low]=0x%02x, packet[hi]=0x%02x\r\n", crc16.flag.bf.low, crc16.flag.bf.hi, buff[packet_len - 2], buff[packet_len - 1]);
#endif
        wifi_send.doing_recv_cb = false; //! 260305부 추가. CJL
        return;
    }

#if WIFI_RX_STRICT_EXPECTED_COMMAND == 1
    // transaction sequence byte가 없는 기존 protocol에서 할 수 있는 최소 상관 검증이다.
    // 현재 AP request와 다른 종류의 늦은 응답은 cache/state를 변경하지 못한다.
    if (buff[WIFI_PACKET_COMMAND] != rx_frame.expected_response_cmd)
    {
        wifi_rx_queue_note_drop(WIFI_RX_DROP_UNEXPECTED_COMMAND, peer_channel);
#if DEBUG_SERIAL_WIFI == 1
        Serial.printf("[MSG]WIFI::RX[%d]::UNEXPECTED CMD=0x%02X EXPECT=0x%02X\r\n",
                      peer_channel,
                      buff[WIFI_PACKET_COMMAND],
                      rx_frame.expected_response_cmd);
#endif
        wifi_send.doing_recv_cb = false;
        return;
    }
#endif

#if DEBUG_SERIAL_MONITOR == 1                                                           //! Modified... : CJL,
    Serial.printf("[MSG]WIFI::CALLBAK::Group=%d, IO Channel=%d, Type=%d, Addr=%d, Len=%d\r\n", buff[WIFI_PACKET_GROUP], buff[WIFI_PACKET_CHANNEL], buff[WIFI_PACKET_TYPE], buff[WIFI_PACKET_ADDRESS], buff[WIFI_PACKET_LENGTH]);
#endif

    g_ap.peer.peer_packet[peer_channel].peer_channel = peer_channel;
    g_ap.peer.peer_packet[peer_channel].paired       = g_ap.peer.peer[peer_channel].pairFlag;
    data_len = packet_len - 8;

    switch (buff[WIFI_PACKET_COMMAND])
    {
    case PEER_PAIRING_OK:
        // 신규 pairing 응답이다. peer 속성, MAC, 초기 cache를 저장하고
        // Wifi_Handle()가 pairing 완료 처리를 하도록 rx_pairing_response를 latch한다.
        if (data_len < 3)
        {
            wifi_rx_queue_note_drop(WIFI_RX_DROP_PROTOCOL, peer_channel);
#if DEBUG_SERIAL_WIFI == 1
            Serial.printf("[MSG]WIFI::CALLBAK[%d]::PAIRING_OK LEN ERR=%u\r\n", peer_channel, data_len);
#endif
            wifi_send.doing_recv_cb = false;
            return;
        }
        if (g_ap.peer.peer[peer_channel].pairFlag)
        {
            switch (wifi_state_machine[peer_channel].peer.state)
            {
            case WIFI_STATE_PAIRED:
            case WIFI_STATE_SEND:
                //wifi_state_machine[peer_channel].peer.response = true;
                rx_normal_response = true;
#if DEBUG_SERIAL_WIFI == 1
                Serial.printf("[MSG]WIFI::CALLBAK[%d]::PAIRING_OK\r\n", peer_channel);
#endif
                break;
            }
            break;
        }
        else
        {
        }
        //! 여기도 수정했음
        g_ap.peer.peer[peer_channel].io_usage = buff[WIFI_PACKET_DATA];
        g_ap.peer.peer[peer_channel].io_page = clamp_io_page_count(buff[WIFI_PACKET_DATA + 1]);
        g_ap.peer.peer[peer_channel].serial = buff[WIFI_PACKET_DATA + 2];

        /////////////////////////////////////////////////////////////////////
        // 프로토콜 수정 후 이거 안나오지 않을까 싶은데... 확인후 삭제
        // usage와 serial이 잘못 들어오는 경우가 있음.... 원인은 아직 못찾음
        // io usage가 1이 아니고, serial 데이타가 있음.....
        // 현재는 모드 1 WORD만 사용중
        if (g_ap.peer.peer[peer_channel].io_usage != 1)
        {
#if DEBUG_SERIAL_WIFI == 1
            Serial.printf("[MSG]WIFI::CALLBAK[%d]::PAIRING_OK::io_usage ERR\r\n", peer_channel);
#endif
            break;
        }
        /////////////////////////////////////////////////////////////////////

        //wifi_state_machine[peer_channel].pairing.response = true;
        rx_pairing_response = true;

        memcpy(g_ap.peer.peer[peer_channel].mac, src_mac, 6);                          // 응답 받은 peer mac 저장
        wifi_state_machine[peer_channel].peer.pMac = g_ap.peer.peer[peer_channel].mac; // 응답 받은 peer mac 저장

#if DEBUG_SERIAL_WIFI == 1
        Serial.printf("[MSG]WIFI::CALLBAK[%d]::PAIRING_OK:: usage=%d, page=%d, serial=%d\r\n",
            peer_channel,
            g_ap.peer.peer[peer_channel].io_usage,
            g_ap.peer.peer[peer_channel].io_page,
            g_ap.peer.peer[peer_channel].serial);
#endif
        wifi_state_machine[peer_channel].peer.device_type = buff[WIFI_PACKET_TYPE];

        /////////////////////////////////////////////////////////////////////
        // 연결 초기값 넣기 :: 기존에 정한 것

#if 0
#else
        if( g_ap.peer.peer[peer_channel].io_page > 0 )
        {   //! g_ap.data.rx_pairing_status로 바꿔야함 아직 안바꿈
            //! 20250919 : 자동 리페어링 때문에 생긴 사이드 이팩트 제거 : 아래 if문 추가
            if( !bitRead(g_ap.data.rx_pairing_status, peer_channel) )   //페어드 상태에서 재페어링이라면 데이타 초기화 하지 않음
            {
                for( i = 1; i < g_ap.peer.peer[peer_channel].io_page+1; i++ )
                {
                    g_ap.data.lPeerData[peer_channel][i] = i;
                    g_ap.data.lPeerData[peer_channel][i] <<= 12;
                    g_ap.data.lPeerData[peer_channel][i] += 2047;  // 0x7FF( 0111 1111 1111 )
                }
            }
        }
        else
        {
            g_ap.data.lPeerData[peer_channel][0] = 2047;
        }
#endif

        if ((g_ap.peer.peer[peer_channel].serial > 0) && is_serial_peer_channel(peer_channel))
        {
            // 시리얼 영역은 1이 12Ch임, 0 안씀
            const uint8_t serial_index = serial_peer_channel_to_index(peer_channel);
            for (i = 1; i < 11; i++)
            {
                g_ap.data.lSPeerData[serial_index][i] = 2047; // 0x7FF( 0111 1111 111 )
            }
        }
        /////////////////////////////////////////////////////////////////////

        if (pairing_register(peer_channel, 0))  wifi_state_machine[peer_channel].pairing.state = true;
        break;

    case PEER_PAIRING_CANCEL:
        // peer가 pairing cancel을 정상 처리했다.
        // callback에서 바로 지우지 않고 Wifi_Handle()에서 del_peer()를 수행하도록 event만 넘긴다.
#if DEBUG_SERIAL_WIFI == 1
        Serial.printf("[MSG]WIFI::CALLBAK[%d]::CANCEL PAIRING OK\n", peer_channel);
#endif
        rx_pairing_response = true;
        rx_pairing_cancel = true;
        //!callback에서 삭제 안하고 handle에서 하는 것으로 옮김
        //del_peer(peer_channel);
        break;

    case PEER_IO_GET:

        if (g_ap.peer.peer[peer_channel].pairFlag == true)
        {
            // AP_IO_GET 응답이다. peer가 보낸 IO 값을 lPeerData cache에 저장하고
            // 이후 Ethercat_Handle()가 이 cache를 PLC 입력 PDO로 올린다.
#if FUNC_PAIRED_VERIFY == 1
            bitWrite(g_ap.data.rx_pairing_status, peer_channel, 1); // 페어드 이후 데이타 요청이 성공 했을때 SET
#endif
            //wifi_state_machine[peer_channel].peer.response = true;
            rx_normal_response = true;
            if (g_ap.peer.peer[peer_channel].io_page == 0)
            {
                //page가 없는 I/O device이라면 page 0 사용
                const uint8_t copy_len = clamp_copy_len(data_len, sizeof(g_ap.data.lPeerData[peer_channel]));
                memcpy(&g_ap.data.lPeerData[peer_channel][0], &buff[WIFI_PACKET_DATA], copy_len);

#if DEBUG_SERIAL_MONITOR == 1
                if (buff[WIFI_PACKET_DATA] == 0 && buff[WIFI_PACKET_DATA + 1] == 0)
                {

                    if (g_ap.peer.peer_debug[peer_channel].zero < 0xffffffff)
                        g_ap.peer.peer_debug[peer_channel].zero++;
                    else
                        g_ap.peer.peer_debug[peer_channel].zero = 0xffffffff;
                    Serial.printf("[MSG]WIFI::CALLBAK[%d]::DATA RESPONSE[PAGE1]=0x%d, CNT=%d\n", peer_channel, g_ap.data.lPeerData[peer_channel][1], g_ap.peer.peer_debug[peer_channel].zero);
                }
#endif

#if DEBUG_SERIAL_WIFI == 1
                Serial.printf("[MSG]WIFI::CALLBAK[%d]::DATA RESPONSE[PAGE0]=0x%04x\n", peer_channel, g_ap.data.lPeerData[peer_channel][0]);
#endif
            }
            else
            {
                const uint8_t copy_len = clamp_copy_len(data_len, sizeof(g_ap.data.lPeerData[peer_channel]) - sizeof(g_ap.data.lPeerData[peer_channel][0]));
                memcpy(&g_ap.data.lPeerData[peer_channel][1], &buff[WIFI_PACKET_DATA], copy_len);

#if DEBUG_SERIAL_MONITOR == 1
                if (buff[WIFI_PACKET_DATA] == 0 && buff[WIFI_PACKET_DATA + 1] == 0)
                {

                    if (g_ap.peer.peer_debug[peer_channel].zero < 0xffffffff)
                        g_ap.peer.peer_debug[peer_channel].zero++;
                    else
                        g_ap.peer.peer_debug[peer_channel].zero = 0xffffffff;
                    Serial.printf("[MSG]WIFI::CALLBAK[%d]::DATA RESPONSE[PAGE1]=0x%d, CNT=%d\n", peer_channel, g_ap.data.lPeerData[peer_channel][1], g_ap.peer.peer_debug[peer_channel].zero);
                }
#endif

#if 0
                    Serial.printf("[MSG]WIFI::CALLBAK[%d]::DATA RESPONSE",peer_channel);
                    for( cnt=0 ; cnt<data_len ; cnt++)
                    {
                        Serial.printf("[%d]:0x%04x ",cnt, g_ap.data.lPeerData[peer_channel][cnt+1]);
                    }
                    Serial.printf("\r\n");
#endif
            }
        }
        break;

    case PEER_IO_SET:

        if (g_ap.peer.peer[peer_channel].pairFlag == true)
        {
            // AP_IO_SET에 대한 응답이다. 쓰기 완료 응답의 payload를 현재 IO cache에 반영한다.
#if FUNC_PAIRED_VERIFY == 1
            bitWrite(g_ap.data.rx_pairing_status, peer_channel, 1); // 페어드 이후 데이타 요청이 성공 했을때 SET
#endif
            //wifi_state_machine[peer_channel].peer.response = true;
            rx_normal_response = true;
        }
        else
        {
            break;
        }

        // set data 일때 응답 데이타가 동일한지 비교할 필요 없음
        if (g_ap.peer.peer[peer_channel].io_page == 0)
        {
            if (data_len < 2)
            {
                wifi_rx_queue_note_drop(WIFI_RX_DROP_PROTOCOL, peer_channel);
                break;
            }
            readData = (uint16_t)buff[WIFI_PACKET_DATA + 1];
            readData <<= 8;
            readData += buff[WIFI_PACKET_DATA];
#if 0

                if( wifi_state_machine[peer_channel].peer.set_io_data == readData )
                {
                    g_ap.data.lPeerData[peer_channel][0] = readData;
                    Serial.printf("[MSG]WIFI::CALLBAK[%d]::DATA SET:: Page=0, readData = 0x%04x\r\n",peer_channel, readData);
                }
#else
            g_ap.data.lPeerData[peer_channel][0] = readData;
            //Serial.printf("[MSG]WIFI::CALLBAK[%d]::DATA SET:: Page=0, readData = 0x%04x\r\n", peer_channel, readData);
#endif
        }
        else
        {
            // Check set page
            page = uint8_t(wifi_state_machine[peer_channel].peer.set_io_data >> 12);
            page = page & 0x07;

#if 0
                //6,7 = 1page, 8,9 = 2page, 10,11 = 3page 12,13 = 4page ~~~
                readData = (uint16_t)buff[page*2+5];
                readData <<= 8;
                readData += buff[page*2+4];

                Serial.printf("[MSG]WIFI::CALLBAK[%d]::DATA SET:: Page=0x%02x, Data = 0x%04d\r\n",peer_channel, page, readData);
                //응답 page의 데이타가 맞는지 확인
                if( wifi_state_machine[peer_channel].peer.set_io_data == readData )
                {
                    Serial.printf("RecvCB[%d]::AP_DATA_SET::DATA UPDATE\r\n",peer_channel);
                    //전체 페이지 업데이트
                    memcpy(&g_ap.data.lPeerData[peer_channel][1], (const uint8_t *)&data[4], len-4);
                }
#else
            const uint8_t copy_len = clamp_copy_len(data_len, sizeof(g_ap.data.lPeerData[peer_channel]) - sizeof(g_ap.data.lPeerData[peer_channel][0]));
            memcpy(&g_ap.data.lPeerData[peer_channel][1], &buff[WIFI_PACKET_DATA], copy_len);

#if 0
                Serial.printf("[MSG]WIFI::CALLBAK[%d]::DATA SET:: Page=0x%02x ", peer_channel, page);
                for( cnt=0 ; cnt<data_len ; cnt++)
                {
                    Serial.printf("[%d]:0x%04x ",cnt, g_ap.data.lPeerData[peer_channel][cnt+1]);
                }
                Serial.printf("\r\n");
#endif

#endif

        }
        break;

    case PEER_SERIAL_SET: //
        // 0 Page 없으므로 편의상 2차원 배열의 시작은 [][1]
        // 12 Ch는 1Ch로 처리함으로 -11 함

        if (is_serial_peer_channel(peer_channel))
        {
            // AP_SERIAL_SET 응답이다. peer의 IO 1word와 serial data block을 cache에 같이 반영한다.
#if DEBUG_SERIAL_WIFI == 1
            Serial.printf("[MSG]WIFI::CALLBAK[%d]::PEER_ALL_SDATA_RESPONSE\r\n", peer_channel);
#endif
            if (g_ap.peer.peer[peer_channel].pairFlag == true)
            {
                if (data_len < 2)
                {
                    wifi_rx_queue_note_drop(WIFI_RX_DROP_PROTOCOL, peer_channel);
                    break;
                }
#if FUNC_PAIRED_VERIFY == 1
                bitWrite(g_ap.data.rx_pairing_status, peer_channel, 1); // 페어드 이후 데이타 요청이 성공 했을때 SET
#endif
                // IO
                memcpy(&g_ap.data.lPeerData[peer_channel][0], &buff[WIFI_PACKET_DATA], 2);
                // SERIAL
                //wifi_state_machine[peer_channel].peer.response = true;
                //wifi_state_machine[peer_channel].peer.response_type_serial = true;
                rx_normal_response = true;
                rx_serial_response = true;
                const uint8_t serial_index = serial_peer_channel_to_index(peer_channel);
                const uint8_t serial_data_len = clamp_copy_len((uint8_t)(data_len - 2), sizeof(g_ap.data.lSPeerData[serial_index]));
                memcpy(&g_ap.data.lSPeerData[serial_index][0], &buff[WIFI_PACKET_DATA + 2], serial_data_len); // 12->0, 13->1, 14->2
            }
        }
        break;

    case PEER_SERIAL_GET: // SET과 같은데 일단 나누어 놓음.....
        if (is_serial_peer_channel(peer_channel))
        {
            // AP_SERIAL_GET 응답이다. PLC가 읽어갈 serial cache(lSPeerData)를 최신값으로 갱신한다.
#if DEBUG_SERIAL_WIFI == 1
            Serial.printf("[MSG]WIFI::CALLBAK[%d]::PEER_ALL_SDATA_RESPONSE\r\n", peer_channel);
#endif
            if (g_ap.peer.peer[peer_channel].pairFlag == true)
            {
                if (data_len < 2)
                {
                    wifi_rx_queue_note_drop(WIFI_RX_DROP_PROTOCOL, peer_channel);
                    break;
                }
#if FUNC_PAIRED_VERIFY == 1
                bitWrite(g_ap.data.rx_pairing_status, peer_channel, 1); // 페어드 이후 데이타 요청이 성공 했을때 SET
#endif

                // IO
                memcpy(&g_ap.data.lPeerData[peer_channel][0], &buff[WIFI_PACKET_DATA], 2);
                // SERIAL
                //wifi_state_machine[peer_channel].peer.response = true;
                //wifi_state_machine[peer_channel].peer.response_type_serial = true;
                rx_normal_response = true;
                rx_serial_response = true;
                const uint8_t serial_index = serial_peer_channel_to_index(peer_channel);
                const uint8_t serial_data_len = clamp_copy_len((uint8_t)(data_len - 2), sizeof(g_ap.data.lSPeerData[serial_index]));
                memcpy(&g_ap.data.lSPeerData[serial_index][0], &buff[WIFI_PACKET_DATA + 2], serial_data_len); // 12 = 0
            }
        }
        break;

    default:
        if (g_ap.peer.peer[peer_channel].pairFlag == true) //!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
        {
            // 정의하지 않은 command라도 paired peer에서 온 응답이면 transaction 종료용 일반 응답으로 처리한다.
            //wifi_state_machine[peer_channel].peer.response = true;
            rx_normal_response = true;
#if DEBUG_SERIAL_WIFI == 1
            Serial.printf("[MSG]WIFI::CALLBAK::Paired::Command Err=0x%02x\r\n", buff[WIFI_PACKET_COMMAND]);
#endif
        }
        break;
    }
    //! Evt flag latch랄까
    //if ((rx_pairing_response || rx_normal_response || rx_serial_response)  아래 추가. 소비안된 flag 있는데 cb가 또 덮어쓰는거 방지
    const bool rx_event_ready = rx_pairing_response || rx_normal_response ||
                                rx_serial_response || rx_pairing_cancel;

    // 같은 request에 대한 첫 정상 응답만 채택한다. Queue에 이미 들어온 duplicate도
    // request_epoch 불일치로 다음 service에서 폐기된다.
    if (rx_event_ready)
    {
        if (!wifi_rx_transaction_consume(&rx_frame))
        {
            wifi_rx_queue_note_drop(WIFI_RX_DROP_STALE_TRANSACTION, peer_channel);
            wifi_send.doing_recv_cb = false;
            return;
        }
        wifi_rx_queue_note_accepted(peer_channel);
    }

    #if TEMP_RX_EVENT_PENDING_MODE == 1
    if ((rx_pairing_response || rx_normal_response || rx_serial_response || rx_pairing_cancel) && !g_rx_event[peer_channel].pending)
    {
#if RF_TEST_SERIAL_LOG == 1
        g_rf_rx_ok_total[peer_channel]++;
#endif
        // callback에서 직접 wifi_state_machine 상태를 마무리하지 않고 event만 남긴다.
        // Wifi_Handle()가 rx_busy 상태에서 이 event를 소비해 response flag로 변환한다.
        g_rx_event[peer_channel].pairing_response = rx_pairing_response;
        g_rx_event[peer_channel].normal_response  = rx_normal_response;
        g_rx_event[peer_channel].serial_response  = rx_serial_response;
        g_rx_event[peer_channel].pairing_cancel   = rx_pairing_cancel;
        g_rx_event[peer_channel].cmd              = buff[WIFI_PACKET_COMMAND];
        g_rx_event[peer_channel].pending          = true;
    }
#if RF_TEST_SERIAL_LOG == 1
    else if (rx_pairing_response || rx_normal_response || rx_serial_response || rx_pairing_cancel)
    {
        rf_log_rx_drop(peer_channel);
    }
#endif
    #else
    if (rx_pairing_response || rx_normal_response || rx_serial_response || rx_pairing_cancel)
    {
#if RF_TEST_SERIAL_LOG == 1
        g_rf_rx_ok_total[peer_channel]++;
#endif
        if (rx_pairing_response) wifi_state_machine[peer_channel].pairing.response = true;
        if (rx_normal_response)  wifi_state_machine[peer_channel].peer.response = true;
        if (rx_serial_response)  wifi_state_machine[peer_channel].peer.response_type_serial = true;
        if (rx_pairing_cancel)   wifi_state_machine[peer_channel].pairing.state = true;
    }
    #endif

    wifi_send.doing_recv_cb = false;

}

static void Wifi_Rx_Service(void)
{
    wifi_rx_frame_t frame;

    // 한 번에 처리할 개수를 제한하여 RX burst가 EtherCAT/Wifi_Handle 실행을 독점하지 않게 한다.
    // loop() 매 회 호출되므로 backlog가 있으면 다음 loop에서 즉시 이어서 처리한다.
    for (uint8_t count = 0; count < WIFI_RX_SERVICE_BUDGET; ++count)
    {
        if (!wifi_rx_queue_pop(&frame)) break;
        Wifi_Rx_ProcessFrame(frame);
    }
}


bool wl_init()
{
    WiFi.mode(WIFI_STA);

    if (esp_now_init() != ESP_OK)
        return false;
    if (esp_wifi_start() != ESP_OK)
        return false;

#if 1
    if (esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT40) == ESP_OK)
    {
#if DEBUG_SERIAL_WIFI == 1
        Serial.printf("[MSG]WIFI::INIT::Set Bandwidth::OK\r\n");
#endif
    }
    else
    {
#if DEBUG_SERIAL_WIFI == 1
        Serial.printf("[MSG]WIFI::INIT::Set Bandwidth::ERR\r\n");
#endif
    }
#endif

#if 1
    esp_wifi_set_promiscuous(true);
    if (esp_wifi_set_channel(wifi_send.rf_set_channel, WIFI_SECOND_CHAN_NONE) == ESP_OK)
    {
#if DEBUG_SERIAL_WIFI == 1
        Serial.printf("[MSG]WIFI::INIT::Set CHANNEL::0x%02x\r\n", wifi_send.rf_set_channel);
#endif
    }
    else
    {
#if DEBUG_SERIAL_WIFI == 1
        Serial.printf("[MSG]WIFI::INIT::Set CHANNEL::ERR\r\n");
#endif
    }
    esp_wifi_set_promiscuous(false);
#endif

    if (esp_wifi_set_promiscuous(true) != ESP_OK)
        return false;
    if (esp_wifi_config_espnow_rate(WIFI_IF_STA, WIFI_PHY_RATE_5M_L) != ESP_OK)
        return false; // 1Mbps, 5Mbps, 11Mbps available
    if (esp_now_register_recv_cb(recv_cb) != ESP_OK)
        return false;
    if (esp_wifi_set_promiscuous_rx_cb(promiscuous_rx_cb) != ESP_OK)
        return false;
    // if (esp_now_register_send_cb(send_cb) != ESP_OK)
    //     return false;

    memcpy(&peerInfo.peer_addr, broadcast_addr, 6);
    if (esp_now_add_peer(&peerInfo) != ESP_OK)
        return false;

    if (esp_wifi_get_channel(&wifi_send.rf_get_channel, &wifi_send.rf_sencond_channel) == ESP_OK)
    {
#if DEBUG_SERIAL_WIFI == 1
        Serial.printf("[MSG]WIFI::INIT::GET CHANNEL::Primary=0x%02x\r\n", wifi_send.rf_get_channel);
#endif
    }
    else
    {
#if DEBUG_SERIAL_WIFI == 1
        Serial.printf("[MSG]WIFI::INIT::GET CHANNEL::ERR\r\n");
#endif
    }

    return true;
}

void debug()
{
    Serial.printf("**********디버깅 모드 시작********************\n");
    Serial.printf("시리얼 명령창 마지막에 뉴라인추가, 가로없이 ", "로 쓸것\n");
    Serial.printf("1.EthterCAT io영역 쓰기 : w,(index),(int value)\n");
    Serial.printf("2.EthterCAT io영역 읽기 : r\n");
}

void serial_cmd()
{
    if (Serial.available() > 0)
    {
        uint16_t *in_p = (uint16_t *)&EASYCAT.BufferIn.Cust.pairing_bit;
        uint16_t *out_p = (uint16_t *)&EASYCAT.BufferOut.Cust.pairing_bit;
        String input = "", _idx = "", _value = "";
        char prefix;
        uint8_t index = 0;
        uint16_t value = 0;
        uint8_t idx = 0;
        uint8_t idx2 = 0;

        input = Serial.readStringUntil('\n');
        prefix = input[0];

        debug();
        switch (prefix)
        {
        case 'w':

            idx = input.indexOf(',');
            idx2 = input.indexOf(',', idx + 1);
            _idx = input.substring(idx + 1, idx2);
            _value = input.substring(idx2 + 1);

            index = _idx.toInt();
            value = _value.toInt();

            Serial.printf("wirte ecat memory index# %d, value %d\n", index, value);
            memcpy(&in_p[index], (const uint16_t *)&value, 2);
            break;

        case 'r':
            for (int i = 0; i < 25; i++)
            {
                Serial.printf("output[%d]=%04X,  input[%d]=%04X\n", i, out_p[i], i, in_p[i]);
            }
            break;
        }
    }
}

void Initialize_PDO()
{
    uint8_t i;
    uint16_t ltemp;

    memset(&EASYCAT.BufferIn.Cust, 0, sizeof(EASYCAT.BufferIn.Cust));
}

bool Wifi_Set_Channel(uint8_t channel)
{
    bool rtn = true;

    esp_wifi_set_promiscuous(true);
    if (esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE) == ESP_OK)
    {
#if DEBUG_SERIAL_WIFI == 1
        Serial.printf("[MSG]WIFI::SET CHANNEL::Primary=0x%02x\r\n", channel);
#endif
    }
    else
    {
#if DEBUG_SERIAL_WIFI == 1
        Serial.printf("[MSG]WIFI::SET CHANNEL::ERR\r\n");
#endif
    }
    esp_wifi_set_promiscuous(false);

    return rtn;
}

void Wifi_Peer_Data_Set(uint8_t channel, uint8_t cmd, uint16_t *tx_data, uint8_t wlen)
{
    word_big_endian_t crc16;

    if (channel >= MAX_PEER) return;

    // 다음 request packet을 만들기 시작하면 이전 request의 대기 frame은 더 이상 유효하지 않다.
    wifi_rx_transaction_abort(channel);

    const uint16_t payload_len = (uint16_t)wlen * 2u;
    const uint16_t tx_len_u16 = (uint16_t)(8u + payload_len);

    if (tx_len_u16 > sizeof(wifi_state_machine[channel].peer.txBuf))
    {
    #if DEBUG_SERIAL_WIFI == 1
        Serial.printf("[MSG]WIFI::TX BUILD OVER::ch=%u tx_len=%u max=%u\r\n",
                    channel,
                    tx_len_u16,
                    (unsigned)sizeof(wifi_state_machine[channel].peer.txBuf));
    #endif
        return;
    }

    const uint8_t txLen = (uint8_t)tx_len_u16;
    const uint16_t len_crc = (uint16_t)(txLen - 2u);

    memset(wifi_state_machine[channel].peer.txBuf, 0, sizeof(wifi_state_machine[channel].peer.txBuf));

    wifi_state_machine[channel].peer.txLen = txLen;

    // Header (6 bytes). V4.0 packet layout has no FW/version prefix bytes.
    wifi_state_machine[channel].peer.txBuf[WIFI_PACKET_GROUP]   = wifi_send.rf_set_group;
    wifi_state_machine[channel].peer.txBuf[WIFI_PACKET_CHANNEL] = channel;
    wifi_state_machine[channel].peer.txBuf[WIFI_PACKET_COMMAND] = cmd;
    wifi_state_machine[channel].peer.txBuf[WIFI_PACKET_TYPE]    = wifi_state_machine[channel].peer.device_type;
    wifi_state_machine[channel].peer.txBuf[WIFI_PACKET_ADDRESS] = wifi_state_machine[channel].peer.device_addr;
    wifi_state_machine[channel].peer.txBuf[WIFI_PACKET_LENGTH]  = txLen;

    // Payload
    if (payload_len)    memcpy(&wifi_state_machine[channel].peer.txBuf[WIFI_PACKET_DATA], tx_data, payload_len);

    // CRC
    crc16.flag.wd = crc16_modbus(CRC16_MODBUS_INIT_CODE, wifi_state_machine[channel].peer.txBuf, len_crc);
    wifi_state_machine[channel].peer.txBuf[txLen - 2] = crc16.flag.bf.low;
    wifi_state_machine[channel].peer.txBuf[txLen - 1] = crc16.flag.bf.hi;

    wifi_send.tx_busy[channel] = false;
    wifi_send.rx_busy[channel] = false;
}

void Wifi_Peer_State_Set(WIFI_STATE_MACHINE state, uint8_t channel, uint8_t cmd, uint16_t typeAddr, uint16_t wLen)
{
    word_big_endian_t crc16;

    if (channel >= MAX_PEER)
    {
    #if DEBUG_SERIAL_WIFI == 1
        Serial.printf("[MSG]WIFI::STATE BUILD::INVALID CHANNEL=%u\r\n", channel);
    #endif
        return;
    }

    // Pairing add/cancel 재시도도 새로운 request로 취급한다.
    wifi_rx_transaction_abort(channel);
    // header8 + crc2, payload 없음
    const uint16_t tx_len_u16 = 8u;

    if (tx_len_u16 > sizeof(wifi_state_machine[channel].peer.txBuf))
    {
    #if DEBUG_SERIAL_WIFI == 1
        Serial.printf("[MSG]WIFI::STATE BUILD OVER::ch=%u tx_len=%u max=%u\r\n",
                    channel,
                    tx_len_u16,
                    (unsigned)sizeof(wifi_state_machine[channel].peer.txBuf));
    #endif
        return;
    }

    const uint8_t  txLen   = (uint8_t)tx_len_u16;
    const uint16_t len_crc = 6u;

    wifi_state_machine[channel].peer.channel = channel;

    if (typeAddr)
    {
        wifi_state_machine[channel].peer.device_type = (uint8_t)(typeAddr >> 8);
        wifi_state_machine[channel].peer.device_addr = (uint8_t)(typeAddr & 0x00ff);
    }

    // Header (6 bytes). V4.0 packet layout has no FW/version prefix bytes.
    wifi_state_machine[channel].peer.txBuf[WIFI_PACKET_GROUP]   = wifi_send.rf_set_group;
    wifi_state_machine[channel].peer.txBuf[WIFI_PACKET_CHANNEL] = channel;
    wifi_state_machine[channel].peer.txBuf[WIFI_PACKET_COMMAND] = cmd;
    wifi_state_machine[channel].peer.txBuf[WIFI_PACKET_TYPE]    = wifi_state_machine[channel].peer.device_type;
    wifi_state_machine[channel].peer.txBuf[WIFI_PACKET_ADDRESS] = wifi_state_machine[channel].peer.device_addr;
    wifi_state_machine[channel].peer.txBuf[WIFI_PACKET_LENGTH]  = txLen;

    // CRC
    crc16.flag.wd = crc16_modbus(CRC16_MODBUS_INIT_CODE, wifi_state_machine[channel].peer.txBuf, len_crc);
    wifi_state_machine[channel].peer.txBuf[txLen - 2] = crc16.flag.bf.low;
    wifi_state_machine[channel].peer.txBuf[txLen - 1] = crc16.flag.bf.hi;

    wifi_state_machine[channel].peer.txLen = txLen;
    wifi_state_machine[channel].peer.send_cmd = cmd;

    switch (state)
    {
    case WIFI_STATE_PAIRING_ADD:
        wifi_state_machine[channel].pairing.request = true;
        wifi_state_machine[channel].pairing.add = true;
        wifi_state_machine[channel].peer.pMac = broadcast_addr;
#if DEBUG_SERIAL_WIFI == 1
        Serial.printf("[MSG]WIFI::STATE SET::PAIRING ADD::PEER[%d] STATE=%d\r\n", channel, wifi_state_machine[channel].peer.state);
#endif
        break;

    case WIFI_STATE_PAIRING_DEL:
        wifi_state_machine[channel].pairing.request = true;
        wifi_state_machine[channel].pairing.del = true;
        wifi_state_machine[channel].peer.pMac = g_ap.peer.peer[channel].mac;
#if DEBUG_SERIAL_WIFI == 1
        Serial.printf("[MSG]WIFI::STATE SET::PAIRING DEL::PEER[%d] STATE=%d\r\n", channel, wifi_state_machine[channel].peer.state);
#endif
        break;

    default:
        break;
    }
}

void Wifi_Peer_MacAddr_Set(uint8_t addr, uint8_t *mac)
{
    wifi_state_machine[addr].peer.pMac = mac;
}

void Wifi_Peer_Rotation(void)
{
#if 0 // fast scan
    //페어드와 페어링 요청이 없는 peer는 pass
    unsigned char i=0;
    for( i=0 ; i<MAX_PEER ; i++)
    {
        if( ++wifi_send.peer_addr>=MAX_PEER ) wifi_send.peer_addr=0;

        if( g_ap.peer.peer[wifi_send.peer_addr].pairFlag || wifi_state_machine[wifi_send.peer_addr].pairing.request )
        {
            break;
        }
    }
#else // same time division rotation
    static unsigned char i = 0;
    if (++wifi_send.peer_addr >= MAX_PEER)
        wifi_send.peer_addr = 0;

#endif
}

void Wifi_Handle(void)
{
    // ESP-NOW 송신 상태 machine 함수. 
    // Ethercat_Handle()가 세운 update_io/update_serial/request_serial/pairing flag를 소비하고,
    // Core 1 RX frame worker가 남긴 response event를 받아 request 종료 및 상태 전이를 처리한다.
    static uint16_t cnt_loop = 0;                  // 현재 request timeout counter.
    static uint16_t setTime = SET_TIME_PAIRING;    // pairing/paired request에 적용되는 현재 timeout 기준값.

    uint8_t channel = wifi_send.peer_addr;
    uint8_t getAddr = 0;
    bool save_response = false;
    esp_err_t err;

    // timeout/state 판정보다 먼저, 현재까지 도착한 RX frame을 같은 Core 1에서 반영한다.
    Wifi_Rx_Service();

    if (wifi_send.doing_recv_cb)
    {
        // RX frame worker가 packet cache/event를 갱신 중이면 이번 주기 처리는 건너뛴다.
        return;
    }

    wifi_send.doing_handle = true;

    switch (wifi_state_machine[channel].peer.state)
    {
    case WIFI_STATE_READY:
        // 페어링 요청 확인하고 queue에 넣고 상태 변경
        wifi_state_machine[channel].pairing.timeout = 0;
        if (wifi_state_machine[channel].pairing.request == true)
        {
            // Ethercat_Handle() 또는 repair 흐름에서 pairing.request가 set된 채널이다.
            // queue에 넣어두면 아래 공통 송신 구간에서 AP_PAIRING_REQ packet이 전송된다.
            if (wifi_queue_enqueue(&wifi_send.queue, channel))
            {
                if (wifi_state_machine[channel].pairing.add)
                {
                    wifi_state_machine[channel].peer.state = WIFI_STATE_PAIRING_ADD;
                    cnt_loop = 0;
                    setTime = SET_TIME_PAIRING;

#if DEBUG_SERIAL_WIFI == 1
                    Serial.printf("[MSG]WIFI::HANDLE[%d]::PAIRING ADD\r\n", channel);
#endif

                }
            }
        }
        break;

    case WIFI_STATE_PAIRING_ADD:

        if (g_ap.peer.peer_bak[channel].repair_itself == true)
        {
            //  Wifi_Peer_State_Set(WIFI_STATE_PAIRING_ADD, peer_channel, AP_PAIRING_REQ, g_ap.peer.peer[peer_channel].typeAddr, 0);
        }

        break;

    case WIFI_STATE_PAIRED:
        // 페어링 요청 확인하고 queue에 넣고 상태 변경
        wifi_state_machine[channel].pairing.timeout = 0;
        if (wifi_state_machine[channel].pairing.request == true)
        {
            // 이미 paired된 채널에서 pairing.request가 살아 있으면 보통 삭제 요청 흐름이다.
            setTime = SET_TIME_PAIRING;
            if (wifi_queue_enqueue(&wifi_send.queue, channel))
            {
                if (wifi_state_machine[channel].pairing.del)
                {
                    // 페어링 된 상태에서 삭제 요청이 온 경우.
                    // AP_PAIRING_CANCEL packet은 이미 txBuf에 구성되어 있으므로 queue 송신만 기다린다.
                    wifi_state_machine[channel].peer.response = false; // 이전 call back 응답이 있을 수 있음.
                    wifi_state_machine[channel].peer.state = WIFI_STATE_PAIRING_DEL;
                    // pairing case는 delay를 더 100ms로 조정
                    cnt_loop = 0;

#if DEBUG_SERIAL_WIFI == 1
                    Serial.printf("[MSG]WIFI::HANDLE[%d]::STATE::PAIRED::DEL\r\n", channel);
#endif
                }
                else if (g_ap.peer.peer[channel].pairFlag == true) // 이미 페어링 되어 있음
                {
                    wifi_state_machine[channel].peer.state = WIFI_STATE_WAIT;
#if DEBUG_SERIAL_WIFI == 1
                    Serial.printf("[MSG]WIFI::HANDLE[%d]::STATE::PAIRED::WAIT\r\n", channel);
#endif
                }
                else
                {
                    // 이상한 상태 //초기화 시킴
                    memset(&wifi_state_machine[channel], 0, sizeof(wifi_state_machine[channel]));
                    del_peer(channel);

#if DEBUG_SERIAL_WIFI == 1
                    Serial.printf("[MSG]WIFI::HANDLE[%d]::STATE::PAIRED::UNKNOWN - DEL\r\n", channel);
#endif
                }
            }
            else
            {
#if DEBUG_SERIAL_WIFI == 1
                Serial.printf("[MSG]WIFI::HANDLE[%d]::STATE::PAIRED::NONE QUE\r\n", channel);
#endif
            }
        }
        else // paired인데, plc에서 pairing del 요청이 없을 경우 : 주기적으로 해당 peer에 데이타 요청
        {
            // 받아둔 데이타가 있다면
            if (wifi_state_machine[channel].peer.response)
            {
                // 이전 recv_cb event가 이미 일반 응답으로 변환된 상태다.
                // 새 request를 만들기 전에 먼저 응답 마무리 처리(save_response)로 빠진다.
                save_response = true;
                break;
            }

            if (wifi_state_machine[channel].peer.update_serial)
            {
                // AP의 Ethercat_Handle()가 PLC serial write 요청을 감지한 상태다.
                // flag를 소비하면서 AP_SERIAL_SET packet을 만든다.
                if (wifi_queue_enqueue(&wifi_send.queue, channel))
                {
                    wifi_state_machine[channel].peer.update_serial = false;
                    Wifi_Peer_Data_Set(channel, AP_SERIAL_SET, wifi_state_machine[channel].peer.set_serial_Buf, 9);
                    // 아래 초기화는 어차피 update_send 할 때 설정되기 때문에 없어도 괜찮음
                    cnt_loop = 0;
#if TEMP_SERIAL_TIMEOUT_EXTEND == 1
                    setTime = TEMP_SERIAL_SET_TIME_PAIRD;
#else
                    setTime = SET_TIME_PAIRD;
#endif
#if DEBUG_SERIAL_WIFI == 1
                    Serial.printf("[MSG]WIFI::HANDLE[%d]::STATE::PAIRED::REQUEST::UPDATE::SERIAL\r\n", channel);
#endif
                }
            }
            else if (wifi_state_machine[channel].peer.request_serial)
            {
                // AP의 Ethercat_Handle()가 PLC serial read 요청을 감지한 상태다.
                // flag를 소비하면서 AP_SERIAL_GET packet을 만든다.
                if (wifi_queue_enqueue(&wifi_send.queue, channel))
                {
                    wifi_state_machine[channel].peer.request_serial = false;
                    Wifi_Peer_Data_Set(channel, AP_SERIAL_GET, wifi_state_machine[channel].peer.set_serial_Buf, 9);
                    // 아래 초기화는 어차피 update_send 할때 설정되기 때문에 없어도 괜찮음
                    cnt_loop = 0;
#if TEMP_SERIAL_TIMEOUT_EXTEND == 1
                    setTime = TEMP_SERIAL_SET_TIME_PAIRD;
#else
                    setTime = SET_TIME_PAIRD;
#endif
#if DEBUG_SERIAL_WIFI == 1
                    Serial.printf("[MSG]WIFI::HANDLE[%d]::STATE::PAIRED::REQUEST::DATA::SERIAL\r\n", channel);
#endif
                }
            }
            else if (wifi_state_machine[channel].peer.update_io)
            {
                // Ethercat_Handle()가 PLC IO write 요청을 감지한 상태다.
                // flag를 소비하면서 AP_IO_SET packet을 만든다.
                if (wifi_queue_enqueue(&wifi_send.queue, channel))
                {
                    wifi_state_machine[channel].peer.update_io = false;
                    Wifi_Peer_Data_Set(channel, AP_IO_SET, &wifi_state_machine[channel].peer.set_io_data, 1);
                    // 아래 초기화는 어차피 update_send 할때 설정되기 때문에 없어도 괜찮음

                    cnt_loop = 0;
                    setTime = SET_TIME_PAIRD;
#if DEBUG_SERIAL_WIFI == 1
                    Serial.printf("[MSG]WIFI::HANDLE[%d]::STATE::PAIRED::REQUEST::UPDATE::IO\r\n", channel);
#endif
                }
            }
            else
            {
                // 별도 쓰기/serial 요청이 없으면 paired peer의 일반 주기 read로 처리한다.
                if (wifi_queue_enqueue(&wifi_send.queue, channel))
                {
                    setTime = SET_TIME_PAIRD;
                    Wifi_Peer_Data_Set(channel, AP_IO_GET, &wifi_state_machine[channel].peer.set_io_data, 1);
#if DEBUG_SERIAL_WIFI == 1
                    //! Modified... : CJL,
                    Serial.printf("[MSG]WIFI::HANDLE[%d]::STATE::PAIRED::REQUEST::DATA::IO=%d\r\n",channel,wifi_state_machine[channel].peer.set_io_data);
#endif
                }
            }

            wifi_state_machine[channel].peer.state = WIFI_STATE_SEND;
        }
        break;

    case WIFI_STATE_PAIRING_DEL:
        if (wifi_state_machine[channel].pairing.add == true) //pairing.del이 아니라? 한덕파트장님께 여쭤보기
        {
            if (++wifi_state_machine[channel].pairing.timeout > 5)
            {
                wifi_state_machine[channel].pairing.timeout = 0;
                wifi_state_machine[channel].peer.state = WIFI_STATE_READY;
#if DEBUG_SERIAL_WIFI == 1
                Serial.printf("[MSG]WIFI::HANDLE[%d]::STATE::PAIRING_DEL::TIMEOUT\r\n", channel);
#endif
            }
        }
        break;

    case WIFI_STATE_SEND:

        break;

    case WIFI_STATE_RECEIVE:
        break;

    case WIFI_STATE_WAIT:
        break;

    case WIFI_STATE_RETRY:
        //    Serial.printf("WIFI HANDLE[%d]::RE-TRY\r\n",channel);
        break;
    }

    if (wifi_queue_peek(&wifi_send.queue, &getAddr) &&
#if TEMP_QUEUE_GLOBAL_PEER_REQ_GATE == 1
        !wifi_send.peer_req &&
#endif
        !wifi_send.tx_busy[getAddr] &&
        !wifi_send.rx_busy[getAddr])

    {
        // 공통 송신 구간이다.
#if TEMP_QUEUE_GLOBAL_PEER_REQ_GATE == 1
        // peer_req가 false일 때만 새 request를 꺼내므로 현재 구조는 AP 전체에서 한 번에 하나만 송신 대기한다.
#else
        // legacy-like test: target channel이 busy가 아니면 전역 peer_req와 무관하게 queue request를 꺼낸다.
#endif
        channel = getAddr;
        wifi_queue_dequeue(&wifi_send.queue);
        cnt_loop = 0;
        wifi_send.tx_busy[channel] = true;
#if PLC_RSSI //! TEST2

        uint8_t *b = wifi_state_machine[channel].peer.txBuf;
        uint8_t  L = wifi_state_machine[channel].peer.txLen;

        Serial.printf("[AP->PEER TX] ch=%u cmd=0x%02X len=%u : ", channel, b[WIFI_PACKET_COMMAND], L);
        for (uint8_t i=0; i<L; i++) Serial.printf("%02X ", b[i]);
        Serial.println();
#endif
        // callback이 다른 core에서 즉시 실행될 수 있으므로 send API 호출 전에 transaction을 연다.
        // API 실패 시 아래 error 분기에서 곧바로 무효화한다.
        wifi_rx_transaction_begin(channel, wifi_state_machine[channel].peer.txBuf[WIFI_PACKET_COMMAND]);
        err = esp_now_send(wifi_state_machine[channel].peer.pMac, (const uint8_t *)wifi_state_machine[channel].peer.txBuf, wifi_state_machine[channel].peer.txLen);

        switch (err)
        {
        case ESP_OK:
#if RF_TEST_SERIAL_LOG == 1
            if (channel < MAX_PEER)
            {
                g_rf_tx_ok_total[channel]++;
            }
#endif
            // 송신 API 호출은 성공했다. 이제 이 채널의 응답을 기다리는 상태로 전환한다.
            wifi_send.tx_busy[channel] = false;
            wifi_send.rx_busy[channel] = true;
            wifi_send.peer_req = true;
#if DEBUG_SERIAL_MONITOR == 1
            Serial.printf("[MSG]WIFI::HANDLE[%d]::REQUEST SEND::Channel=0x%02x, Command=0x%02x, Type=0x%02x, Addr=0x%02x\r\n", channel, wifi_state_machine[channel].peer.txBuf[1], wifi_state_machine[channel].peer.txBuf[2], wifi_state_machine[channel].peer.txBuf[3], wifi_state_machine[channel].peer.txBuf[4]);
#endif
            break;

        //! 예외 case flag 처리 完, 기존 flow 내에서 진행함.(하단 timeout 루트 이동) 0511부 CJL

        case ESP_ERR_ESPNOW_NOT_FOUND:
            // peer 등록이 없어서 송신 자체가 실패한 경우다.
            // 응답 대기 상태로 가지 않고 현재 request만 해제한다.
#if RF_TEST_SERIAL_LOG == 1
            if (channel < MAX_PEER)
            {
                g_rf_send_fail_total[channel]++;
            }
#endif
            wifi_rx_transaction_abort(channel);
            wifi_send.tx_busy[channel] = false;
            wifi_send.rx_busy[channel] = false;
            wifi_send.peer_req = false;
            break;

        default:
            // 그 외 송신 실패도 응답을 기다릴 수 없으므로 request flag를 해제한다.
#if RF_TEST_SERIAL_LOG == 1
            if (channel < MAX_PEER)
            {
                g_rf_send_fail_total[channel]++;
            }
#endif
            wifi_rx_transaction_abort(channel);
            wifi_send.tx_busy[channel] = false;
            wifi_send.rx_busy[channel] = false;
            wifi_send.peer_req = false;
#if DEBUG_SERIAL_WIFI == 1
            Serial.printf("[MSG]WIFI::HANDLE[%d]::SEND ERR=%d\r\n", channel, err);
#endif
            break;
        }
    }

    if (wifi_send.tx_busy[channel])
    {
    }
    else if (wifi_send.rx_busy[channel]) //응답 왔는지 체크하고 상태 전이하는 진입점
    {
//! ===========================================================================================

    //? 이 구간 lock 걸어야 하나?

//! ===========================================================================================
        // check of peer response
#if TEMP_RX_EVENT_PENDING_MODE == 1
        if (g_rx_event[channel].pending)
        {
            // recv_cb()가 남긴 event를 이곳에서 소비한다.
            // 이후 기존 코드가 쓰던 pairing.response/peer.response flag로 변환한다.
            const bool pairing_rsp    = g_rx_event[channel].pairing_response;
            const bool normal_rsp     = g_rx_event[channel].normal_response;
            const bool serial_rsp     = g_rx_event[channel].serial_response;
            const bool pairing_cancel = g_rx_event[channel].pairing_cancel;

            g_rx_event[channel].pending = false;
            g_rx_event[channel].pairing_response = false;
            g_rx_event[channel].normal_response  = false;
            g_rx_event[channel].serial_response  = false;
            g_rx_event[channel].pairing_cancel   = false;
            g_rx_event[channel].cmd = 0;

            if (pairing_rsp) wifi_state_machine[channel].pairing.response = true;
            if (normal_rsp)  wifi_state_machine[channel].peer.response = true;
            if (serial_rsp)  wifi_state_machine[channel].peer.response_type_serial = true;
            if (pairing_cancel) wifi_state_machine[channel].pairing.state = true;
        }
#endif

        if (wifi_state_machine[channel].pairing.response)
        {
            // pairing 계열 응답을 받았으므로 현재 request cycle을 종료한다.
            wifi_send.peer_req = false;
            wifi_state_machine[channel].pairing.response = false;
            wifi_state_machine[channel].pairing.request = false;
            wifi_send.rx_busy[channel] = false;

            if (wifi_state_machine[channel].pairing.state == true) // 페어링 정상 응답 완료
            {
#if DEBUG_SERIAL_WIFI == 1
                Serial.printf("[MSG]WIFI::HANDLE[%d]::RESPONSE::PAIRING OK\r\n", channel);
#endif
                wifi_state_machine[channel].pairing.state = false;

                if (wifi_state_machine[channel].pairing.add)
                {
                    // 신규 pairing이 정상 완료된 경우. 이후 이 채널은 주기 데이터 교환 대상으로 들어간다.
                    wifi_state_machine[channel].pairing.add = false;
                    // set peer state
                    wifi_state_machine[channel].peer.state = WIFI_STATE_PAIRED;

                    wifi_state_machine[channel].pairing.retry = 0; //! 20250804. csot 현장에서 추가
#if DEBUG_SERIAL_PEER == 1
                    if (g_ap.peer.peer_debug[channel].paired)
                    {
                        if (g_ap.peer.peer_debug[channel].retry_pair_cnt < 0xffff)
                            g_ap.peer.peer_debug[channel].retry_pair_cnt++;
                    }
                    else
                    {
                        g_ap.peer.peer_debug[channel].paired = true;
                    }
#endif
                }
                else if (wifi_state_machine[channel].pairing.del)
                {
                    // pairing cancel 응답을 받은 경우. peer 정보를 실제로 삭제하고 state machine도 초기화한다.
                    del_peer(channel); //!cb에서 이쪽으로 옮김
                    wifi_state_machine[channel].pairing.del = false;
                    wifi_state_machine[channel].pairing.retry = 0; //! 20250804. csot 현장에서 추가
                    memset(&wifi_state_machine[channel], 0, sizeof(wifi_state_machine[channel]));
                }
            }
            else // 비정상
            {
                if (wifi_state_machine[channel].pairing.add && !g_ap.peer.peer[channel].pairFlag)
                {
                    // pairing add 응답은 왔지만 pairFlag가 set되지 않은 비정상 상태
                    // 제한 횟수 안에서는 다시 AP_PAIRING_REQ를 구성한다.
#if DEBUG_SERIAL_WIFI == 1
                    Serial.printf("[MSG]WIFI::HANDLE[%d]::RESPONSE::Add-NonPaired\r\n", channel);
#endif
                    // 페어링 추가 요청이었는데, 페어링이 안되면 제시도 3트

                    if (++wifi_state_machine[channel].pairing.retry > 3)
                    {
                        wifi_state_machine[channel].pairing.retry = 0;
                        memset(&wifi_state_machine[channel], 0, sizeof(wifi_state_machine[channel]));
                    }
                    else
                    {
                        //재시도
                        Wifi_Peer_State_Set(WIFI_STATE_PAIRING_ADD, channel, AP_PAIRING_REQ, g_ap.peer.peer[channel].typeAddr, 0);
                        wifi_state_machine[channel].peer.state = WIFI_STATE_READY;
#if DEBUG_SERIAL_WIFI == 1
                        Serial.printf("[MSG]WIFI::HANDLE[%d]::RESPONSE::NG - Retry\r\n", channel);
#endif
                    }
                }
                else if (wifi_state_machine[channel].pairing.del && g_ap.peer.peer[channel].pairFlag)
                {
                    // pairing delete 응답 후에도 pairFlag가 남아 있으면 삭제 실패로 보고 재시도한다.
// 페어링 제거 요청이었는데, 제거가 안됐다면
#if DEBUG_SERIAL_WIFI == 1
                    Serial.printf("[MSG]WIFI::HANDLE[%d]::RESPONSE::Del-Paired\r\n", channel);
#endif
                    if (++wifi_state_machine[channel].pairing.retry > 3)
                    {
                        wifi_state_machine[channel].pairing.retry = 0;
                        memset(&wifi_state_machine[channel], 0, sizeof(wifi_state_machine[channel]));
                        // peer 강제 제거
                        del_peer(channel);

                        // ethercat도 클리어...
                    }
                    else
                    {
                        Wifi_Peer_State_Set(WIFI_STATE_PAIRING_DEL, channel, AP_PAIRING_CANCEL, 0, 0);
                        wifi_state_machine[channel].peer.state = WIFI_STATE_READY;
                        wifi_send.tx_busy[channel] = false;
                    }
                }
                else
                {
                    // 예외처리, 페어링 프로세스 강제 초기화
                    memset(&wifi_state_machine[channel], 0, sizeof(wifi_state_machine[channel]));
                    g_ap.peer.peer[channel].pairFlag = false;
                    PairMask_Set(channel, false);
                    del_peer(channel);
                }
            }
        }
        else
        {
            // 일반 응답
            if (wifi_state_machine[channel].peer.response)
            {
                // IO/serial 일반 응답은 아래 save_response 블록에서 공통 마무리한다.
                save_response = true;
            }
        }
    }

    if (save_response)
    {
        // 일반 IO/serial 응답 처리 완료 지점이다.
        // RX frame worker가 이미 lPeerData/lSPeerData cache를 갱신했으므로 여기서는 busy와 timeout 관련 flag를 정리한다.
        wifi_state_machine[channel].peer.response = false;
        wifi_state_machine[channel].peer.cnt_disconnect = 0;
        wifi_send.rx_busy[channel] = false;
        wifi_send.peer_req = false;

        if (wifi_state_machine[channel].peer.response_type_serial)
        {
            // 이번 일반 응답이 serial 응답이면 표시 flag만 소모한다.
            // 실제 serial data는 RX frame worker에서 lSPeerData에 이미 복사되어 있다.
            wifi_state_machine[channel].peer.response_type_serial = false;

#if DEBUG_SERIAL_WIFI == 1
            Serial.printf("[MSG]WIFI::HANDLE[%d]::RESPONSE::Seiral::D[0]=%d\r\n", channel, g_ap.data.lPeerData[channel][0]);
#endif
        }

        if (g_ap.peer.peer[channel].io_page)
        {     
#if DEBUG_SERIAL_WIFI == 1
            Serial.printf("[MSG]WIFI::HANDLE[%d]::RESPONSE::IO-PAGE::D[1]=%d, D[2]=%d, D[3]=%d, D[4]=%d\r\n", channel, g_ap.data.lPeerData[channel][1], g_ap.data.lPeerData[channel][2], g_ap.data.lPeerData[channel][3], g_ap.data.lPeerData[channel][4]);
#endif
        }
        else
        {
#if DEBUG_SERIAL_WIFI == 1
            Serial.printf("[MSG]WIFI::HANDLE[%d]::RESPONSE::IO::D[0]=%d\r\n", channel, g_ap.data.lPeerData[channel][0]);
#endif
        }

        if (g_ap.peer.peer[channel].pairFlag == false)
        {
            memset(&wifi_state_machine[channel], 0, sizeof(wifi_state_machine[channel]));
        }

#if FUNC_REPAIRD_AUTO == 1
        g_ap.peer.peer_bak[channel].receiveLoss_cnt = 0;

        //!260916부 수정完...CJL : 재페어링 후 정상 응답 확인 시 auto-repair 진행 상태를 종료함.
        if (g_ap.peer.peer_bak[channel].repair_itself && g_ap.peer.peer[channel].pairFlag)
        {
            g_ap.peer.peer_bak[channel].repair_itself = false;
            g_ap.peer.peer_bak[channel].repair_cnt = 0;
        }
#endif
    }

    if (++cnt_loop > setTime) // 60ms, 60 * 16 = 960ms : this is 16 device scan loop time
    {
        // 현재 채널 기준 request timeout 처리 구간이다.
        // 응답이 정상적으로 오면 save_response/pairing.response에서 peer_req가 먼저 내려간다.
        cnt_loop = 0;
        wifi_send.tx_busy[channel] = false;

        // Send timeout
        if (wifi_send.peer_req) //응답이 없어서 flag가 살아 있다면
        {
            // 송신은 성공했지만 제한 시간 안에 peer의 응답 event가 오지 않은 경우
            // request를 닫고 disconnect counter를 증가시킨다.
#if RF_TEST_SERIAL_LOG == 1
            if (channel < MAX_PEER)
            {
                g_rf_timeout_total[channel]++;
            }
#endif
            wifi_send.peer_req = false;
#if TEMP_CLEAR_RX_BUSY_ON_REQ_TIMEOUT == 1
            wifi_rx_transaction_abort(channel);
            wifi_send.rx_busy[channel] = false; //! 기존; 최대 timeout 조건에 있던 것을 request timeout 종료 시 해당 채널 응답 대기도 같이 해제로 변경
#endif
            if (++wifi_state_machine[channel].peer.cnt_disconnect >= SETUP_DISCONNECT_MAX) // Disconnect Retry Time!!!!!!!!!!!!
            {
                // 강제 del
                wifi_state_machine[channel].peer.cnt_disconnect = 0;
                wifi_send.rx_busy[channel] = false; //!기존 위치

#if RF_TEST_SERIAL_LOG == 1
                if (channel < MAX_PEER)
                {
                    g_rf_disconnect_total[channel]++;
                    g_rf_last_disconnect_ms[channel] = millis();
                }
#endif

                if (g_ap.peer.peer[channel].pairFlag)
                {
                    // paired peer가 연속 timeout 한계에 도달하면 강제 삭제한다.
                    del_peer(channel);
                }

#if FUNC_REPAIRD_AUTO == 1
                if (g_ap.peer.peer_bak[channel].repair_itself == false)
                {
                    memset(&wifi_state_machine[channel], 0, sizeof(wifi_state_machine[channel]));
                }

#else
                memset(&wifi_state_machine[channel], 0, sizeof(wifi_state_machine[channel]));
#endif

#if DEBUG_SERIAL_MONITOR == 1
                Serial.printf("[MSG]WIFI::HANDLE[%d]::TIME OUT::Del Peer!!!!\r\n", channel);
#endif

                if (g_ap.peer.peer_debug[channel].del_cnt < 0xffffffff)
                {
                    g_ap.peer.peer_debug[channel].del_cnt++;
                }
            }
            else
            {
                // 페어드는 페어드 상태로
                if (g_ap.peer.peer[channel].pairFlag == true)
                {
#if DEBUG_SERIAL_MONITOR == 1
                    Serial.printf("[MSG]WIFI::HANDLE[%d]::TIME OUT::Disconnet Counter=%d\r\n", channel, wifi_state_machine[channel].peer.cnt_disconnect);
#endif
                    g_ap.peer.peer_bak[channel].receiveLoss_cnt++;
                    wifi_state_machine[channel].peer.state = WIFI_STATE_PAIRED;
                }
                else
                {
                    // 페어링 요청중이면 초기 상태로
                    wifi_rx_transaction_abort(channel);
                    wifi_send.rx_busy[channel] = false;
                    memset(&wifi_state_machine[channel], 0, sizeof(wifi_state_machine[channel]));

#if DEBUG_SERIAL_MONITOR == 1
                    Serial.printf("[MSG]WIFI::HANDLE[%d]::Wait Pairing\r\n", channel);
#endif
                }
            }
        }
        else
        {
            if (g_ap.peer.peer[channel].pairFlag == true) // 요청 상태 없이 타임아웃되었을 경우
            {
                if (wifi_state_machine[channel].pairing.del)
                {
                    // 삭제 상태(pairing.del)는 남아 있지만 현재 응답 대기(peer_req)는 아닌 경우.
                    // 송신 실패, queue/상태 꼬임, 또는 비정상 응답 처리 후 남은 삭제 상태를 정리 및 peer 삭제...
                    wifi_rx_transaction_abort(channel);
                    wifi_send.rx_busy[channel] = false;

#if DEBUG_SERIAL_MONITOR == 1
                    Serial.printf("[MSG]WIFI::HANDLE[%d]::TIME OUT::Del Peer::No response from paired peer, Pairing \r\n", channel);
#endif
                    memset(&wifi_state_machine[channel], 0, sizeof(wifi_state_machine[channel]));
                    del_peer(channel);
                }
                else
                {
                    wifi_state_machine[channel].peer.state = WIFI_STATE_PAIRED;
                }
            }
            else
            {
                // 예외처리 : delete를 진행했는데, 다른 응답으로 rx_busy가 클리어 되어 있을 경우.
                if (wifi_state_machine[channel].pairing.response)
                {
#if DEBUG_SERIAL_MONITOR == 1
                    Serial.printf("[MSG]WIFI::HANDLE[%d]::TIME OUT::Del Peer::No Rx Busy Check\r\n", channel);
#endif
                    memset(&wifi_state_machine[channel], 0, sizeof(wifi_state_machine[channel]));
                }

                else if (wifi_state_machine[channel].pairing.request)
                {
                    wifi_state_machine[channel].pairing.request = false;
                }

                else if (g_ap.peer.peer_bak[channel].repair_itself == true)
                {
#if FUNC_REPAIRD_AUTO == 1
                    if (++g_ap.peer.peer_bak[channel].repair_cnt < SETUP_REPAIR_MAX) // 자체 리페어링 시도 횟수
                    {
                        Wifi_Peer_State_Set(WIFI_STATE_PAIRING_ADD, channel, AP_PAIRING_REQ, g_ap.peer.peer[channel].typeAddr, 0);

                        g_ap.peer.peer_bak[channel].receiveLoss_cnt++;
#if DEBUG_SERIAL_MONITOR == 1
                        Serial.printf("[MSG]WIFI::HANDLE[%d]::TIME OUT::It self try repair, receiveLoss Cnt=%d\r\n", channel, g_ap.peer.peer_bak[channel].receiveLoss_cnt);
#endif
                    }
                    else // 연속 리페어링 실패
                    {
                        g_ap.peer.peer_bak[channel].receiveLoss_cnt = SETUP_RECEVIELOSS_MAX + 1;

#if DEBUG_SERIAL_MONITOR == 1
                        Serial.printf("[MSG]WIFI::HANDLE[%d]::TIME OUT::REPAIRING\r\n", channel);
#endif
                    }

                    if (g_ap.peer.peer_bak[channel].receiveLoss_cnt > SETUP_RECEVIELOSS_MAX) // 데이타를 하나도 받지 못함
                    {
#if DEBUG_SERIAL_MONITOR == 1
                        Serial.printf("[MSG]WIFI::HANDLE[%d]::End Auto Repairing\r\n", channel);
#endif
                        // End auto repairing
                        #if 0
                        memset(&g_ap.peer.peer_bak[channel], 0, sizeof(g_ap.peer.peer_bak[channel]));
                        memset(&g_ap.peer.peer[channel], 0, sizeof(g_ap.peer.peer[channel]));
                        bitWrite(g_ap.data.rx_pairing_status, channel, 0);
                        #else  //! 추가

                        PairMask_Set(channel, false);
                        wifi_send.tx_busy[channel] = false;
                        wifi_send.rx_busy[channel] = false;
                        wifi_rx_peer_invalidate(channel);
                        memset(&g_rx_event[channel], 0, sizeof(g_rx_event[channel]));
                        memset(&wifi_state_machine[channel], 0, sizeof(wifi_state_machine[channel]));

                        memset(&g_ap.peer.peer_bak[channel], 0, sizeof(g_ap.peer.peer_bak[channel]));
                        memset(&g_ap.peer.peer[channel], 0, sizeof(g_ap.peer.peer[channel]));
                        bitWrite(g_ap.data.rx_pairing_status, channel, 0);

                        #endif
                    }

#else
#endif
                }
            }
        }
#if DEBUG_SERIAL_MONITOR == 1
// Serial.printf("[MSG]WIFI::HANDLE[%d]::DEBUG del cnt=%d, zero=%d retry=%d\r\n",channel, g_ap.peer.peer_debug[channel].del_cnt, peer_debug[channel].zero, peer_debug[channel].retry_pair_cnt );
#endif
        Wifi_Peer_Rotation();
    }
    wifi_send.doing_handle = false;
}
// With core v2.0.0+, you can't use Serial.print/println in ISR or crash.
// and you can't use float calculation inside ISR
// Only OK in core v1.0.6-
bool IRAM_ATTR TimerHandler0(void *timerNo)
{
    g_ap.os.gSysTick.f_ms1 = 1;
    return true;
}

void Tick_Handle(void)
{
    g_ap.os.gSysTick.flag.wf = false;

    if (++g_ap.os.gSysTick.cnt_ms2 > 2)
    {
        g_ap.os.gSysTick.flag.bf.ms2 = true;
        g_ap.os.gSysTick.cnt_ms2 = 0;
    }

    if (++g_ap.os.gSysTick.cnt_ms5 > 5)
    {
        g_ap.os.gSysTick.flag.bf.ms5 = true;
        g_ap.os.gSysTick.cnt_ms5 = 0;
    }
    if (++g_ap.os.gSysTick.cnt_ms10 > 10)
    {
        g_ap.os.gSysTick.flag.bf.ms10 = true;
        g_ap.os.gSysTick.cnt_ms10 = 0;
    }

    if (++g_ap.os.gSysTick.cnt_ms25 > 25)
    {
        g_ap.os.gSysTick.flag.bf.ms25 = true;
        g_ap.os.gSysTick.cnt_ms25 = 0;
    }

    // if (++gSysTick.cnt_ms25 > 25)
    // {
    //     gSysTick.flag.bf.ms25 = true;
    //     gSysTick.cnt_ms25 = 0;
    // }

    if (++g_ap.os.gSysTick.cnt_ms50 > 50)
    {
        g_ap.os.gSysTick.flag.bf.ms50 = true;
        g_ap.os.gSysTick.cnt_ms50 = 0;
    }

    if (++g_ap.os.gSysTick.cnt_ms100 > 100)
    {
        g_ap.os.gSysTick.flag.bf.ms100 = true;
        g_ap.os.gSysTick.cnt_ms100 = 0;
    }

    if (++g_ap.os.gSysTick.cnt_ms500 > 500)
    {
        g_ap.os.gSysTick.flag.bf.ms500 = true;
        g_ap.os.gSysTick.cnt_ms500 = 0;
    }

    if (++g_ap.os.gSysTick.cnt_sec1 > 1000)
    {
        g_ap.os.gSysTick.flag.bf.sec1 = true;
        g_ap.os.gSysTick.cnt_sec1 = 0;
    }
}

void setup()
{
    Serial.begin(230400);
    wifi_queue_init(&wifi_send.queue);
    const bool wifi_rx_queue_ready = wifi_rx_queue_init();
#if AP_CLI
    ap_debug_cli_init();
#endif
    Set_GPIO(&g_ap);
    Read_Rotary(&g_ap);

    ap_led_cfg_t led_cfg =
    {
        .pixel_count = NUMPIXELS,
        .brightness = BRIGHTNESS,
        .pixel_gpio = PEER_CHK_LED,
        .onboard_led_gpio = LED,
    };

    peer_led_init_ctx(&g_led, &led_cfg);

    g_led_task_arg.led = &g_led;
    g_led_task_arg.app = &g_ap;

    g_led_drv_service_task = xTaskCreateStaticPinnedToCore(
        LedDrvServiceTask,
        "LED_DRV",
        2048,
        NULL,
        1,
        g_led_drv_service_task_stack,
        &g_led_drv_service_task_tcb,
        0
    );

    if (g_led_drv_service_task == NULL)
    {
        Serial.printf("[ERR]LED_DRV TASK CREATE FAIL\r\n");
        g_led.ready = false;
    }
    else
    {
        led_drv_bind_service_task(g_led_drv_service_task);
        peer_led_setup(&g_led);
    }

#if PLC_RSSI
    for (int i = 0; i < MAX_PEER; i++)  g_ap.peer.rssi[i] = -127;
#endif
    Serial.println("");


    if (EASYCAT.Init()) Serial.printf("[MSG]SYSTEM INFO::WIFI::ECAT OK\r\n");
    else    Serial.printf("[MSG]SYSTEM INFO::WIFI::ECAT NG\r\n");

    if (wifi_rx_queue_ready && wl_init())
    {
        Serial.printf("[MSG]SYSTEM INFO::WIFI::INIT OK, RXQ=%u x %u bytes\r\n",
                      (unsigned)WIFI_RX_QUEUE_DEPTH,
                      (unsigned)sizeof(wifi_rx_frame_t));
    }
    else
    {
        // RX Queue가 없으면 callback frame을 안전하게 처리할 수 없으므로 Wi-Fi 정상으로 보고하지 않는다.
        Serial.printf("[MSG]SYSTEM INFO::WIFI::INIT NG, RXQ=%u\r\n", wifi_rx_queue_ready ? 1u : 0u);
    }

    uint32_t ll32temp;

    EASYCAT.SPIWriteRegisterIndirect(g_ap.board.iAddr, ALIAS_REG_H, 2);
    delay(100);

    ll32temp = EASYCAT.SPIReadRegisterIndirect(ALIAS_REG_H, 2);
    Serial.printf("[MSG]SYSTEM INFO::ECAT::0x0012 : 0x%x(%d)\n", ll32temp, ll32temp);

    Initialize_PDO();
    delay(100);

    EASYCAT.MainTask();

    g_led_task_handle = xTaskCreateStaticPinnedToCore(
        LedTask,
        "LedTask",
        8000,
        &g_led_task_arg,
        1,
        g_led_task_stack,
        &g_led_task_tcb,
        0
    );
    if (g_led_task_handle == NULL)
    {
        Serial.printf("[ERR]LED TASK CREATE FAIL\r\n");
    }
    //! 얘는 바인드 필요 없을 듯 ? 위에도 굳이?
    ////////////////////////////////////////////////////////////////////////////////
    // Using ESP32  => 80 / 160 / 240MHz CPU clock ,
    // For 64-bit timer counter
    // For 16-bit timer prescaler up to 1024
    // Interval in microsecs

    if (ITimer0.attachInterruptInterval(TIMER0_INTERVAL_MS * 1000, TimerHandler0))
    {
        Serial.print(F("[MSG]SYSTEM INFO::Starting  ITimer0 OK\r\n"));
        // Serial.println(millis());
    }
}

void loop()
{
    static unsigned int input_cnt = 0;
    static bool cnt_set = false;

    // RX frame 해석/cache 갱신은 항상 Core 1의 loop 문맥에서 먼저 수행한다.
    Wifi_Rx_Service();

    if (g_ap.os.gSysTick.f_ms1)
    {
        g_ap.os.gSysTick.f_ms1 = 0;
        Tick_Handle();
        Wifi_Handle();

        if (g_ap.os.gSysTick.flag.bf.ms10)      Ethercat_Handle();
        if (g_ap.os.gSysTick.flag.bf.ms25)
        {
            if (!digitalRead(SW))
            {
                if (cnt_set == false)
                {
                    if (++input_cnt > 80)
                    {
                        debug_out ^= 1;
                        cnt_set = true;
                    }
                }
            }
            else
            {
                cnt_set = false;
                input_cnt = 0;
            }
        }
#if RF_TEST_SERIAL_LOG == 1
        if (g_ap.os.gSysTick.flag.bf.sec1)
        {
            Log_RF_Summary_1s();
        }
#endif
        EASYCAT.MainTask();
    }
#if AP_CLI
    ap_debug_cli_service();
#endif
} // end loop

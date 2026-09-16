
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
//! 10100 setting, W출장 debug 확인때문에 
#define DEBUG_SERIAL_SYSTEM 1
#define DEBUG_SERIAL_ECAT 0
#define DEBUG_SERIAL_WIFI 1
#define DEBUG_SERIAL_MONITOR 0
#define DEBUG_SERIAL_PEER 0 //NG 
//! 5.5G 장비 TEST log
#define CHINA_TEST           1
#define CHINA_TEST_RF        1


#define FUNC_REPAIR_SAVE_DATA 0     // 사용하지 않음
#define FUNC_PAIRED_VERIFY 1        // 데이타 요청 성공 이후부터 업데이트 V3.3 업데이트 기능
#define FUNC_REPAIRD_AUTO 1         // 연결되었었던 PEER 자체 연결 재시도 기능, V3.3 업데이트

#if FUNC_REPAIRD_AUTO == 1

#define SETUP_REPAIR_MAX 10         // 자체 Repair 시도 횟수
#define SETUP_RECEVIELOSS_MAX 19    // DATA 입력이 없는 최대 시간 : looptime * n, SETUP_DISCONNECT_MAX +

#endif

#define SETUP_DISCONNECT_MAX 10     // 데이타 재요청 최대 횟수

// 무선 Network config
#define MAX_SERIAL_PEER 4           // serial을 사용하는 peer 최대수
#define MAX_CHANNEL 13              // 2.4GHz channel 한국/중국 1~13

// 부품 Type and id define
#define DIW_FLOW 0x01
#define CDA_FLOW 0x02
#define SMART_DAMPER 0x03
#define X_RAY 0x04
#define D40A 0x05
#define D4SL 0x06
#define TIC 0x07
#define LMFC 0x08
#define MANOMETER 0x09
#define LCT 0x0A

// Reqeuset & response HEX
#define AP_PAIRING_REQ 0x01
#define AP_PAIRING_CANCEL 0x02
#define AP_IO_GET 0x03
#define AP_IO_SET 0x04
#define AP_SERIAL_SET 0x05
#define AP_SERIAL_GET 0x06 

#define PEER_PAIRING_OK 0x81
#define PEER_PAIRING_CANCEL 0x82
#define PEER_IO_GET 0x83
#define PEER_IO_SET 0x84
#define PEER_SERIAL_SET 0x85
#define PEER_SERIAL_GET 0x86

#define MAX_PEER 16                 // peer 최대수
#define MAX_IO_BUFF 4               // in 16bit word
#define MAX_SERIAL_BUFF 70
#define MAX_SERIAL_PEER 4
#define MAX_PAIRING_BUFF_SIZE 16
#define MAX_SEND_BUFF 40            // in word

#define MAX_IO_PEER 12

// LAN9252
#define ALIAS_REG 0x0012
#define ALIAS_REG_H 0x0012
#define ALIAS_REG_L 0x0013

// Hardware IO
#define SW 0                        // Onboard SW
#define LED 2                       // Onboard LED Blue
#define MUXInput1 36                // sensor_VP pin 
#define NRESET 4                    //
#define MUX_16EN1 32                //
#define MUX_SEL0 27                 //
#define MUX_SEL1 26                 //
#define MUX_SEL2 33                 //
#define MUX_SEL3 25                 //
//#define PEER_CHK_LED    12          // gpio12 mapping

#define OSCILLOSCOPE    0

#if OSCILLOSCOPE == 1
#define TEST_POINT      14
#define TP_HIGH() digitalWrite(TEST_POINT, HIGH)
#define TP_LOW()  digitalWrite(TEST_POINT, LOW)
#endif

#define Board       "AP"          //CJL:temp...
#define VERSION     "v4.00"          //CJL:temp...



typedef struct
{
    uint8_t mac[6];    // mac 주소
    uint16_t typeAddr; // typeID
    bool pairFlag;     // 페어링 상태, 알람 발생 시 false
    uint8_t io_usage;  // IO영역 점유 word (In/out)
    uint8_t io_page;   // IO영역 페이지 사용, 1이상일 경우 Tx요청에 의해 출력값 선택
    uint8_t serial;    // Serial. page 사용, 0일 경우 Serial 미사용, 1이상일 경우 Serial. 사용

} peer_t;

typedef struct _peer_bak_t
{
    uint8_t mac[6];    // mac 주소
    uint16_t typeAddr; // typeID
    uint8_t io_usage;  // IO영역 점유 word (In/out)
    uint8_t io_page;   // IO영역 페이지 사용, 1이상일 경우 Tx요청에 의해 출력값 선택
    uint8_t serial;    // Serial. page 사용, 0일 경우 Serial 미사용, 1이상일 경우 Serial. 사용

    // 연결 후 disconnect and repair
    uint8_t repair_cnt;
    uint8_t del_cnt;
    bool repair_itself; // ap자체적으로 repair 시도
    bool del_set;

    // Data Recevie 확인.
    uint8_t receiveLoss_cnt;

    uint32_t wait;

} peer_bak_t;

typedef struct
{
    uint8_t channel;
    uint32_t zero;
    uint32_t del_cnt;

    uint32_t wait;

    bool paired;
    uint32_t retry_pair_cnt;

} peer_debug_t;

peer_debug_t peer_debug[MAX_PEER];
peer_bak_t peer_bak[MAX_PEER];
////////////////////////Peer 정보//////////////////////////
peer_t peer[MAX_PEER];

#if ATOMIC == 1
std::atomic<uint32_t> g_pairMask{0}; //! 추가해봄 TEST
#endif

int RSSI[MAX_PEER];
esp_now_peer_info_t peerInfo;

bool pairing_req_flag = false;
bool pairing_init_flag = false;

//////////////////esp_now통신 ///////////////////////////////////////////////////
uint8_t broadcast_addr[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}; // broadcast Mac address

///////////////////// recv_callback buffer /////////////////////
uint8_t pairing_buffer_cnt = 0;
uint8_t pairing_buffer[MAX_PEER][MAX_PAIRING_BUFF_SIZE] = {
    0,
};

uint16_t rx_serial_status = 0;
uint16_t rx_pairing_status = 0;
uint16_t rx_io_buffer[MAX_PEER][MAX_IO_BUFF] = {
    0,
}; // data buffer   [index][size in word]
uint16_t rx_serial_buffer[MAX_SERIAL_PEER][MAX_SERIAL_BUFF] = {
    0,
}; // data buffer   [index-13][size in word]

uint16_t srx_io_buffer[13];
uint16_t srx_serial_buffer[9];

uint16_t tx_serial_buffer[MAX_SERIAL_PEER][MAX_SERIAL_BUFF] = {
    0,
}; // data buffer   [index-13][size in word]
// uint16_t tx_pairing_status=0;
uint16_t tx_io_buffer[MAX_PEER][MAX_IO_BUFF] = {
    0,
}; // data buffer   [index][size in word]

unsigned long preTime = 0;
unsigned long pTime = 0;

//////////////////////// 세마포어 ////////////////////////
portMUX_TYPE pMUX = portMUX_INITIALIZER_UNLOCKED;
// portMUX_TYPE sMUX = portMUX_INITIALIZER_UNLOCKED;

SemaphoreHandle_t send_sem;
SemaphoreHandle_t recv_sem;
SemaphoreHandle_t tx_sem;
SemaphoreHandle_t rx_sem;

uint8_t pair_counter = 0;

uint8_t debug_out = 0;
// add, HJun
// uint8_t iRSSI_Cnt;
// uint32_t l32RSSI_Cnt;
// uint32_t llRSSI_Cnt;
// uint8_t iPeer_RSSI[12][101] = { 0, };
// min이 1쪽, max가 99쪽; 절댓값으로 처리하기때문에 일단은 반대임
// uint8_t iRSSI_Min[MAX_PEER];
// uint8_t iRSSI_Max[MAX_PEER];
// uint8_t iRSSI_Avr[MAX_PEER];
// uint16_t lPeerData[MAX_IO_PEER][3];          // AP rev24; IO영역 2페이지 기준 테스트진행
// uint16_t lPeerData[MAX_IO_PEER+1][5];             // AP rev26; IO영역 최대 4페이지; 0페이지 없음 -> 조건에따라 0페이지 있음
uint16_t lPeerData[MAX_IO_PEER + 1][7];       // rev26이 잘못되어 있음....이런...미처버려 IO영역 최대 6페이지; 0페이지 없음 -> 조건에따라 0페이지 있음
uint16_t lSPeerData[MAX_SERIAL_PEER + 1][81]; // AP rev29; 240409 시리열영역 최대 데이터 70워드;
// uint16_t lSPeerData[MAX_SERIAL_PEER+1][17][9];    // AP rev27; 시리열영역 최대 비트상 16페이지; 0채널, 0페이지 없음

// add, HJun; rev25
uint8_t iRWBit[16]; //

uint16_t iAddr;
uint8_t iAddr1, iAddr2;

void Set_GPIO();
void Mux_Sel_16ch(uint8_t Ch);
void Read_Rotary();

void Initialize_PDO();

#define SET_TIME_PAIRING 35 // 59
#define SET_TIME_PAIRD 20   // 29

ESP32Timer ITimer0(0);
void Tick_Handle(void);

typedef struct _sys_tick_t
{
    bool f_ms1;

    uint16_t cnt_ms2;
    uint16_t cnt_ms5;
    uint16_t cnt_ms10;
    uint16_t cnt_ms25;
    uint16_t cnt_ms50;
    uint16_t cnt_ms100;
    uint16_t cnt_ms250;
    uint16_t cnt_ms500;
    uint16_t cnt_ms750;
    uint16_t cnt_sec1;

    union
    {
        uint16_t wf;
        struct
        {
            uint16_t ms2 : 1;
            uint16_t ms5 : 1;
            uint16_t ms10 : 1;
            uint16_t ms25 : 1;

            uint16_t ms50 : 1;
            uint16_t ms100 : 1;
            uint16_t ms250 : 1;
            uint16_t ms500 : 1;

            uint16_t ms750 : 1;
            uint16_t sec1 : 1;
            uint16_t sec2 : 1;
            uint16_t sec3 : 1;

            uint16_t sec4 : 1;
            uint16_t sec5 : 1;
            uint16_t sec10 : 1;
            uint16_t min1 : 1;
        } bf;
    } flag;
} sys_tick_t;

sys_tick_t gSysTick;
/**
 * wifi send를 시분할 하기 위한 작업
 *
 *
 *
 */
#define WIFI_HANDLE 1
#define WIFI_SEND_BUF_MAX 40
#define WIFI_SEND_QUEUE_MAX 50

typedef struct _wifi_state_machine_t
{
    struct
    {
        bool request;
        bool response;
        bool add;
        bool del;
        bool state;

        uint8_t retry;
        uint16_t timeout;
    } pairing;

    struct
    {
        bool rw;
        bool response;
        bool response_type_serial;
        bool update_io;
        bool update_serial;
        bool request_serial;

        uint8_t state;
        uint8_t *pMac;
        uint8_t device_type;
        uint8_t device_addr;
        uint8_t channel;
        uint8_t send_cmd;
        uint8_t txBuf[WIFI_SEND_BUF_MAX];
        uint16_t txLen;
        uint8_t repair_counter;
        uint8_t repair_state;
        uint32_t repair_timeout;

        uint16_t set_io_data;
        uint16_t set_serial_Buf[WIFI_SEND_BUF_MAX];
        uint16_t bak_send;
        uint32_t cnt_disconnect;
    } peer;

} wifi_state_machine_t;

wifi_state_machine_t wifi_state_machine[MAX_PEER];

typedef struct _wifi_send_t
{
    struct
    {
        uint8_t tail;
        uint8_t head;
        uint8_t item[WIFI_SEND_QUEUE_MAX];
    } queue;

    bool tx_busy[MAX_PEER];
    bool rx_busy[MAX_PEER];

    uint8_t peer_addr;
    bool peer_req;

    bool doing_recv_cb;
    bool doing_handle;

    uint8_t paired_cnt;

    uint8_t rf_set_channel;
    uint8_t rf_get_channel;

    uint8_t rf_set_group;

    wifi_second_chan_t rf_sencond_channel;

} wifi_send_t;

wifi_send_t wifi_send;

uint16_t *null_ptr = 0;

void Ethercat_Handle(void);
bool Wifi_enQueue(uint8_t item);
void Wifi_deQueue(void);
bool Wifi_getQueue(uint8_t *item);
void Wifi_Peer_Data_Set(uint8_t peer_addr, uint8_t query, uint16_t *tx_data, uint8_t wlen);
void Wifi_Peer_State_Set(WIFI_STATE_MACHINE state, uint8_t addr, uint8_t cmd, uint16_t typeAddr, uint16_t wLen);
void Wifi_Peer_MacAddr_Set(uint8_t addr, uint8_t *mac);
void Wifi_Peer_Rotation(void);
void Wifi_Handle(void);
//////////////////////////////////////////////////////////////////////////////////////////////

uint8_t count_setBit(uint16_t bitField)
{ // word의 1인 bit수 count : brian kernighan algorithm
    uint8_t cnt = 0;
    while (bitField)
    {
        bitField &= (bitField - 1);
        cnt++;
    }
    return cnt;
}

///////// esp-now 통신 관련 함수 ////////////////////////////////

////////////// callback 함수내에서 사용 ////////////////////////
uint16_t get_typeId(const uint8_t *_data)
{
    uint16_t *typeId = (uint16_t *)&_data[2];
    return *typeId;
}

uint8_t get_query(const uint8_t *_data)
{
    return _data[0];
}

uint8_t get_index(const uint8_t *_data)
{
    return _data[1];
}

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
        if (memcmp(peer[i].mac, hdr->addr2, 6) == 0)
        {
            RSSI[i] = ppkt->rx_ctrl.rssi;
            break;
        }
    }
}
//! =========================================================위해 test 


static void Log_RF_Summary_1s(void)
{
#if CHINA_TEST_RF
    const uint32_t mask_snap = PairMask_Snapshot();
    const uint16_t plc_pair  = EASYCAT.BufferOut.Cust.pairing_bit;
    const uint16_t ap_pair   = rx_pairing_status;

    Serial.printf(
        "\r\n[TEST][RF] set_ch=%u get_ch=%u group=%u plc_pair=0x%04X ap_pair=0x%04X mask=0x%04lX\r\n",
        wifi_send.rf_set_channel,
        wifi_send.rf_get_channel,
        wifi_send.rf_set_group,
        plc_pair,
        ap_pair,
        (unsigned long)mask_snap
    );

    for (uint8_t i = 0; i < MAX_PEER; i++)
    {
        const bool paired = PairMask_Test(mask_snap, i);

        if (!paired &&
            peer_debug[i].del_cnt == 0 &&
            peer_bak[i].receiveLoss_cnt == 0 &&
            RSSI[i] == 0)
        {
            continue;
        }
/*
*
//!@@@@@@@@@@@@@@친절한 설명@@@@@@@@@@@@@@@@@@@@@@@@
* set_ch : AP rotary에서 읽은 설정 채널
* get_ch : ESP32가 현재 실제로 잡고 있는 실채널
* plc_pair : PLC가 AP에 요청한 pairing bit
ex) plc_pair=0x0007
0x0007 = 0000 0000 0000 0111b
→ CH0, CH1, CH2를 PLC가 붙이라고 요청 중
* ap_pair : AP 내부 rx_pairing_status
AP가 현재 PLC에 올려주는  pair 상태
* mask : 무선) 사실상 실제 현 시점 pair 상태

ex) [TEST][RF][CH02] pair=1 rssi=-85 typeAddr=0x0603 loss=9 del=1 repair=1 req=1 rxBusy=1 txBusy=0

* pair
mask 기준 현재 paired 여부
1이면 붙어 있음
0이면 안 붙어 있음

* rssi
-127: 아직 못 잡았거나 초기값 처리 상태

* typeAddr
peer 장치 타입 + 주소 정보
예: 0x0603
상위 byte: type
하위 byte: id/addr

* loss
receiveLoss_cnt : 데이터를 제때 못 받은 횟수 누적 성격

loss=0 → 안정적


* del : delete 관련 이력/counter
del=0 → 아직 삭제 이력 없음
del이 올라감 → 실제로 AP가 peer를 지운 이력이 있d음 

즉 del이 보이면:
단순 weak RSSI 수준이 아니라
실제 연결 끊김/삭제까지 간 적이 있음
* repair : auto repair 상태 여부
1이면 AP가 재연결 시도 흐름에 들어간 상태
ex) 
repair=0 → 정상 또는 아직 복구 단계는 아님
repair=1 → 이미 끊김/이상 판단 후 자체 복구 시도 중

* req : 현재 Wi-Fi 요청 처리 중인지?(처리 흐름 참고용)
계속 1로 오래 유지되면 처리 정체를 의심할 수 있음
rxBusy, txBusy
현재 해당 채널의 수신/송신 busy 상태
ex) 
rxBusy=1 → 수신 쪽 처리 중
txBusy=1 → 송신 중
*
*
*
*
*

*/

        Serial.printf(
            "[TEST][RF][CH%02u] pair=%u rssi=%d typeAddr=0x%04X loss=%u del=%lu repair=%u req=%u rxBusy=%u txBusy=%u\r\n",
            i,
            paired ? 1 : 0,
            RSSI[i],
            peer[i].typeAddr,
            peer_bak[i].receiveLoss_cnt,
            (unsigned long)peer_debug[i].del_cnt,
            peer_bak[i].repair_itself ? 1 : 0,
            wifi_send.peer_req ? 1 : 0,
            wifi_send.rx_busy[i] ? 1 : 0,
            wifi_send.tx_busy[i] ? 1 : 0
        );
    }
#endif
}
//! =========================================================위해 test end  

#if 1
void set_board_Version(void)
{
    Serial.printf("\n");
    Serial.printf("[MSG]BOARD INFO::%s::VERSION:%s\r\n",Board,VERSION);//CJL:temp...
    Serial.printf("\n");
}
#endif 

void del_peer(uint8_t index)
{ // delete peer from pairing info and index
#if FUNC_REPAIRD_AUTO == 1
    if (peer[index].pairFlag)
    {
#if 0
        if( ++peer_bak[index].del_cnt>2 ) 
        {
            bitWrite(rx_pairing_status, index, 0);
            memset(&peer_bak[index], 0, sizeof(peer_bak[index]) );

#if DEBUG_SERIAL_MONITOR == 1
            Serial.printf("[MSG]WIFI::MODULE::DEL PEER[%d]::peer_bak clear\r\n", index);
#endif            
        }else
        {
            peer_bak[index].repair_itself=true;
            peer_bak[index].repair_cnt=0;
            //backup 
            peer_bak[index].typeAddr = peer[index].typeAddr;

#if DEBUG_SERIAL_MONITOR == 1
            Serial.printf("[MSG]WIFI::MODULE::DEL PEER[%d]::Itself retry repair, typeAddr=%04x, cnt=%d\r\n", index, peer_bak[index].typeAddr, peer_bak[index].del_cnt);
#endif
        }
#else   //! 여기 구간 mask Set/Reset 추가 여부 생각 좀.  
        //! peer에는 등록되어 있지만 PLC에 등록되어 있지 않다면 강제 삭제라고 정리함 
        if (!peer_bak[index].del_set) //! peer가 삭제될 때 del set여부에 따라(false면 자체 repair, true면 의도 삭제이므로 repair진행x? )
        {
            peer_bak[index].repair_itself = true;
            peer_bak[index].repair_cnt = 0;
            // backup
            peer_bak[index].typeAddr = peer[index].typeAddr;
            
            // retry data request와 repair후에도 지속적으로 페어링만 되고 데이타를 못 받았을 경우
            // 최대 20sec(변경 필요) 넘어서면 바로 repair_itself를 cancel하고 ecat에 disconnect report

#if DEBUG_SERIAL_MONITOR == 1
            Serial.printf("[MSG]WIFI::MODULE::DEL PEER[%d]::Itself retry repair, typeAddr=%04x, cnt=%d\r\n", index, peer_bak[index].typeAddr, peer_bak[index].del_cnt);
#endif
        }   
        else
        {
            bitWrite(rx_pairing_status, index, 0);
            peer_bak[index].del_set = false;
            peer_bak[index].receiveLoss_cnt = 0;
        }

#endif
    }
#else  
    bitWrite(rx_pairing_status, index, 0);
#endif

    peer[index].pairFlag = false;
#if ATOMIC == 1
    PairMask_Set(index, false); //! 추가 TEST 
#endif 
    esp_now_del_peer(peer[index].mac);
    memset(&peer[index], 0, sizeof(peer[index]));

    wifi_state_machine[index].pairing.del = false;

#if FUNC_REPAIRD_AUTO == 1
    if (peer_bak[index].repair_itself == true)
    {
        memset(&wifi_state_machine[index], 0, sizeof(wifi_state_machine[index]));
        peer[index].typeAddr = peer_bak[index].typeAddr;
        Wifi_Peer_State_Set(WIFI_STATE_PAIRING_ADD, index, AP_PAIRING_REQ, peer_bak[index].typeAddr, 0);
    }
#endif

#if DEBUG_SERIAL_WIFI == 1
    Serial.printf("[MSG]WIFI::MODULE::DEL PEER[%d]::Removed from peer list\r\n", index);
#endif
}

bool pairing_register(uint8_t idx, uint16_t channel)
{
    // esp_err_t pairing_result;
    memcpy(&peerInfo.peer_addr, peer[idx].mac, 6);
    peerInfo.channel = channel;
    peerInfo.encrypt = false;
    esp_err_t pairing_result = esp_now_add_peer(&peerInfo);

    if (pairing_result == ESP_OK)
    {
        peer[idx].pairFlag = true;
#if ATOMIC == 1
        PairMask_Set(idx, true); //! 추가 TEST
#endif 

#if FUNC_PAIRED_VERIFY == 0
        bitWrite(rx_pairing_status, idx, 1); // 페어드 이후 데이타 요청이 성공 했을때 SET
#endif

#if DEBUG_SERIAL_WIFI == 1
        Serial.printf("[MSG]WIFI::MODULE::REGISTER[%2d] : OK\n", idx);
#endif
        // 페어링 완료

        return true;
    }
    else if (pairing_result == ESP_ERR_ESPNOW_EXIST)
    {
        peer[idx].pairFlag = true;
#if ATOMIC == 1
        PairMask_Set(idx, true); //! 추가 TEST 
#endif 

#if FUNC_PAIRED_VERIFY == 0
        bitWrite(rx_pairing_status, idx, 1); // 페어드 이후 데이타 요청이 성공 했을때 SET
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
        if (peer[i].pairFlag == true)
        {
            if ( bitRead(tx_p_status, i) == 0 )     
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

void Set_GPIO()
{
    // Input Setting
    pinMode(SW, INPUT); // ESP32 Onabrd SW
    pinMode(MUXInput1, INPUT);

    // Output Setting
    pinMode(LED, OUTPUT); // ESP32 Onboard Blue LED
    pinMode(NRESET, OUTPUT);
    pinMode(MUX_16EN1, OUTPUT);
    pinMode(MUX_SEL0, OUTPUT);
    pinMode(MUX_SEL1, OUTPUT);
    pinMode(MUX_SEL2, OUTPUT);
    pinMode(MUX_SEL3, OUTPUT);
    //! peer check LED 
    pinMode(PEER_CHK_LED,OUTPUT);
#if OSCILLOSCOPE == 1
    pinMode(TEST_POINT,OUTPUT);
#endif

#if DEBUG_SERIAL_SYSTEM == 1
    Serial.printf("[MSG]SYSTEM SETUP::MODULE::Set GPIO OK\n");
#endif
}

void Mux_Sel_16ch(uint8_t Ch)
{
    switch (Ch)
    { // S3, S2, S1, S0
    case 0:
        digitalWrite(MUX_SEL3, LOW);
        digitalWrite(MUX_SEL2, LOW);
        digitalWrite(MUX_SEL1, LOW);
        digitalWrite(MUX_SEL0, LOW);
        break;
    case 1:
        digitalWrite(MUX_SEL3, LOW);
        digitalWrite(MUX_SEL2, LOW);
        digitalWrite(MUX_SEL1, LOW);
        digitalWrite(MUX_SEL0, HIGH);
        break;
    case 2:
        digitalWrite(MUX_SEL3, LOW);
        digitalWrite(MUX_SEL2, LOW);
        digitalWrite(MUX_SEL1, HIGH);
        digitalWrite(MUX_SEL0, LOW);
        break;
    case 3:
        digitalWrite(MUX_SEL3, LOW);
        digitalWrite(MUX_SEL2, LOW);
        digitalWrite(MUX_SEL1, HIGH);
        digitalWrite(MUX_SEL0, HIGH);
        break;
    case 4:
        digitalWrite(MUX_SEL3, LOW);
        digitalWrite(MUX_SEL2, HIGH);
        digitalWrite(MUX_SEL1, LOW);
        digitalWrite(MUX_SEL0, LOW);
        break;
    case 5:
        digitalWrite(MUX_SEL3, LOW);
        digitalWrite(MUX_SEL2, HIGH);
        digitalWrite(MUX_SEL1, LOW);
        digitalWrite(MUX_SEL0, HIGH);
        break;
    case 6:
        digitalWrite(MUX_SEL3, LOW);
        digitalWrite(MUX_SEL2, HIGH);
        digitalWrite(MUX_SEL1, HIGH);
        digitalWrite(MUX_SEL0, LOW);
        break;
    case 7:
        digitalWrite(MUX_SEL3, LOW);
        digitalWrite(MUX_SEL2, HIGH);
        digitalWrite(MUX_SEL1, HIGH);
        digitalWrite(MUX_SEL0, HIGH);
        break;
    case 8:
        digitalWrite(MUX_SEL3, HIGH);
        digitalWrite(MUX_SEL2, LOW);
        digitalWrite(MUX_SEL1, LOW);
        digitalWrite(MUX_SEL0, LOW);
        break;
    case 9:
        digitalWrite(MUX_SEL3, HIGH);
        digitalWrite(MUX_SEL2, LOW);
        digitalWrite(MUX_SEL1, LOW);
        digitalWrite(MUX_SEL0, HIGH);
        break;
    case 10:
        digitalWrite(MUX_SEL3, HIGH);
        digitalWrite(MUX_SEL2, LOW);
        digitalWrite(MUX_SEL1, HIGH);
        digitalWrite(MUX_SEL0, LOW);
        break;
    case 11:
        digitalWrite(MUX_SEL3, HIGH);
        digitalWrite(MUX_SEL2, LOW);
        digitalWrite(MUX_SEL1, HIGH);
        digitalWrite(MUX_SEL0, HIGH);
        break;
    case 12:
        digitalWrite(MUX_SEL3, HIGH);
        digitalWrite(MUX_SEL2, HIGH);
        digitalWrite(MUX_SEL1, LOW);
        digitalWrite(MUX_SEL0, LOW);
        break;
    case 13:
        digitalWrite(MUX_SEL3, HIGH);
        digitalWrite(MUX_SEL2, HIGH);
        digitalWrite(MUX_SEL1, LOW);
        digitalWrite(MUX_SEL0, HIGH);
        break;
    case 14:
        digitalWrite(MUX_SEL3, HIGH);
        digitalWrite(MUX_SEL2, HIGH);
        digitalWrite(MUX_SEL1, HIGH);
        digitalWrite(MUX_SEL0, LOW);
        break;
    case 15:
        digitalWrite(MUX_SEL3, HIGH);
        digitalWrite(MUX_SEL2, HIGH);
        digitalWrite(MUX_SEL1, HIGH);
        digitalWrite(MUX_SEL0, HIGH);
        break;
    default:
        break;
    }
    delay(10);
}

void Read_Rotary()
{
    uint8_t i = 0;
    iAddr1 = 0;
    iAddr2 = 0;

    uint8_t readR3 = 0, readR4 = 0;

    digitalWrite(MUX_16EN1, HIGH);
    delay(100);

    Mux_Sel_16ch(i);
    if (!digitalRead(MUXInput1))
        iAddr2 += 4;
    i++;
    Mux_Sel_16ch(i);
    if (!digitalRead(MUXInput1))
        iAddr2 += 1;
    i++;
    Mux_Sel_16ch(i);
    if (!digitalRead(MUXInput1))
        iAddr2 += 8;
    i++;
    Mux_Sel_16ch(i);
    if (!digitalRead(MUXInput1))
        iAddr2 += 2;
    i++;

    Mux_Sel_16ch(i);
    if (!digitalRead(MUXInput1))
        iAddr1 += 4;
    i++;
    Mux_Sel_16ch(i);
    if (!digitalRead(MUXInput1))
        iAddr1 += 1;
    i++;
    Mux_Sel_16ch(i);
    if (!digitalRead(MUXInput1))
        iAddr1 += 8;
    i++;
    Mux_Sel_16ch(i);
    if (!digitalRead(MUXInput1))
        iAddr1 += 2;

    i++;
    Mux_Sel_16ch(i);
    if (!digitalRead(MUXInput1))
        readR4 += 4;
    i++;
    Mux_Sel_16ch(i);
    if (!digitalRead(MUXInput1))
        readR4 += 1;
    i++;
    Mux_Sel_16ch(i);
    if (!digitalRead(MUXInput1))
        readR4 += 8;
    i++;
    Mux_Sel_16ch(i);
    if (!digitalRead(MUXInput1))
        readR4 += 2;
    i++;

    Mux_Sel_16ch(i);
    if (!digitalRead(MUXInput1))
        readR3 += 4;
    i++;
    Mux_Sel_16ch(i);
    if (!digitalRead(MUXInput1))
        readR3 += 1;
    i++;
    Mux_Sel_16ch(i);
    if (!digitalRead(MUXInput1))
        readR3 += 8;
    i++;
    Mux_Sel_16ch(i);
    if (!digitalRead(MUXInput1))
        readR3 += 2;

    digitalWrite(MUX_16EN1, LOW);
    if ((readR3 > 14) || (readR3 == 0)) readR3 = 1;
    if (readR4 > 15)    readR4 = 1;

    wifi_send.rf_set_channel = readR3;
    wifi_send.rf_set_group = readR4;

    iAddr = (iAddr2 * 16) + iAddr1;

#if DEBUG_SERIAL_SYSTEM == 1
    Serial.printf("[MSG]SYSTEM::INIT::Address =%d, WIFI Group= %d,WIFI Channel=%d\r\n", iAddr, wifi_send.rf_set_group, wifi_send.rf_set_channel);
#endif
}

void Ethercat_Handle(void)
{
    static uint16_t bakData = 0;

    // ethercAT 값 Scan 또는 pairing 해야할 목록 업데이트
    // rev25 : IO영역(시리얼을 포함하는)에 대해 PDO맵상 R/W 비트 확인하여 피어에 설정데이터 송신
    static uint8_t peer_channel = 0;

    static uint16_t tx_pairStatus;
    static uint16_t rx_pairStatus;

    static uint16_t *tx_ptr;
    static uint16_t *rx_ptr;

    static uint16_t tempData[17];
    static uint16_t sendData[2];

    static uint16_t tx_serial;   // 녹색, Serial 영역 Data1, Ch, Page 영역
    static uint16_t *serial_ptr; // 녹색, Serial 영역 Data2 ~, 시리얼 데이터 영역

    static uint8_t tx_w_ch;
    static uint8_t tx_w_page;
    static uint8_t tx_r_ch;
    static uint8_t tx_r_page;

    uint8_t tmpCh = 0;

    bool req_pair = false;
    bool w_serial = false;

    uint8_t device_id, device_type, page;

    tx_pairStatus = EASYCAT.BufferOut.Cust.pairing_bit; // PLC에서 보낸 pairing데이터
    tx_ptr = (uint16_t *)&EASYCAT.BufferOut.Cust.data1; // 녹색 plc에서 보낸 데이터
    rx_ptr = (uint16_t *)&EASYCAT.BufferIn.Cust.data1;  // 적색 mcu 데이터 
    // rx

    EASYCAT.BufferIn.Cust.pairing_bit = rx_pairing_status; // 적색; rx_pairing status update    //mcu쪽 정보를 plc가 읽어갈 수 있게 

    if (gSysTick.flag.bf.sec1)
    {
#if DEBUG_SERIAL_ECAT == 1
        Serial.printf("[MSG]ECAT::Request Pair List = 0x%04x\r\n", tx_pairStatus);
#endif
    }

    if (bitRead(tx_pairStatus, peer_channel))
        req_pair = true;

    if (req_pair)
    {
        device_type = (uint8_t)(tx_ptr[peer_channel] >> 8);
        device_id = (uint8_t)(tx_ptr[peer_channel] & 0x00ff);
        // 페어링이 안되어 있고, device id와 device type가 있을 경우(id, type은 ap slave map상의 용어)
        if (!peer[peer_channel].pairFlag && device_type && device_id)
        {
            if (wifi_state_machine[peer_channel].pairing.request == false) // 요청중이지 않을때만 전송
            {
                peer[peer_channel].typeAddr = tx_ptr[peer_channel];

                Wifi_Peer_State_Set(WIFI_STATE_PAIRING_ADD, peer_channel, AP_PAIRING_REQ, peer[peer_channel].typeAddr, 0);

#if DEBUG_SERIAL_ECAT == 1
                Serial.printf("[MSG]ECAT::Request Pair[%d]::New Add::TypeAddress=0x%04x\r\n", peer_channel, peer[peer_channel].typeAddr);
#endif
            }
        }
        else if (peer[peer_channel].pairFlag && device_type && device_id)
        {
            // 페어링 성공 했고, 데이타는 아직 안 받았을때 : 데이타 요청 처리를 해야 함.
            if (peer_channel < 12)
            {
#if 0 
                if( peer[peer_channel].io_page )  //read and page
                {
                    page = 1;
                    lPeerData[peer_channel][0] = 0; //0은 page가 없을때의 데이타 영역. 쓰레기 값이 들어갈 수 있음
                }else   //read and no page
                {
                    page = 0;

                    wifi_state_machine[peer_channel].peer.set_io_data = 0;
                }
#endif
            }
            else if ((peer_channel < 16) && peer[peer_channel].serial)
            {
                //! tmpCh 세팅 전일텐데
                if (!wifi_state_machine[tmpCh].peer.update_serial && !wifi_state_machine[tmpCh].peer.request_serial) // Serial Device 12-15
                {
                    wifi_state_machine[tmpCh].peer.request_serial = true;

                    memset(&wifi_state_machine[tmpCh].peer.set_serial_Buf[1], 0, 16);
                    // device comm command
                    wifi_state_machine[tmpCh].peer.set_serial_Buf[1] = 0;
                }
            }
        }
        else if (peer[peer_channel].pairFlag && !tx_ptr[peer_channel]) // peer는 등록되어 있고, ethercat에는 없을 경우
        {
            if (wifi_state_machine[peer_channel].pairing.request == false) // 요청중이지 않을때만 전송
            {
#if FUNC_REPAIRD_AUTO == 1
                memset(&peer_bak[peer_channel], 0, sizeof(peer_bak[peer_channel]));
                peer_bak[peer_channel].del_set = true;
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
        if (peer[peer_channel].pairFlag) // paired 일때
        {
#if 0 
            switch( wifi_state_machine[peer_channel].peer.pairedStart_Proc )
            {
                case 0:
                    wifi_state_machine[peer_channel].peer.pairedStart_Proc=1;
                    if( ++peer_channel>=MAX_PEER ) peer_channel=0;    

                break;
            }
#endif
            //* page 있는 IO 애들 쓸 때 같음 
            //! 20250105 : Serial Deviceeh io를 사용할 수 있기 때문에 15까지 검색해서 해야 하는데, 시스템에서 아직 듀얼로는 사용 안하니까
            //! 추후에 시스템과 협의해서 작업 필요. 코드는 만들어서 넣어놨음..검증필요!!!!
            if (peer_channel < 12)
            {
                if ((tx_ptr[peer_channel] & BIT15) && (peer[peer_channel].io_page)) // write and page ... 1:W, 0:R
                {
                    page = (tx_ptr[peer_channel] & 0x7000) >> 12; //* PDO 상 12-14bit page 할당됨
                    // Write
                    if (!wifi_state_machine[peer_channel].peer.update_io)
                    {
                        wifi_state_machine[peer_channel].peer.update_io = true;
                        wifi_state_machine[peer_channel].peer.set_io_data = tx_ptr[peer_channel];
                    }

                    lPeerData[peer_channel][0] = 0; // 0은 page가 없을때의 데이타 영역. 쓰레기 값이 들어갈 수 있음
                }
                else if (peer[peer_channel].io_page) // read and page
                {
                    page = (tx_ptr[peer_channel] & 0x7000) >> 12;

                    lPeerData[peer_channel][0] = 0; // 0은 page가 없을때의 데이타 영역. 쓰레기 값이 들어갈 수 있음
                }
                else // read and no page
                {
                    page = 0;

                    wifi_state_machine[peer_channel].peer.set_io_data = tx_ptr[peer_channel]; // page가 없을때도 쓰기 데이타를 계속 보내야 함
                }
                memcpy(&rx_ptr[peer_channel], (const uint16_t *)&lPeerData[peer_channel][page], 2);
            }
            else if ((peer_channel < 16) && peer[peer_channel].serial)
            {
                // IO와 SERIAL은 같은 채널 동기화 시켜서 처리 : 그래야지 무선으로 데이타를 한번에 보낼 수 있음

                tx_serial = EASYCAT.BufferOut.Cust.com1; //* PDO 상 Serial Device Comm Command W17
                tx_w_ch = (tx_serial & 0xF000) >> 12; //1~4 범위
                tx_w_page = (tx_serial & 0x0F00) >> 8;
                tx_r_ch = (tx_serial & 0x00F0) >> 4;
                tx_r_page = tx_serial & 0x000F;

                if (tx_w_ch && tx_w_page)
                {
                    tmpCh = 11 + tx_w_ch; //12-15로 변환 

                    if (!wifi_state_machine[tmpCh].peer.update_serial) // Serial Device 12-15
                    {
                        // write
                        // 채널 요청 정보가 있을때
                        wifi_state_machine[tmpCh].peer.update_serial = true;
                        wifi_state_machine[tmpCh].peer.request_serial = false;
                        memcpy(&wifi_state_machine[tmpCh].peer.set_serial_Buf[1], &EASYCAT.BufferOut.Cust.com1, 16);
                    }
                }

                if (tx_r_ch && tx_r_page)
                {
                    tmpCh = 11 + tx_r_ch;
                    // read
                    serial_ptr = (uint16_t *)&EASYCAT.BufferIn.Cust.com1; // 적색
                    //peer data를 com1-8로 덮어쑈ㅡㅁ
                    memcpy(&serial_ptr[0], (const uint16_t *)&lSPeerData[tx_r_ch - 1][((tx_r_page - 1) * 8) + 0], 2); //* com1-8 
                    memcpy(&serial_ptr[1], (const uint16_t *)&lSPeerData[tx_r_ch - 1][((tx_r_page - 1) * 8) + 1], 2);
                    memcpy(&serial_ptr[2], (const uint16_t *)&lSPeerData[tx_r_ch - 1][((tx_r_page - 1) * 8) + 2], 2);
                    memcpy(&serial_ptr[3], (const uint16_t *)&lSPeerData[tx_r_ch - 1][((tx_r_page - 1) * 8) + 3], 2);
                    memcpy(&serial_ptr[4], (const uint16_t *)&lSPeerData[tx_r_ch - 1][((tx_r_page - 1) * 8) + 4], 2);
                    memcpy(&serial_ptr[5], (const uint16_t *)&lSPeerData[tx_r_ch - 1][((tx_r_page - 1) * 8) + 5], 2);
                    memcpy(&serial_ptr[6], (const uint16_t *)&lSPeerData[tx_r_ch - 1][((tx_r_page - 1) * 8) + 6], 2);
                    memcpy(&serial_ptr[7], (const uint16_t *)&lSPeerData[tx_r_ch - 1][((tx_r_page - 1) * 8) + 7], 2);

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
                    memcpy(&rx_ptr[tmpCh], (const uint16_t *)&lPeerData[tmpCh][0], 2); //! 이 부분 생각해봐야할 듯. tmpCh이 lPeerData overflow날 확률 존재
                }
            }
        }
    }

    if (++peer_channel >= MAX_PEER)
        peer_channel = 0;
}

void recv_cb(const uint8_t *src_mac, const uint8_t *data, int len)
{
    uint8_t peer_channel = 0, packet_len = 0, cnt, data_len;
    word_big_endian_t crc16;
    uint16_t readData;

#if 0
    uint8_t peer_group;
    uint8_t peer_channel;
    uint8_t command;
    uint8_t device_type;
    uint8_t device_addr;
    uint8_t packet_len;
    peer_group      = data[0];
    peer_channel    = data[1];
    command         = data[2];
    device_type     = data[3];
    device_addr     = data[4];
    packet_len      = data[5];
#endif

    static uint8_t buff[WIFI_PACKET_MAX];
    uint8_t page = 0;
    uint8_t exist_cnt = 0, i;

    memcpy(buff, data, len);
    peer_channel = buff[WIFI_PACKET_CHANNEL];
    packet_len = (uint8_t)len;

    if (wifi_send.rf_set_group != buff[WIFI_PACKET_GROUP])
    {
#if DEBUG_SERIAL_WIFI == 1
        Serial.printf("[MSG]WIFI::CALLBAK::Miss match group::AP=%d, Peer=%d\r\n", wifi_send.rf_set_group, buff[WIFI_PACKET_GROUP]);
#endif
        return;
    }

    if (packet_len != buff[WIFI_PACKET_LENGTH])
    {
#if DEBUG_SERIAL_WIFI == 1
        Serial.printf("[MSG]WIFI::CALLBAK::Err packet Length::AP=%d, Peer=%d\r\n", packet_len, buff[WIFI_PACKET_LENGTH]);
#endif
        return;
    }

    if (!wifi_send.rx_busy[peer_channel])
    {
// 해당 채널에 대해서 wifi handle이 처리 중
// if( peer_channel == wifi_state_machine[peer_channel].peer.channel )
#if DEBUG_SERIAL_WIFI == 1
        Serial.printf("[MSG]WIFI::CALLBAK::Channel[%d] is not rxBusy\r\n", peer_channel);
#endif
        return;
    }
    wifi_send.doing_recv_cb = true;

    if (peer_channel > 15)
    {
        wifi_send.doing_recv_cb = false;
#if DEBUG_SERIAL_WIFI == 1
        Serial.printf("[MSG]WIFI::CALLBAK::Over peer channel\r\n");
#endif
        return;
    }

    if (peer_channel != wifi_state_machine[peer_channel].peer.channel)
    {
#if DEBUG_SERIAL_WIFI == 1
        Serial.printf("[MSG]WIFI::CALLBAK::Miss match channel::Response=%d vs Request=%d\r\n", peer_channel, wifi_state_machine[peer_channel].peer.channel);
#endif
        wifi_send.doing_recv_cb = false;
        return;
    }
    if (buff[WIFI_PACKET_TYPE] != wifi_state_machine[peer_channel].peer.device_type)
    {
#if DEBUG_SERIAL_WIFI == 1
        Serial.printf("[MSG]WIFI::CALLBAK::Miss match type::Response=%d vs Request=%d\r\n", buff[WIFI_PACKET_TYPE], wifi_state_machine[peer_channel].peer.device_type);
#endif
        wifi_send.doing_recv_cb = false;
        return;
    }
    if (buff[WIFI_PACKET_ADDRESS] != wifi_state_machine[peer_channel].peer.device_addr)
    {
#if DEBUG_SERIAL_WIFI == 1
        Serial.printf("[MSG]WIFI::CALLBAK::Miss match address::Response=%d vs Request=%d\r\n", buff[WIFI_PACKET_ADDRESS], wifi_state_machine[peer_channel].peer.device_addr);
#endif
        wifi_send.doing_recv_cb = false;
        return;
    }

    // CRC 체크
    crc16.flag.wd = crc16_modbus(CRC16_MODBUS_INIT_CODE, buff, packet_len - 2);
    if ((crc16.flag.bf.low != buff[packet_len - 2]) || (crc16.flag.bf.hi != buff[packet_len - 1]))
    {
#if DEBUG_SERIAL_WIFI == 1
        Serial.printf("[MSG]WIFI::CALLBAK::Miss match crc16::low=0x%02x hi=0x%02x vs packet[low]=0x%02x, packet[hi]=0x%02x\r\n", crc16.flag.bf.low, crc16.flag.bf.hi, buff[packet_len - 2], buff[packet_len - 1]);
#endif
        wifi_send.doing_recv_cb = false; //! 260317 추가 
        return;
    }

#if DEBUG_SERIAL_MONITOR == 1                                                           //! Modified... : CJL, 
    Serial.printf("[MSG]WIFI::CALLBAK::Group=%d, IO Channel=%d, Type=%d, Addr=%d, Len=%d\r\n", buff[WIFI_PACKET_GROUP], buff[WIFI_PACKET_CHANNEL], buff[WIFI_PACKET_TYPE], buff[WIFI_PACKET_ADDRESS], buff[WIFI_PACKET_LENGTH]);
#endif

    data_len = packet_len - 8;

    switch (buff[WIFI_PACKET_COMMAND])
    {
    case PEER_PAIRING_OK:
        if (peer[peer_channel].pairFlag)
        {
            switch (wifi_state_machine[peer_channel].peer.state)
            {
            case WIFI_STATE_PAIRED:
            case WIFI_STATE_SEND:
                wifi_state_machine[peer_channel].peer.response = true;
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

        peer[peer_channel].io_usage = buff[WIFI_PACKET_DATA];
        peer[peer_channel].io_page = buff[WIFI_PACKET_DATA + 1];
        peer[peer_channel].serial = buff[WIFI_PACKET_DATA + 2];

        /////////////////////////////////////////////////////////////////////
        // 프로토콜 수정 후 이거 안나오지 않을까 싶은데... 확인후 삭제
        // usage와 serial이 잘못 들어오는 경우가 있음.... 원인은 아직 못찾음
        // io usage가 1이 아니고, serial 데이타가 있음.....
        // 현재는 모드 1 WORD만 사용중
        if (peer[peer_channel].io_usage != 1)
        {
#if DEBUG_SERIAL_WIFI == 1
            Serial.printf("[MSG]WIFI::CALLBAK[%d]::PAIRING_OK::io_usage ERR\r\n", peer_channel);
#endif
            break;
        }
        /////////////////////////////////////////////////////////////////////

        wifi_state_machine[peer_channel].pairing.response = true;

        memcpy(peer[peer_channel].mac, src_mac, 6);                          // 응답 받은 peer mac 저장
        wifi_state_machine[peer_channel].peer.pMac = peer[peer_channel].mac; // 응답 받은 peer mac 저장

#if DEBUG_SERIAL_WIFI == 1
        Serial.printf("[MSG]WIFI::CALLBAK[%d]::PAIRING_OK:: usage=%d, page=%d, serial=%d\r\n", peer_channel, peer[peer_channel].io_usage, peer[peer_channel].io_page, peer[peer_channel].serial);
#endif
        wifi_state_machine[peer_channel].peer.device_type = buff[WIFI_PACKET_TYPE];

        /////////////////////////////////////////////////////////////////////
        // 연결 초기값 넣기 :: 기존에 정한 것

#if FUNC_REPAIR_SAVE_DATA
        if (wifi_state_machine[peer_channel].peer.repair_state == 2)
        {
            wifi_state_machine[peer_channel].peer.repair_state = 3;
        }
        else
        {
            if (peer[peer_channel].io_page > 0)
            {
                for (i = 1; i < peer[peer_channel].io_page + 1; i++)
                {
                    lPeerData[peer_channel][i] = i;
                    lPeerData[peer_channel][i] <<= 12;
                    lPeerData[peer_channel][i] += 2047; // 0x7FF( 0111 1111 111 ) -
                }
            }
            else
            {
                lPeerData[peer_channel][0] = 2047;
            }
        }
#else
        if( peer[peer_channel].io_page > 0 )
        {
            //! 20250919 : 자동 리페어링 때문에 생긴 사이드 이팩트 제거 : 아래 if문 추가
            if( !bitRead(rx_pairing_status, peer_channel) )   //페어드 상태에서 재페어링이라면 데이타 초기화 하지 않음
            {
                for( i = 1; i < peer[peer_channel].io_page+1; i++ )
                {
                    lPeerData[peer_channel][i] = i;
                    lPeerData[peer_channel][i] <<= 12;
                    lPeerData[peer_channel][i] += 2047;  // 0x7FF( 0111 1111 1111 ) 
                }
            }
        }
        else
        {
            lPeerData[peer_channel][0] = 2047;
        }
#endif

        if (peer[peer_channel].serial > 0)
        {
            // 시리얼 영역은 1이 12Ch임, 0 안씀
            for (i = 1; i < 11; i++)
            {
                lSPeerData[peer_channel - 11][i] = 2047; // 0x7FF( 0111 1111 111 )
            }
        }
        /////////////////////////////////////////////////////////////////////

        if (pairing_register(peer_channel, 0))
            wifi_state_machine[peer_channel].pairing.state = true;
        break;

    case PEER_PAIRING_CANCEL:
#if DEBUG_SERIAL_WIFI == 1
        Serial.printf("[MSG]WIFI::CALLBAK[%d]::CANCEL PAIRING OK\n", peer_channel);
#endif
        wifi_state_machine[peer_channel].pairing.response = true;
        wifi_state_machine[peer_channel].pairing.state = true;
        del_peer(peer_channel);
        break;

    case PEER_IO_GET:

        if (peer[peer_channel].pairFlag == true)
        {
#if FUNC_PAIRED_VERIFY == 1
            bitWrite(rx_pairing_status, peer_channel, 1); // 페어드 이후 데이타 요청이 성공 했을때 SET
#endif
            wifi_state_machine[peer_channel].peer.response = true;
            if (peer[peer_channel].io_page == 0)
            {
                memcpy(&lPeerData[peer_channel][0], &buff[WIFI_PACKET_DATA], data_len); 

#if DEBUG_SERIAL_MONITOR == 1
                if (buff[WIFI_PACKET_DATA] == 0 && buff[WIFI_PACKET_DATA + 1] == 0)
                {

                    if (peer_debug[peer_channel].zero < 0xffffffff)
                        peer_debug[peer_channel].zero++;
                    else
                        peer_debug[peer_channel].zero = 0xffffffff;
                    Serial.printf("[MSG]WIFI::CALLBAK[%d]::DATA RESPONSE[PAGE1]=0x%d, CNT=%d\n", peer_channel, lPeerData[peer_channel][1], peer_debug[peer_channel].zero);
                }
#endif

#if DEBUG_SERIAL_WIFI == 1
                Serial.printf("[MSG]WIFI::CALLBAK[%d]::DATA RESPONSE[PAGE0]=0x%04x\n", peer_channel, lPeerData[peer_channel][0]);
#endif
            }
            else
            {
                memcpy(&lPeerData[peer_channel][1], &buff[WIFI_PACKET_DATA], data_len);

#if DEBUG_SERIAL_MONITOR == 1
                if (buff[WIFI_PACKET_DATA] == 0 && buff[WIFI_PACKET_DATA + 1] == 0)
                {

                    if (peer_debug[peer_channel].zero < 0xffffffff)
                        peer_debug[peer_channel].zero++;
                    else
                        peer_debug[peer_channel].zero = 0xffffffff;
                    Serial.printf("[MSG]WIFI::CALLBAK[%d]::DATA RESPONSE[PAGE1]=0x%d, CNT=%d\n", peer_channel, lPeerData[peer_channel][1], peer_debug[peer_channel].zero);
                }
#endif

#if 0                   
                    Serial.printf("[MSG]WIFI::CALLBAK[%d]::DATA RESPONSE",peer_channel);
                    for( cnt=0 ; cnt<data_len ; cnt++)
                    {
                        Serial.printf("[%d]:0x%04x ",cnt, lPeerData[peer_channel][cnt+1]);
                    }
                    Serial.printf("\r\n");
#endif
            }
#if FUNC_REPAIR_SAVE_DATA
            wifi_state_machine[peer_channel].peer.repair_counter = 0;
#endif
        }
        break;

    case PEER_IO_SET:

        if (peer[peer_channel].pairFlag == true)
        {
#if FUNC_PAIRED_VERIFY == 1
            bitWrite(rx_pairing_status, peer_channel, 1); // 페어드 이후 데이타 요청이 성공 했을때 SET
#endif
            wifi_state_machine[peer_channel].peer.response = true;
        }
        else
        {
            break;
        }

        // set data 일때 응답 데이타가 동일한지 비교할 필요 없음
        if (peer[peer_channel].io_page == 0)
        {
            readData = (uint16_t)buff[WIFI_PACKET_DATA + 1];
            readData <<= 8;
            readData += buff[WIFI_PACKET_DATA];
#if 0

                if( wifi_state_machine[peer_channel].peer.set_io_data == readData )
                {
                    lPeerData[peer_channel][0] = readData;
                    Serial.printf("[MSG]WIFI::CALLBAK[%d]::DATA SET:: Page=0, readData = 0x%04x\r\n",peer_channel, readData);
                }
#else
            lPeerData[peer_channel][0] = readData;
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
                    memcpy(&lPeerData[peer_channel][1], (const uint8_t *)&data[4], len-4);                
                }
#else
            memcpy(&lPeerData[peer_channel][1], &buff[WIFI_PACKET_DATA], data_len);

#if 0
                Serial.printf("[MSG]WIFI::CALLBAK[%d]::DATA SET:: Page=0x%02x ", peer_channel, page);
                for( cnt=0 ; cnt<data_len ; cnt++)
                {
                    Serial.printf("[%d]:0x%04x ",cnt, lPeerData[peer_channel][cnt+1]);
                }
                Serial.printf("\r\n");
#endif

#endif

#if FUNC_REPAIR_SAVE_DATA
            wifi_state_machine[peer_channel].peer.repair_counter = 0;
#endif
        }
        break;

    case PEER_SERIAL_SET: //
        // 0 Page 없으므로 편의상 2차원 배열의 시작은 [][1]
        // 12 Ch는 1Ch로 처리함으로 -11 함

        if (peer_channel > 11)
        {
#if DEBUG_SERIAL_WIFI == 1
            Serial.printf("[MSG]WIFI::CALLBAK[%d]::PEER_ALL_SDATA_RESPONSE\r\n", peer_channel);
#endif
            if (peer[peer_channel].pairFlag == true)
            {
#if FUNC_PAIRED_VERIFY == 1
                bitWrite(rx_pairing_status, peer_channel, 1); // 페어드 이후 데이타 요청이 성공 했을때 SET
#endif
                // IO
                memcpy(&lPeerData[peer_channel][0], &buff[WIFI_PACKET_DATA], 2);
                // SERIAL
                wifi_state_machine[peer_channel].peer.response = true;
                wifi_state_machine[peer_channel].peer.response_type_serial = true;
                memcpy(&lSPeerData[peer_channel - 12][0], &buff[WIFI_PACKET_DATA + 2], data_len); // 12->0, 13->1, 14->2, 15->3
#if FUNC_REPAIR_SAVE_DATA
                wifi_state_machine[peer_channel].peer.repair_counter = 0;
#endif
            }
        }
        break;

    case PEER_SERIAL_GET: // SET과 같은데 일단 나누어 놓음.....
        if (peer_channel > 11)
        {
#if DEBUG_SERIAL_WIFI == 1
            Serial.printf("[MSG]WIFI::CALLBAK[%d]::PEER_ALL_SDATA_RESPONSE\r\n", peer_channel);
#endif
            if (peer[peer_channel].pairFlag == true)
            {
#if FUNC_PAIRED_VERIFY == 1
                bitWrite(rx_pairing_status, peer_channel, 1); // 페어드 이후 데이타 요청이 성공 했을때 SET
#endif

                // IO
                memcpy(&lPeerData[peer_channel][0], &buff[WIFI_PACKET_DATA], 2);
                // SERIAL
                wifi_state_machine[peer_channel].peer.response = true;
                wifi_state_machine[peer_channel].peer.response_type_serial = true;
                memcpy(&lSPeerData[peer_channel - 12][0], &buff[WIFI_PACKET_DATA + 2], data_len); // 12 = 0
#if FUNC_REPAIR_SAVE_DATA
                wifi_state_machine[peer_channel].peer.repair_counter = 0;
#endif
            }
        }
        break;

    default:
        if (peer[peer_channel].pairFlag == true)
        {
            wifi_state_machine[peer_channel].peer.response = true;
#if DEBUG_SERIAL_WIFI == 1
            Serial.printf("[MSG]WIFI::CALLBAK::Paired::Command Err=0x%02x\r\n", buff[WIFI_PACKET_COMMAND]);
#endif
        }
        break;
    }
    wifi_send.doing_recv_cb = false;

} // end recv_cb

void send_cb(const uint8_t *des_addr, esp_now_send_status_t status)
{
    // int8_t send_idx = 0;

    for (int i = 0; i <= MAX_PEER; i++)
    {
        if (i == MAX_PEER)
        {
            // send_idx = i;
            break;
        }
        else if (memcmp(peer[i].mac, des_addr, 6) == 0)
        {
            xSemaphoreGive(send_sem); // send list에 있을 경우 semaphore를 릴리즈
            break;
        }
    }
    // Serial.printf("send_cb::des_addr=0x%x, status=%d\r\n", *des_addr,status );
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
    if (esp_now_register_send_cb(send_cb) != ESP_OK)
        return false;

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

bool Wifi_enQueue(uint8_t item)
{
    bool rtn = false;
    if (wifi_send.queue.head > wifi_send.queue.tail)
    {
        if ((wifi_send.queue.head - wifi_send.queue.tail) < (WIFI_SEND_QUEUE_MAX - 1))
            rtn = true;
    }
    else if (wifi_send.queue.head < wifi_send.queue.tail)
    {
        if ((wifi_send.queue.tail - wifi_send.queue.head) < (WIFI_SEND_QUEUE_MAX - 1))
            rtn = true;
    }
    else
        rtn = true;

    if (rtn)
    {
        wifi_send.queue.item[wifi_send.queue.head] = item;

        if (++wifi_send.queue.head >= WIFI_SEND_QUEUE_MAX)
            wifi_send.queue.head = 0;
    }

    return rtn;
}
void Wifi_deQueue(void)
{
    if (++wifi_send.queue.tail >= WIFI_SEND_QUEUE_MAX)
        wifi_send.queue.tail = 0;
}
bool Wifi_getQueue(uint8_t *item)
{
    if (wifi_send.queue.head != wifi_send.queue.tail)
    {
        *item = wifi_send.queue.item[wifi_send.queue.tail];

        if (++wifi_send.queue.tail >= WIFI_SEND_QUEUE_MAX)
            wifi_send.queue.tail = 0;
        return true;
    }
    else
        return false;
}

void Wifi_Peer_Data_Set(uint8_t channel, uint8_t cmd, uint16_t *tx_data, uint8_t wlen)
{
    word_big_endian_t crc16;
    uint16_t len;
    memset(wifi_state_machine[channel].peer.txBuf, 0, sizeof(wifi_state_machine[channel].peer.txBuf));

    len = wlen * 2 + 6;                               // data 까지의 길이
    wifi_state_machine[channel].peer.txLen = len + 2; // 전체 길이

    wifi_state_machine[channel].peer.txBuf[0] = wifi_send.rf_set_group;
    wifi_state_machine[channel].peer.txBuf[1] = channel;
    wifi_state_machine[channel].peer.txBuf[2] = cmd;
    wifi_state_machine[channel].peer.txBuf[3] = wifi_state_machine[channel].peer.device_type;
    wifi_state_machine[channel].peer.txBuf[4] = wifi_state_machine[channel].peer.device_addr;
    wifi_state_machine[channel].peer.txBuf[5] = wifi_state_machine[channel].peer.txLen;

    switch (cmd)
    {
    // 데이타 0인 케이스
    case AP_IO_GET:
        memcpy(&wifi_state_machine[channel].peer.txBuf[6], tx_data, wlen * 2);
        break;

    // 데이타 있을 경우
    default:
        memcpy(&wifi_state_machine[channel].peer.txBuf[6], tx_data, wlen * 2);
        break;
    }

    crc16.flag.wd = crc16_modbus(CRC16_MODBUS_INIT_CODE, wifi_state_machine[channel].peer.txBuf, len);
    wifi_state_machine[channel].peer.txBuf[len] = crc16.flag.bf.low;
    wifi_state_machine[channel].peer.txBuf[len + 1] = crc16.flag.bf.hi;

    wifi_send.tx_busy[channel] = false;
    wifi_send.rx_busy[channel] = false;
}

void Wifi_Peer_State_Set(WIFI_STATE_MACHINE state, uint8_t channel, uint8_t cmd, uint16_t typeAddr, uint16_t wLen)
{
    word_big_endian_t crc16;
    uint16_t len;

    len = 6; // data 까지의 길이

    wifi_state_machine[channel].peer.txBuf[0] = wifi_send.rf_set_group;
    wifi_state_machine[channel].peer.txBuf[1] = channel;
    wifi_state_machine[channel].peer.txBuf[2] = cmd;

    wifi_state_machine[channel].peer.channel = channel;

    if (typeAddr)
    {
        wifi_state_machine[channel].peer.device_type = (uint8_t)(typeAddr >> 8);
        wifi_state_machine[channel].peer.device_addr = (uint8_t)(typeAddr & 0x00ff);
    }
    else
    {
        // 이미 등록되어 있음.
    }
    wifi_state_machine[channel].peer.txBuf[3] = wifi_state_machine[channel].peer.device_type;
    wifi_state_machine[channel].peer.txBuf[4] = wifi_state_machine[channel].peer.device_addr;
    wifi_state_machine[channel].peer.txBuf[5] = 8;

    crc16.flag.wd = crc16_modbus(CRC16_MODBUS_INIT_CODE, wifi_state_machine[channel].peer.txBuf, 6);
    wifi_state_machine[channel].peer.txBuf[len] = crc16.flag.bf.low;
    wifi_state_machine[channel].peer.txBuf[len + 1] = crc16.flag.bf.hi;

    wifi_state_machine[channel].peer.txLen = 8;

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
        wifi_state_machine[channel].peer.pMac = peer[channel].mac;
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

        if( peer[wifi_send.peer_addr].pairFlag || wifi_state_machine[wifi_send.peer_addr].pairing.request )
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
    static uint16_t cnt_loop = 0, setTime = SET_TIME_PAIRING;
    static uint8_t send_cmd = 0;
    uint8_t channel = wifi_send.peer_addr;
    uint8_t getAddr = 0;
    bool save_response = false;
    esp_err_t err;

    if (wifi_send.doing_recv_cb)
        return;
    wifi_send.doing_handle = true;

    switch (wifi_state_machine[channel].peer.state)
    {
    case WIFI_STATE_READY:
        // 페어링 요청 확인하고 queue에 넣고 상태 변경
        wifi_state_machine[channel].pairing.timeout = 0;
        if (wifi_state_machine[channel].pairing.request == true)
        {
            if (Wifi_enQueue(channel))
            {
                if (wifi_state_machine[channel].pairing.add)
                {
                    wifi_state_machine[channel].peer.state = WIFI_STATE_PAIRING_ADD;
                    cnt_loop = 0;
                    setTime = SET_TIME_PAIRING;

#if DEBUG_SERIAL_WIFI == 1
                    Serial.printf("[MSG]WIFI::HANDLE[%d]::PAIRING ADD\r\n", channel);
#endif

#if FUNC_REPAIR_SAVE_DATA
                    if (wifi_state_machine[channel].peer.repair_state == 1)
                    {
                        wifi_state_machine[channel].peer.repair_state = 2;
                    }
                    else if (wifi_state_machine[channel].peer.repair_state == 2)
                    {
                        if (++wifi_state_machine[channel].peer.repair_counter > 3)
                        {
                            wifi_state_machine[channel].peer.repair_counter = 0;
                            wifi_state_machine[channel].peer.repair_state = 0;
                        }
                    }
#endif
                }
            }
        }
        break;

    case WIFI_STATE_PAIRING_ADD:

        if (peer_bak[channel].repair_itself == true)
        {
            //  Wifi_Peer_State_Set(WIFI_STATE_PAIRING_ADD, peer_channel, AP_PAIRING_REQ, peer[peer_channel].typeAddr, 0);
        }

        break;

    case WIFI_STATE_PAIRED:
        // 페어링 요청 확인하고 queue에 넣고 상태 변경
        wifi_state_machine[channel].pairing.timeout = 0;
        if (wifi_state_machine[channel].pairing.request == true)
        {
            setTime = SET_TIME_PAIRING;
            if (Wifi_enQueue(channel))
            {
                if (wifi_state_machine[channel].pairing.del)
                {
                    wifi_state_machine[channel].peer.response = false; // 이전 call back 응답이 있을 수 있음.
                    wifi_state_machine[channel].peer.state = WIFI_STATE_PAIRING_DEL;
                    // pairing case는 delay를 더 100ms로 조정
                    cnt_loop = 0;

#if DEBUG_SERIAL_WIFI == 1
                    Serial.printf("[MSG]WIFI::HANDLE[%d]::STATE::PAIRED::DEL\r\n", channel);
#endif

// 이더캣 요청에 의해서 unpairing시
#if FUNC_REPAIR_SAVE_DATA
                    wifi_state_machine[channel].peer.repair_counter = 0;
                    wifi_state_machine[channel].peer.repair_state = 0;
#endif
                }
                else if (peer[channel].pairFlag == true) // 이미 페어링 되어 있음
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

// 이더캣 요청에 의해서 unpairing시
#if FUNC_REPAIR_SAVE_DATA
                    wifi_state_machine[channel].peer.repair_counter = 0;
                    wifi_state_machine[channel].peer.repair_state = 0;
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
                save_response = true;
                break;
            }

            if (wifi_state_machine[channel].peer.update_serial)
            {
                if (Wifi_enQueue(channel))
                {
                    wifi_state_machine[channel].peer.update_serial = false;
                    Wifi_Peer_Data_Set(channel, AP_SERIAL_SET, wifi_state_machine[channel].peer.set_serial_Buf, 9);
                    // 아래 초기화는 어차피 update_send 할때 설정되기 때문에 없어도 괜찮음
                    cnt_loop = 0;
                    setTime = SET_TIME_PAIRD;
#if DEBUG_SERIAL_WIFI == 1
                    Serial.printf("[MSG]WIFI::HANDLE[%d]::STATE::PAIRED::REQUEST::UPDATE::SERIAL\r\n", channel);
#endif
                }
            }
            else if (wifi_state_machine[channel].peer.request_serial)
            {
                if (Wifi_enQueue(channel))
                {
                    wifi_state_machine[channel].peer.request_serial = false;
                    Wifi_Peer_Data_Set(channel, AP_SERIAL_GET, wifi_state_machine[channel].peer.set_serial_Buf, 9);
                    // 아래 초기화는 어차피 update_send 할때 설정되기 때문에 없어도 괜찮음
                    cnt_loop = 0;
                    setTime = SET_TIME_PAIRD;
#if DEBUG_SERIAL_WIFI == 1
                    Serial.printf("[MSG]WIFI::HANDLE[%d]::STATE::PAIRED::REQUEST::DATA::SERIAL\r\n", channel);
#endif
                }
            }
            else if (wifi_state_machine[channel].peer.update_io)
            {
                if (Wifi_enQueue(channel))
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
                if (Wifi_enQueue(channel))
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
        if (wifi_state_machine[channel].pairing.add == true)
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

    if (Wifi_getQueue(&getAddr) && !wifi_send.tx_busy[channel] && !wifi_send.rx_busy[channel])
    {
        channel = getAddr;
        cnt_loop = 0;
        wifi_send.tx_busy[channel] = true;
        err = esp_now_send(wifi_state_machine[channel].peer.pMac, (const uint8_t *)wifi_state_machine[channel].peer.txBuf, wifi_state_machine[channel].peer.txLen);
        switch (err)
        {
        case ESP_OK:
            wifi_send.tx_busy[channel] = false;
            wifi_send.rx_busy[channel] = true;
            wifi_send.peer_req = true;
#if DEBUG_SERIAL_MONITOR == 1
            Serial.printf("[MSG]WIFI::HANDLE[%d]::REQUEST SEND::Channel=0x%02x, Command=0x%02x, Type=0x%02x, Addr=0x%02x\r\n", channel, wifi_state_machine[channel].peer.txBuf[1], wifi_state_machine[channel].peer.txBuf[2], wifi_state_machine[channel].peer.txBuf[3], wifi_state_machine[channel].peer.txBuf[4]);
#endif
            break;

        case ESP_ERR_ESPNOW_NOT_FOUND:
            break;

        default:
#if DEBUG_SERIAL_WIFI == 1
            Serial.printf("[MSG]WIFI::HANDLE[%d]::SEND ERR=%d\r\n", channel, err);
#endif
            break;
        }
    }

    if (wifi_send.tx_busy[channel])
    {
    }
    else if (wifi_send.rx_busy[channel])
    {
        // check of peer response
        if (wifi_state_machine[channel].pairing.response)
        {
            wifi_send.peer_req = false;
            // Serial.printf("[MSG]WIFI::HANDLE[%d]::RESPONSE::PAIRING\r\n",channel);

            wifi_state_machine[channel].pairing.response = false;
            wifi_state_machine[channel].pairing.request = false;
            wifi_send.rx_busy[channel] = false;

            if (wifi_state_machine[channel].pairing.state == true) // 페어링 정상 응답 완료
            {
#if DEBUG_SERIAL_WIFI == 1
                Serial.printf("[MSG]WIFI::HANDLE[%d]::RESPONSE::PAIRING OK\r\n", channel);
#endif

                wifi_state_machine[channel].pairing.state = false;
                // reset pairing flag

                if (wifi_state_machine[channel].pairing.add)
                {
                    wifi_state_machine[channel].pairing.add = false;
                    // set peer state
                    wifi_state_machine[channel].peer.state = WIFI_STATE_PAIRED;

                    wifi_state_machine[channel].pairing.retry = 0; //! 20250804. csot 현장에서 추가
#if DEBUG_SERIAL_PEER == 1
                    if (peer_debug[channel].paired)
                    {
                        if (peer_debug[channel].retry_pair_cnt < 0xffff)
                            peer_debug[channel].retry_pair_cnt++;
                    }
                    else
                    {
                        peer_debug[channel].paired = true;
                    }
#endif
                }
                else if (wifi_state_machine[channel].pairing.del)
                {
                    wifi_state_machine[channel].pairing.del = false;
                    wifi_state_machine[channel].pairing.retry = 0; // 20250804. cost 현장에서 추가
                    memset(&wifi_state_machine[channel], 0, sizeof(wifi_state_machine[channel]));
                }
            }
            else // 비정상
            {
                if (wifi_state_machine[channel].pairing.add && !peer[channel].pairFlag)
                {
#if DEBUG_SERIAL_WIFI == 1
                    Serial.printf("[MSG]WIFI::HANDLE[%d]::RESPONSE::Add-NonPaired\r\n", channel);
#endif
                    // 페어링 추가 요청이었는데, 페어링이 안돼었다면... 재시도 3번

                    if (++wifi_state_machine[channel].pairing.retry > 3)
                    {
                        wifi_state_machine[channel].pairing.retry = 0;
                        memset(&wifi_state_machine[channel], 0, sizeof(wifi_state_machine[channel]));
                    }
                    else
                    {
                        Wifi_Peer_State_Set(WIFI_STATE_PAIRING_ADD, channel, AP_PAIRING_REQ, peer[channel].typeAddr, 0);
                        wifi_state_machine[channel].peer.state = WIFI_STATE_READY;
#if DEBUG_SERIAL_WIFI == 1
                        Serial.printf("[MSG]WIFI::HANDLE[%d]::RESPONSE::NG - Retry\r\n", channel);
#endif
                    }
                }
                else if (wifi_state_machine[channel].pairing.del && peer[channel].pairFlag)
                {
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
                    peer[channel].pairFlag = false;
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
                save_response = true;
#if FUNC_REPAIR_SAVE_DATA
                wifi_state_machine[channel].peer.repair_timeout = 0;
                wifi_state_machine[channel].peer.repair_counter = 0;
                wifi_state_machine[channel].peer.repair_state = 0;
#endif
            }
        }
    }

    if (save_response)
    {
        wifi_state_machine[channel].peer.response = false;
        wifi_state_machine[channel].peer.cnt_disconnect = 0;
        wifi_send.rx_busy[channel] = false;
        wifi_send.peer_req = false;

        if (wifi_state_machine[channel].peer.response_type_serial)
        {
            wifi_state_machine[channel].peer.response_type_serial = false;

#if DEBUG_SERIAL_WIFI == 1
            Serial.printf("[MSG]WIFI::HANDLE[%d]::RESPONSE::Seiral::D[0]=%d\r\n", channel, lPeerData[channel][0]);
#endif
        }

        if (peer[channel].io_page)
        {
#if DEBUG_SERIAL_WIFI == 1
            Serial.printf("[MSG]WIFI::HANDLE[%d]::RESPONSE::IO-PAGE::D[1]=%d, D[2]=%d, D[3]=%d, D[4]=%d\r\n", channel, lPeerData[channel][1], lPeerData[channel][2], lPeerData[channel][3], lPeerData[channel][4]);
#endif
        }
        else
        {
#if DEBUG_SERIAL_WIFI == 1
            Serial.printf("[MSG]WIFI::HANDLE[%d]::RESPONSE::IO::D[0]=%d\r\n", channel, lPeerData[channel][0]);
#endif
        }

        if (peer[channel].pairFlag == false)
        {
            memset(&wifi_state_machine[channel], 0, sizeof(wifi_state_machine[channel]));
        }

#if FUNC_REPAIRD_AUTO == 1
        peer_bak[channel].receiveLoss_cnt = 0;
#endif
    }

    if (++cnt_loop > setTime) // 60ms, 60 * 16 = 960ms : this is 16 device scan loop time
    {
        cnt_loop = 0;
        wifi_send.tx_busy[channel] = false;

        // Send timeout
        if (wifi_send.peer_req) // 다음 순서까지 peer_req인데 응답이 없어서 flag가 살아 있다면
        {
            wifi_send.peer_req = false;
            if (++wifi_state_machine[channel].peer.cnt_disconnect >= SETUP_DISCONNECT_MAX) // Disconnect Retry Time!!!!!!!!!!!!
            {
                // 강제 del
                wifi_send.rx_busy[channel] = false;
                wifi_state_machine[channel].peer.cnt_disconnect = 0;

                if (peer[channel].pairFlag)
                {
                    del_peer(channel);
                }

#if FUNC_REPAIRD_AUTO == 1
                if (peer_bak[channel].repair_itself == false)
                {
                    memset(&wifi_state_machine[channel], 0, sizeof(wifi_state_machine[channel]));
                }
#else
                memset(&wifi_state_machine[channel], 0, sizeof(wifi_state_machine[channel]));
#endif

#if DEBUG_SERIAL_MONITOR == 1
                Serial.printf("[MSG]WIFI::HANDLE[%d]::TIME OUT::Del Peer!!!!\r\n", channel);
#endif

                if (peer_debug[channel].del_cnt < 0xffffffff)
                {
                    peer_debug[channel].del_cnt++;
                }
#if FUNC_REPAIR_SAVE_DATA
                wifi_state_machine[channel].peer.repair_state = 1;
#endif
            }
            else
            {
                // 페어드는 페어드 상태로
                if (peer[channel].pairFlag == true)
                {
#if DEBUG_SERIAL_MONITOR == 1
                    Serial.printf("[MSG]WIFI::HANDLE[%d]::TIME OUT::Disconnet Counter=%d\r\n", channel, wifi_state_machine[channel].peer.cnt_disconnect);
#endif
                    peer_bak[channel].receiveLoss_cnt++;
                    wifi_state_machine[channel].peer.state = WIFI_STATE_PAIRED;
                }
                else
                {
                    // 페어링 요청중이면 초기 상태로
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
            if (peer[channel].pairFlag == true) // 요청 상태 없이 타임아웃되었을 경우
            {
                if (wifi_state_machine[channel].pairing.del)
                {
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
                else if (peer_bak[channel].repair_itself == true)
                {
#if FUNC_REPAIRD_AUTO == 1
                    if (++peer_bak[channel].repair_cnt < SETUP_REPAIR_MAX) // 자체 리페어링 시도 횟수
                    {
                        Wifi_Peer_State_Set(WIFI_STATE_PAIRING_ADD, channel, AP_PAIRING_REQ, peer[channel].typeAddr, 0);

                        peer_bak[channel].receiveLoss_cnt++;
#if DEBUG_SERIAL_MONITOR == 1
                        Serial.printf("[MSG]WIFI::HANDLE[%d]::TIME OUT::It self try repair, receiveLoss Cnt=%d\r\n", channel, peer_bak[channel].receiveLoss_cnt);
#endif
                    }
                    else // 연속 리페어링 실패
                    {
                        peer_bak[channel].receiveLoss_cnt = SETUP_RECEVIELOSS_MAX + 1;

#if DEBUG_SERIAL_MONITOR == 1
                        Serial.printf("[MSG]WIFI::HANDLE[%d]::TIME OUT::REPAIRING\r\n", channel);
#endif
                    }

                    if (peer_bak[channel].receiveLoss_cnt > SETUP_RECEVIELOSS_MAX) // 데이타를 하나도 받지 못함
                    {
#if DEBUG_SERIAL_MONITOR == 1
                        Serial.printf("[MSG]WIFI::HANDLE[%d]::End Auto Repairing\r\n", channel);
#endif
                        // End auto repairing
                        memset(&peer_bak[channel], 0, sizeof(peer_bak[channel]));
                        memset(&peer[channel], 0, sizeof(peer[channel]));
                        bitWrite(rx_pairing_status, channel, 0);
                    }

#else

#endif
                }
            }
        }

#if FUNC_REPAIR_SAVE_DATA
        if (wifi_state_machine[channel].peer.repair_state != 0)
        {
            if (++wifi_state_machine[channel].peer.repair_timeout > 60)
            {
                wifi_state_machine[channel].peer.repair_state = 0;
                wifi_state_machine[channel].peer.repair_timeout = 0;
                wifi_state_machine[channel].peer.repair_counter = 0;
            }
        }
#endif

#if DEBUG_SERIAL_MONITOR == 1
// Serial.printf("[MSG]WIFI::HANDLE[%d]::DEBUG del cnt=%d, zero=%d retry=%d\r\n",channel, peer_debug[channel].del_cnt, peer_debug[channel].zero, peer_debug[channel].retry_pair_cnt );
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
    // SysTick_Set(true);
    gSysTick.f_ms1 = 1;

    return true;
    ///////////////////////////////////////////////////////////
}

void Tick_Handle(void)
{
    gSysTick.flag.wf = false;

    if (++gSysTick.cnt_ms2 > 2)
    {
        gSysTick.flag.bf.ms2 = true;
        gSysTick.cnt_ms2 = 0;
    }

    if (++gSysTick.cnt_ms5 > 5)
    {
        gSysTick.flag.bf.ms5 = true;
        gSysTick.cnt_ms5 = 0;
    }
    if (++gSysTick.cnt_ms10 > 10)
    {
        gSysTick.flag.bf.ms10 = true;
        gSysTick.cnt_ms10 = 0;
    }

    if (++gSysTick.cnt_ms25 > 20)
    {
        gSysTick.flag.bf.ms25 = true;       
        gSysTick.cnt_ms25 = 0;
    }

    // if (++gSysTick.cnt_ms25 > 25)
    // {
    //     gSysTick.flag.bf.ms25 = true;
    //     gSysTick.cnt_ms25 = 0;
    // }

    if (++gSysTick.cnt_ms50 > 50)
    {
        gSysTick.flag.bf.ms50 = true;
        gSysTick.cnt_ms50 = 0;
    }

    if (++gSysTick.cnt_ms100 > 100)
    {
        gSysTick.flag.bf.ms100 = true;
        gSysTick.cnt_ms100 = 0;
    }

    if (++gSysTick.cnt_ms500 > 500)
    {
        gSysTick.flag.bf.ms500 = true;
        gSysTick.cnt_ms500 = 0;
    }

    if (++gSysTick.cnt_sec1 > 1000)
    {
        gSysTick.flag.bf.sec1 = true;
        gSysTick.cnt_sec1 = 0;
    }
}

void setup()
{
    // Serial.begin(115200);
    Serial.begin(230400);
    while (!Serial);
    set_board_Version();
    Set_GPIO();
    
    Read_Rotary();
    peer_led_setup(); //! set gpio안에있는 peer led 선언을 led setup으로 뺴기 
    
    Serial.println("");

    send_sem = xSemaphoreCreateBinary();
    recv_sem = xSemaphoreCreateBinary();
    tx_sem = xSemaphoreCreateBinary();
    rx_sem = xSemaphoreCreateBinary();


    if (EASYCAT.Init())
    {
        Serial.printf("[MSG]SYSTEM INFO::WIFI::ECAT OK\r\n");
    }
    else
    {
        Serial.printf("[MSG]SYSTEM INFO::WIFI::ECAT NG\r\n");
    }

    if (wl_init())
    {
        Serial.printf("[MSG]SYSTEM INFO::WIFI::INIT OK\r\n");
    }
    else
    {
        Serial.printf("[MSG]SYSTEM INFO::WIFI::INIT NG\r\n");
    }

    uint32_t ll32temp;

    EASYCAT.SPIWriteRegisterIndirect(iAddr, ALIAS_REG_H, 2);
    delay(100);

    ll32temp = EASYCAT.SPIReadRegisterIndirect(ALIAS_REG_H, 2);
    Serial.printf("[MSG]SYSTEM INFO::ECAT::0x0012 : 0x%x(%d)\n", ll32temp, ll32temp);

    Initialize_PDO();
    delay(100);
    EASYCAT.MainTask();

    //delay(1000);
    xTaskCreatePinnedToCore(LedTask,"LED",8000,NULL,1,NULL,0); // Core0
    

    
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
    //static uint8_t red_toggle_cnt = 0; //! NEW

    if (gSysTick.f_ms1)
    {
        gSysTick.f_ms1 = 0;
        Tick_Handle();
        Wifi_Handle();

        if (gSysTick.flag.bf.ms10)      Ethercat_Handle();
        if (gSysTick.flag.bf.ms25)
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
        if (gSysTick.flag.bf.sec1)
        {
            //Log_RF_Summary_1s();
        }
        // if (gSysTick.flag.bf.ms500)
        // {

        //      //! mutex 사용 안하고, lock free로 일단 해봄 
        //     peer_led.EtherCAT_pairing_bit = EASYCAT.BufferIn.Cust.pairing_bit;//temp
        // }
        //TP_HIGH();  
        EASYCAT.MainTask();
        //TP_LOW();
    } 




} // end loop
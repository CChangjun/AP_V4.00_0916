#ifndef _PEER_CHK_LED_H
#define _PEER_CHK_LED_H

#include <Adafruit_NeoPixel.h>
//#include "Tick_Handler.h"


/*====================================================================
 *  Peer LED State Transition Diagram
 *====================================================================
/*
 * Per-PEER LED state machine
 *
 * States (per peer_num):
 *   LED_STATE_OFF       : 아직 한 번도 페어링된 적 없음 (ever_paired == false)
 *   LED_STATE_NORMAL    : 현재 pairing OK, 정상 통신 중 (Green / Yellow 토글)
 *   LED_STATE_PAIR_DISC : 이전에 pairing 이 있었으나 현재는 disconnect 상태 (Red 토글)
 *
 * Transitions:
 *
 *   +------------------+          paired == true
 *   | LED_STATE_OFF    | -----------------------------+
 *   | (ever_paired=0)  |                              |
 *   +------------------+                              v
 *                                 +------------------------+
 *                                 | LED_STATE_NORMAL       |
 *                                 | (paired && ever_paired)|
 *                                 +------------------------+
 *                                   ^                    |
 *      (!paired && ever_paired)     |                    | (!paired)
 *                    +--------------+                    |
 *                    |                                   |
 *   +-------------------------------+                    |
 *   | LED_STATE_PAIR_DISC           | <------------------+
 *   | (ever_paired == true,         |
 *   |  현재는 disconnect 상태)       |
 *   +-------------------------------+
 *
 * 동작 요약:
 *  - 처음 페어링되면: OFF → NORMAL, ever_paired = true
 *  - 페어링이 끊기면: NORMAL → PAIR_DISC (Red 깜빡)
 *  - 다시 페어링되면: PAIR_DISC → NORMAL
 *  - 페어링된 적이 한 번도 없는 peer는 계속 OFF 유지
 */

#define ATOMIC  1
#define TEST    1
/***************************************************** */
#define FST                         0                  //
#define NUMPIXELS                   16                 // LED수, 
#define BRIGHTNESS                  15                 // 헤더,0-255  
#define GREEN_Boundary              -68                //
//#define YELLOW_Boundary             -41              //
                                                       //
#define LED_RED                     255, 0, 0          //
#define LED_GREEN                   0, 255, 0          //
#define LED_BLUE                    0, 0, 255          //
#define LED_YELLOW                  255,255, 0         //
#define LED_PURPLE                  128,0,128          //
#define LED_OFF                     0, 0, 0            //
#define MAX_PEER                    16                 //
#define LED                         2                  // Onboard LED Blue
#define PEER_CHK_LED                12                 //
                                                       //
#define Serial_                     1                  //
#define TEST_POINT                  14                 //
#define TP_HIGH() digitalWrite(TEST_POINT, HIGH)       //
#define TP_LOW()  digitalWrite(TEST_POINT, LOW)        //
#define RSSI_TEST                   0                  //
                                                       //
                                                       //
/***************************************************** */ 
//#define GET_RSSI_STATE(rssi) \
//    ((rssi) > GREEN_Boundary ? RSSI_STATE_GREEN : ((rssi) > YELLOW_Boundary ? RSSI_STATE_YELLOW : RSSI_STATE_RED))

#define GET_RSSI_STATE(rssi)\
    ((rssi) > GREEN_Boundary ? RSSI_STATE_GREEN : RSSI_STATE_YELLOW)
//todo 한덕매니저님이 RED : paired->disconnected, 통신 상태 : GREEN or YELLOW로 변경하라고 하심 


//LUT는 안쓸듯 
#if 0
const uint32_t rssi_color_LUT[13] = 
{
    strip.Color(255, 0, 0),   // -90
    strip.Color(255, 64, 0),  // -85
    strip.Color(255, 128, 0), // -80
    strip.Color(255, 192, 0), // -75
    strip.Color(255, 255, 0), // -70
    strip.Color(192, 255, 0), // -65
    strip.Color(128, 255, 0), // -60
    strip.Color(64, 255, 0),  // -55
    strip.Color(0, 255, 0),   // -50
    strip.Color(0, 255, 64),  // -45
    strip.Color(0, 255, 128), // -40
    strip.Color(0, 255, 192), // -35
    strip.Color(0, 255, 255)  // -30
};

uint32_t get_rssi_color(int rssi) 
{
    int idx = (rssi + 90) / 5; // -90~-30 → 0~12 LUT mapping 하면 되지않을까 
    //signed char idx = (rssi + 90)/5; int보단 sigend가 낫지 않을까
    if (idx < 0)    idx = 0;
    if (idx > 12)   idx = 12;
    return rssi_color_LUT[idx];
}
#endif
void update_pairing_led(bool paired);
void show_led_init(void);
void peer_led_setup(void);
void set_peer_LED(int rssi, uint8_t peer_num);
void set_peer_LED_state(uint8_t peer_num,
                        bool paired_flag,
                        bool peerLED_tog_flag,
                        bool any_paired);
void LED_Task(void);
void LedTask(void *arg);
//void rssi_display();

// void set_peer_LED_state(uint8_t peer_num, bool paired_flag, bool peerLED_tog_flag, int rssi);




#if 0
void (*P_show_led_init)(void);
void (*P_peer_led_setup)(void);
void (*P_set_peer_LED)(int,uint8_t);
void (*P_set_peer_LED_state)(uint8_t,bool);
void (*P_LED_Task)(void);
#endif

typedef enum
{
    RSSI_STATE_GREEN    = 0,
    RSSI_STATE_YELLOW,
    RSSI_STATE_RED,
} RSSI_STATE;

typedef enum 
{
    LED_STATE_OFF     = 0,
    LED_STATE_NORMAL,
    LED_STATE_PAIR_DISC,
} led_state_t;

typedef struct 
{
    led_state_t state;
    //*** ...  */
    uint8_t blink_cnt;
    bool ever_paired; //! 위해 TEST할 때 추가했음 빼도될ㄷㄱㄷ극
    // struct
    // {
    //     bool pl_tog_flag = false;
    //     uint8_t pl_state = 0;
    // }status

} led_ctrl_t;

typedef struct _peer_led_t
{
    bool pl_tog_flag = false; 
    uint8_t pl_state = 0; 
    uint8_t led_pairing_state[MAX_PEER] = {0,}; //NA
    uint8_t red_led_toggle_cnt = 0;
    uint16_t EtherCAT_pairing_bit = 0;
#if 0
    union
    {
        uint16_t field_chk;
        struct
        {
            uint16_t lsb : 1;
            uint16_t  : 1;
            uint16_t  : 1;
            uint16_t  : 1;

            uint16_t  : 1;
            uint16_t  : 1;
            uint16_t  : 1;
            uint16_t  : 1;

            uint16_t  : 1;
            uint16_t  : 1;
            uint16_t  : 1;
            uint16_t  : 1;

            uint16_t  : 1;
            uint16_t  : 1;
            uint16_t  : 1;
            uint16_t msb : 1;
        } bitfield;
    } flag;
#endif
}peer_led_t;

extern volatile bool g_led_ready;
extern peer_led_t peer_led; //^ temp..삭제 
//extern led_ctrl_t led_ctrl[MAX_PEER];
//extern Adafruit_NeoPixel strip;
//extern TaskHandle_t h_led_task; 
  

#endif
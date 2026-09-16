#ifndef _PEER_CHK_LED_H
#define _PEER_CHK_LED_H

#include <Arduino.h>
//#include <Adafruit_NeoPixel.h>
#include "led_drv.h"
#include "ap_context.h"

/*
 *====================================================================
 *  Peer LED State Transition Diagram
 *====================================================================
 *
 * Per-PEER LED state machine
 *
 * States (per peer_num):
 *   LED_STATE_OFF       : 아직 한 번도 페어링된 적 없음 (ever_paired == false)
 *   LED_STATE_NORMAL    : 현재 pairing OK, 정상 통신 중 (Green / Yellow 토글)
 *   LED_STATE_PAIR_DISC : 이전에 pairing 이 있었으나 현재는 disconnect 상태 (Red 토글)

 * 동작 요약:
 *  - 처음 페어링되면: OFF → NORMAL, ever_paired = true
 *  - 페어링이 끊기면: NORMAL → PAIR_DISC (Red 깜빡)
 *  - 다시 페어링되면: PAIR_DISC → NORMAL
 *  - 페어링된 적이 한 번도 없는 peer는 계속 OFF 유지
 */

#define ATOMIC  1
/***************************************************** */
#define FST                         0                  //
#define NUMPIXELS                   16                 // LED수, 
#define BRIGHTNESS                  15                 // 헤더,0-255  
#define GREEN_Boundary              -68                //
//#define YELLOW_Boundary             -41              //
                                                       //
#define LED_OFF     led_drv_color(0, 0, 0)             //   //!여기 일단 함수매크로 나중에 static inline uint@ lefd-color_off()이런식으로 수정 요망 
#define LED_RED     led_drv_color(255, 0, 0)           //
#define LED_GREEN   led_drv_color(0, 255, 0)           //
#define LED_YELLOW  led_drv_color(255, 255, 0)         //
#define LED_PURPLE  led_drv_color(128, 0, 128)       //
//#define LED_PURPLE  led_drv_color(0,139,139)             //platIO build test...0528

                                                       //
#define LED                         2                  // Onboard LED Blue
#define PEER_CHK_LED                12                 //
                                                       //
#define Serial_                     0                  //
#define RSSI_TEST                   1                  //
                                                       //
                                                       //
/***************************************************** */ 
//#define GET_RSSI_STATE(rssi) \
//    ((rssi) > GREEN_Boundary ? RSSI_STATE_GREEN : ((rssi) > YELLOW_Boundary ? RSSI_STATE_YELLOW : RSSI_STATE_RED))

#define GET_RSSI_STATE(rssi)\
    ((rssi) >= GREEN_Boundary ? RSSI_STATE_GREEN : RSSI_STATE_YELLOW)



typedef enum
{
    // 통신 중인 peer의 RSSI 표시 색상 상태.
    // RED는 RSSI 품질 표시가 아니라 disconnect blink에서 별도로 사용한다.
    RSSI_STATE_GREEN    = 0,
    RSSI_STATE_YELLOW,
    RSSI_STATE_RED,
} RSSI_STATE;

typedef enum 
{
    // peer별 LED 표시 상태.
    // LED_Task()에서 PairMask 변화에 따라 OFF/NORMAL/PAIR_DISC가 전환된다.
    LED_STATE_OFF     = 0,
    LED_STATE_NORMAL,
    LED_STATE_PAIR_DISC,
} led_state_t;

typedef struct 
{
    // peer 한 채널의 LED state machine 저장 영역이다.
    led_state_t state;
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
    // 전체 peer LED task에서 공유하는 표시 보조 상태.
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

typedef struct _ap_led_cfg_t
{
    // board별 LED driver 설정값. setup()에서 g_led 초기화 시 사용한다.
    uint8_t pixel_count;
    uint8_t brightness;
    uint8_t pixel_gpio;
    uint8_t onboard_led_gpio;
} ap_led_cfg_t;

typedef struct _ap_led_ctx_t
{
    // LED module runtime context.
    // led_ctrl[]는 peer별 상태, ready는 driver 초기화 완료 여부를 나타낸다.
    ap_led_cfg_t cfg;
    peer_led_t peer_led;
    led_ctrl_t led_ctrl[MAX_PEER];
    //Adafruit_NeoPixel strip;
    volatile bool ready;
} ap_led_ctx_t;

typedef struct _ap_led_task_arg_t
{
    // FreeRTOS LED task에 넘기는 인자 묶음.
    // app context에서 PairMask/RSSI 표시용 데이터를 읽고 led context에 결과를 쓴다.
    ap_led_ctx_t *led;
    ap_app_ctx_t *app; //context header 
} ap_led_task_arg_t;

extern ap_led_ctx_t g_led;


void peer_led_init_ctx(ap_led_ctx_t *led, const ap_led_cfg_t *cfg);

void update_pairing_led(ap_led_ctx_t *led, bool paired);
void show_led_init(ap_led_ctx_t *led);
void peer_led_setup(ap_led_ctx_t *led);
void set_peer_LED(ap_led_ctx_t *led, int rssi, uint8_t peer_num);
void set_peer_LED_state(ap_led_ctx_t *led,
                        uint8_t peer_num,
                        int rssi,
                        bool paired_flag,
                        bool peerLED_tog_flag,
                        bool any_paired);
void LED_Task(ap_led_ctx_t *led, ap_app_ctx_t *app);
void LedTask(void *arg);
void LedDrvServiceTask(void *arg);
#if 0
void (*P_show_led_init)(void);
void (*P_peer_led_setup)(void);
void (*P_set_peer_LED)(int,uint8_t);
void (*P_set_peer_LED_state)(uint8_t,bool);
void (*P_LED_Task)(void);
#endif
#endif

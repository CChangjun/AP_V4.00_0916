#include "peer_chk_led.h"
//#define NDEBUG
//#include <assert.h>
//* WS2812 LED는 데이터 전송 끝나고 300µs 이상 latch 시간이 필요햐
peer_led_t peer_led; //^ chk p..
led_ctrl_t led_ctrl[MAX_PEER] = { {LED_STATE_OFF, 0}, };
Adafruit_NeoPixel strip = Adafruit_NeoPixel(NUMPIXELS,12, NEO_GRB + NEO_KHZ800);

#define TEST    1


void show_led_init(void)
{
    strip.fill(strip.Color(LED_PURPLE),FST, NUMPIXELS); 
    strip.show();

    delay(500);

    strip.fill(strip.Color(LED_OFF),FST, NUMPIXELS); 
    strip.show();
}


//^ mapping 보단 그냥 threshold가 더 나을 것 같아서? 
void peer_led_setup(void)
{
    strip.setBrightness(BRIGHTNESS);
    strip.begin(); 
    strip.show();
    show_led_init();
    Serial.printf("[MSG]PEER LED SETUP::OK\r\n");
}

void set_peer_LED(int rssi, uint8_t peer_num) // 인자로 p_n까지 받을까? peer별로 다
{
    peer_led.pl_state = GET_RSSI_STATE(rssi);
    
    switch (peer_led.pl_state)
    {
    case RSSI_STATE_GREEN:
        //* GREEN ON 
        strip.setPixelColor(peer_num, LED_GREEN); 
        break; 

    case RSSI_STATE_YELLOW:
        //^ YELLOW ON
        strip.setPixelColor(peer_num,LED_YELLOW);  
        break;  
        //todo red 역할 변경 --> 한덕매니저님
#if 0
    case RSSI_STATE_RED:
        strip.setPixelColor(peer_num, 255, 0, 0);
        //! RED ON
        break;
#endif
    default:
        break;  
    }    
}
void set_peer_LED_state(uint8_t peer_num, bool paired_flag, bool peerLED_tog_flag)
{//todo red 역할 및 toggle 변경 $$ paired --> disconnected : RED로 수정- 한덕매니저님 

    if (peer_num >= MAX_PEER) return; 
    
    switch(led_ctrl[peer_num].state)//! 인자로 한 번 받고 그거 쓰는게 나을 듯 ? 
    {
        case LED_STATE_PAIR_DISC:
            if(peer_led.EtherCAT_pairing_bit)
            {
                if (peerLED_tog_flag)
                //if (!paired_flag && peerLED_tog_flag)
                    strip.setPixelColor(peer_num, LED_RED);
                else
                    strip.setPixelColor(peer_num, LED_OFF);
            }
            else strip.setPixelColor(peer_num,LED_OFF);

            break;
        case LED_STATE_NORMAL:
            if (paired_flag && peerLED_tog_flag)
                set_peer_LED(RSSI[peer_num], peer_num);
            else
                strip.setPixelColor(peer_num, LED_OFF);
            break;
        case LED_STATE_OFF:
        default:
            strip.setPixelColor(peer_num, LED_OFF);
            break;
    }
}
                                
            //^ SHOW() : data latch time 300us 정도? 무조건 잡음
            //^ LED 1개당 약 30us
            //^ N:LED 개수
            //!   T_exec  ≈ (LED_num * 28~30us쯤?) + 300us latch    
            //*   For 16 LEDs: (16 * 30us) + 300us ≈ 768~800us

#if TEST == 0
void LED_Task(void)
{
    //! pairFlag 인자로 받아야할 ㄱ듯
    //digitalWrite(TEST_POINT,HIGH);
    peer_led.pl_tog_flag ^= 1; //temp ... 
    
    for (uint8_t peer_num = 0; peer_num < MAX_PEER; peer_num++)
    {   //!판단 조건이 flag 하나여서 조금~, 고민 좀 
        if (led_ctrl[peer_num].state == LED_STATE_OFF && peer[peer_num].pairFlag)       led_ctrl[peer_num].state = LED_STATE_NORMAL;
        else if (led_ctrl[peer_num].state == LED_STATE_NORMAL && !peer[peer_num].pairFlag)      led_ctrl[peer_num].state = LED_STATE_PAIR_DISC;
        else if (led_ctrl[peer_num].state == LED_STATE_PAIR_DISC && peer[peer_num].pairFlag)    led_ctrl[peer_num].state = LED_STATE_NORMAL;
        //else off로? 
        set_peer_LED_state(peer_num, peer[peer_num].pairFlag, peer_led.pl_tog_flag);
    }
    strip.show(); //! data latch Time ; 300us
    //digitalWrite(TEST_POINT,LOW); 
}

#endif

#if TEST == 0
void LED_Task(const peer_t *peer_list, const int *rssi_list, uint16_t pairing_bit, bool led_toggle_flag)
{
    
}

//! 현재 원복 코드 
#if TEST == 1
void LED_Task(void)
{
    //peer_led.pl_tog_flag ^= 1; 

    for (uint8_t peer_num = 0; peer_num < MAX_PEER; peer_num++)
    {
        if (led_ctrl[peer_num].state == LED_STATE_OFF && peer[peer_num].pairFlag) 
        {
            led_ctrl[peer_num].state = LED_STATE_NORMAL;
            led_ctrl[peer_num].ever_paired = true;
        }
        else if (led_ctrl[peer_num].state == LED_STATE_NORMAL && !peer[peer_num].pairFlag)
            led_ctrl[peer_num].state = LED_STATE_PAIR_DISC;
        
        else if (led_ctrl[peer_num].state == LED_STATE_PAIR_DISC && peer[peer_num].pairFlag) 
            led_ctrl[peer_num].state = LED_STATE_NORMAL;
        
        //일단 홀딩
        if (!peer[peer_num].pairFlag && led_ctrl[peer_num].ever_paired) 
            led_ctrl[peer_num].state = LED_STATE_PAIR_DISC;
        
        set_peer_LED_state(peer_num, peer[peer_num].pairFlag, peer_led.pl_tog_flag);
    }

    strip.show();
}

#endif 

#if TEST == 0           //! 일단 최종 코드. 위 함수로 원복시켜야 함. 지시사항 
void LED_Task(void)
{
    digitalWrite(14,HIGH);
    peer_led.pl_tog_flag ^= 1;

    bool all_disconnected = true;

    for (uint8_t peer_num = 0; peer_num < MAX_PEER; peer_num++)
    {
        if (led_ctrl[peer_num].state == LED_STATE_OFF && peer[peer_num].pairFlag) 
        {
            led_ctrl[peer_num].state = LED_STATE_NORMAL;
            led_ctrl[peer_num].ever_paired = true;
        }
        else if (led_ctrl[peer_num].state == LED_STATE_NORMAL && !peer[peer_num].pairFlag)
            led_ctrl[peer_num].state = LED_STATE_PAIR_DISC;

        else if (led_ctrl[peer_num].state == LED_STATE_PAIR_DISC && peer[peer_num].pairFlag)
            led_ctrl[peer_num].state = LED_STATE_NORMAL;

        // 연결된 peer가 하나라도 있다면~ 
        if (peer[peer_num].pairFlag)    all_disconnected = false;

        if (all_disconnected) //연결된 peer가 없을 때! 
        {
            if (led_ctrl[peer_num].ever_paired && peer_led.red_led_toggle_cnt < 30) 
            {
                if (peer_led.pl_tog_flag)   strip.setPixelColor(peer_num, LED_RED);
                else    strip.setPixelColor(peer_num, LED_OFF);
            } 
            else    strip.setPixelColor(peer_num, LED_OFF);
        } 
        else 
        {
            // 하나라도 연결된 peer가 있으면
            peer_led.red_led_toggle_cnt = 0;
            set_peer_LED_state(peer_num, peer[peer_num].pairFlag, peer_led.pl_tog_flag);
        }
    }

    if (all_disconnected && peer_led.red_led_toggle_cnt < 30)   peer_led.red_led_toggle_cnt++;

    strip.show();

    digitalWrite(14,LOW); //! 16peer 기준 all led 764us
}
#endif
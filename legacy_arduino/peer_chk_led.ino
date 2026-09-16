#include "peer_chk_led.h"
#include "PairMask.h"
//! peer chk led data line - p down 10k? ohm 달면 흐르지 않는듯? 
//* 한쪽에선 load 한쪽에선 store만 하니까 atomic도 8byte아래면 한 번에 하니까.. 
//* 굳이 mutex, semaphore까지 쓸 필요는 없을 듯?.. pairing bit가 8바이트도 아니고 해서 atomic으로 해봄 
//#define NDEBUG
//#include <assert.h>
//* WS2812 LED는 데이터 전송 끝나고 300µs 이상 latch 시간이 필요
peer_led_t peer_led; //^ chk p..
led_ctrl_t led_ctrl[MAX_PEER] = { {LED_STATE_OFF, 0}, };
Adafruit_NeoPixel strip = Adafruit_NeoPixel(NUMPIXELS,12, NEO_GRB + NEO_KHZ800);

volatile bool g_led_ready = false;

#if TEST == 0
//************'ATOMIC'으로 main 검색 */
void LedTask(void *arg) 
{
    vTaskDelay(pdMS_TO_TICKS(500)); //! 얘 대신 
#if Serial_ == 1
    Serial.printf("[MSG] LED Task Run on Core %d..\n", (int)xPortGetCoreID());
    Serial.printf("[MSG] Size =%d,'is lock free' =%d..\r\n",sizeof(g_pairMask),g_pairMask.is_lock_free()); //! 찾아보니까 8byte가 끝인듯 
    //! 여기에 flag 세워서 하드웨어나 ㅇstrip 초기화 완료 후 돌아가게 끔 while ! 사용 - glob flag 
    if(!(g_pairMask.is_lock_free() == true))    Serial.printf("[MSG] 'Lock free' is failed.. \r\n");
#endif 
    //! 현재 RSSI : ISR -> 전역 -> LED Task라 스냅샷으로 가져와야하나? main에서 안써서 필요없나? 

    TickType_t last   = xTaskGetTickCount();
    const TickType_t led_task_tick = pdMS_TO_TICKS(500); 

    for(;;) 
    {
        TP_HIGH();  
        LED_Task(); 
        strip.show();
        //update_pairing_led(peer_led.EtherCAT_pairing_bit);
        TP_LOW();
        vTaskDelayUntil(&last, led_task_tick); //!500ms
        //debug LED 동기화 시켜야 함
    }
}

#endif

void LedTask(void *arg) 
{
    //bool timeout_flag = false;
    vTaskDelay(pdMS_TO_TICKS(10));
#if Serial_ == 1
    Serial.printf("[MSG] LED Task Run on Core %d..\n", (int)xPortGetCoreID());
    Serial.printf("[MSG] Size = %d, 'is lock free' = %d..\r\n",sizeof(g_pairMask), g_pairMask.is_lock_free());
    if (!(g_pairMask.is_lock_free() == true))   Serial.printf("[MSG] 'Lock free' is failed..\r\n");
#endif 

    TickType_t start = xTaskGetTickCount();

    while (!g_led_ready) 
    {
        vTaskDelay(pdMS_TO_TICKS(10));

        if (xTaskGetTickCount() - start > pdMS_TO_TICKS(2000)) 
        {
            Serial.println("[LED] waiting for ready timed out");

            // 2초 초과 시 디버그용으로 점등켜놨ㅇ음. 어차피 바로 꺼지긴 할텐데 del쓰는거 아닌이상 
            strip.fill(strip.Color(LED_BLUE));   
            strip.show();
            //timeout_flag = true; //fㄹloop에서 쓸 수ㄷ도
            break;  
        }
    }
    TickType_t last = xTaskGetTickCount();
    const TickType_t led_task_tick = pdMS_TO_TICKS(500); 
    //! 일단 Delay 500ms 먼저 걸고 포루프 시작 
    for (;;) 
    {
        
        LED_Task(); 
        //TP_HIGH();  
        strip.show();
        //TP_LOW();

        vTaskDelayUntil(&last, led_task_tick);  // 500ms 주기 
    }
}

void update_pairing_led(bool paired) 
{
    if (paired)     digitalWrite(LED, peer_led.pl_tog_flag ? LOW : HIGH); 
    else    digitalWrite(LED, HIGH); 
}


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
    //! peer check LED 
    strip.setBrightness(BRIGHTNESS);
    strip.begin(); 
    strip.clear();
    strip.show();
    
    show_led_init();
    
    Serial.printf("[MSG]PEER LED SETUP::OK\r\n");

    g_led_ready = true;
}



void set_peer_LED(int rssi, uint8_t peer_num) // 인자로 p_n까지 받을까? peer별로 다
{
    //peer_led.pl_state = GET_RSSI_STATE(rssi); //! pl_state 여기도 차라리.. 
    const uint8_t pl_state = GET_RSSI_STATE(rssi);
    switch (pl_state)
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

#if 0
void set_peer_LED_state(uint8_t peer_num, bool paired_flag, bool peerLED_tog_flag)
{//todo red 역할 및 toggle 변경 $$ paired --> disconnected : RED로 수정- 한덕매니저님 

    if (peer_num >= MAX_PEER) return; 
    
    switch(led_ctrl[peer_num].state)//! 인자로 한 번 받고 그거 쓰는게 나을 듯 ? 
    {
        case LED_STATE_PAIR_DISC:
            if(peer_led.EtherCAT_pairing_bit) //! 이쪽 수정해야 할 듯. PLC 기준에서 상태보단 AP 무선 페어링 상태로 판단하는 것으로 일단 코드 작성 
            {
                if (peerLED_tog_flag)   strip.setPixelColor(peer_num, LED_RED);
                //if (!paired_flag && peerLED_tog_flag)
                else    strip.setPixelColor(peer_num, LED_OFF);
            }
            else strip.setPixelColor(peer_num,LED_OFF);
            break;

        case LED_STATE_NORMAL:
            if (paired_flag && peerLED_tog_flag)     set_peer_LED(RSSI[peer_num], peer_num);
            else    strip.setPixelColor(peer_num, LED_OFF);
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

void LED_Task(void)
{   //! 유지 시켜야 할ㅇ듯. atomic - snapshot 사용해보기 
    peer_led.pl_tog_flag ^= 1; 

    const uint32_t mask_snap = PairMask_Snapshot(); //! 추가 TEST

    for (uint8_t peer_num = 0; peer_num < MAX_PEER; peer_num++)
    {
    #if ATOMIC == 1
        //if (led_ctrl[peer_num].state == LED_STATE_OFF && peer[peer_num].pairFlag) 
        const bool paired = PairMask_Test(mask_snap, peer_num);
        if (led_ctrl[peer_num].state == LED_STATE_OFF && paired)
        {
            led_ctrl[peer_num].state = LED_STATE_NORMAL;
            led_ctrl[peer_num].ever_paired = true;
        }
        //else if (led_ctrl[peer_num].state == LED_STATE_NORMAL && !peer[peer_num].pairFlag)
        else if (led_ctrl[peer_num].state == LED_STATE_NORMAL && !paired)
            led_ctrl[peer_num].state = LED_STATE_PAIR_DISC;
        
        //else if (led_ctrl[peer_num].state == LED_STATE_PAIR_DISC && peer[peer_num].pairFlag)
        else if (led_ctrl[peer_num].state == LED_STATE_PAIR_DISC && paired) 
            led_ctrl[peer_num].state = LED_STATE_NORMAL;
        
        //! 아래 조건 일단 홀딩, 가끔 Red LED가 잠깐 보이는.. --> 이게 무선 페어링 상태랑 plc 페어링 상태랑 차이있어서그런가? 
        //if (!peer[peer_num].pairFlag && led_ctrl[peer_num].ever_paired) 
        if (!paired && led_ctrl[peer_num].ever_paired)
            led_ctrl[peer_num].state = LED_STATE_PAIR_DISC;
        //set_peer_LED_state(peer_num, peer[peer_num].pairFlag, peer_led.pl_tog_flag);
        set_peer_LED_state(peer_num, paired, peer_led.pl_tog_flag);
    #endif 
    }
    
    //strip.show();
}
#endif 

//* PLC가 보는 시스템 상태에 맞춘게 아니라 실제 무선 pairing 상태에 맞춤 

void set_peer_LED_state(uint8_t peer_num,
                        bool paired_flag,
                        bool peerLED_tog_flag,
                        bool any_paired) 
{
    if (peer_num >= MAX_PEER) return; 
    
    switch (led_ctrl[peer_num].state)
    {
        case LED_STATE_PAIR_DISC:
            // pairing된 peer가 1개라도 있어야 RED 토글
            if (any_paired) 
            {
                if (peerLED_tog_flag)   strip.setPixelColor(peer_num, LED_RED);
                else    strip.setPixelColor(peer_num, LED_OFF);
            } 
            else    strip.setPixelColor(peer_num, LED_OFF); // pairing된 peer가 1개라도 없으면 

            break;

        case LED_STATE_NORMAL:
            if (paired_flag && peerLED_tog_flag)    set_peer_LED(RSSI[peer_num], peer_num);
            else    strip.setPixelColor(peer_num, LED_OFF);
            break;

        case LED_STATE_OFF:
        default:
            strip.setPixelColor(peer_num, LED_OFF);
            break;
    }
}

void LED_Task(void)
{   //! 유지 시켜야 할ㅇ듯. atomic - snapshot 사용해보기 
    peer_led.pl_tog_flag ^= 1; 

    const uint32_t mask_snap = PairMask_Snapshot();
    const bool any_paired = (mask_snap != 0);       

    update_pairing_led(any_paired);
    
    for (uint8_t peer_num = 0; peer_num < MAX_PEER; peer_num++)
    {
    #if ATOMIC == 1
        //if (led_ctrl[peer_num].state == LED_STATE_OFF && peer[peer_num].pairFlag) 
        const bool paired = PairMask_Test(mask_snap, peer_num);
#if RSSI_TEST ==1
        if(paired == true)
        {
            if(RSSI[peer_num] != 0)  Serial.printf("%02d", RSSI[peer_num]);
        }
        
#endif 
        if (led_ctrl[peer_num].state == LED_STATE_OFF && paired)
        {
            led_ctrl[peer_num].state = LED_STATE_NORMAL;
            led_ctrl[peer_num].ever_paired = true;
        }
        //else if (led_ctrl[peer_num].state == LED_STATE_NORMAL && !peer[peer_num].pairFlag)
        else if (led_ctrl[peer_num].state == LED_STATE_NORMAL && !paired)
            led_ctrl[peer_num].state = LED_STATE_PAIR_DISC;
        
        //else if (led_ctrl[peer_num].state == LED_STATE_PAIR_DISC && peer[peer_num].pairFlag)
        else if (led_ctrl[peer_num].state == LED_STATE_PAIR_DISC && paired) 
            led_ctrl[peer_num].state = LED_STATE_NORMAL;
        
        // 아래 조건 일단 홀딩, 가끔 Red LED가 잠깐 보이는.. --> 이게 무선 페어링 상태랑 plc 페어링 상태랑 차이있어서그런가? 
        //! - led 업데이트 기준을 plc pairing bit가 아닌 실제 무선 통신 pairing bit로 처리 
        //if (!peer[peer_num].pairFlag && led_ctrl[peer_num].ever_paired) 
        if (!paired && led_ctrl[peer_num].ever_paired)
            led_ctrl[peer_num].state = LED_STATE_PAIR_DISC;
        //set_peer_LED_state(peer_num, peer[peer_num].pairFlag, peer_led.pl_tog_flag);
        set_peer_LED_state(peer_num, paired, peer_led.pl_tog_flag,any_paired);
    #endif 
    }
#if RSSI_TEST == 1    
    Serial.printf("\r\n");
#endif 
}

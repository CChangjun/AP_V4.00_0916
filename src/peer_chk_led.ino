#include "peer_chk_led.h"
#include "PairMask.h"
//! peer chk led data line - p down 10k? ohm 달면 흐르지 않는듯? 
// 한쪽에선 load 한쪽에선 store만 하니까 atomic도 8byte아래면 한 번에 하니까.. 
// 굳이 mutex, semaphore까지 쓸 필요는 없을 듯?.. pairing bit가 8바이트도 아니고 해서 atomic으로 해봄 

//* WS2812 LED lib은 데이터 전송 끝나고 300µs 이상 latch 시간이 필요

ap_led_ctx_t g_led = {};


void peer_led_init_ctx(ap_led_ctx_t *led, const ap_led_cfg_t *cfg)
{
    // LED context를 초기화하고 board별 설정값을 복사한다.
    // 실제 RMT driver 초기화는 peer_led_setup()에서 수행한다.
    if (!led || !cfg) return;

    memset(led, 0, sizeof(*led));
    led->cfg = *cfg;
}

//* ulTaskNotifyTake에 잠들어있다가 RMT 전송 끝나면 cb가 notify 전송하고, 서비스 태스크 깨어나는 concept 
// Tx 완료 후처리 전담 대기 task느낌 
void LedDrvServiceTask(void *arg)
{
    // RMT TX done callback이 notify를 주면 깨어나서 reset-hold 상태로 넘긴다.
    // LED 색 계산 task와 분리해서 ISR 후처리 시간을 짧게 유지한다.
    for (;;)
    {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY); //이 task를 done cb가 깨움 
        led_drv_tx_done_service();
    }
}

void LedTask(void *arg)
{
    // 주기적으로 pair 상태/RSSI를 snapshot해서 LED 논리 버퍼를 갱신하고 flush한다.
    // 실제 RMT 전송이 바쁘면 이번 주기 flush는 건너뛰고 다음 주기에 다시 시도한다.
    ap_led_task_arg_t *task_arg = (ap_led_task_arg_t *)arg;
    if (!task_arg || !task_arg->led || !task_arg->app)
    {
        vTaskDelete(NULL);
        return;
    }

    ap_led_ctx_t *led = task_arg->led;
    ap_app_ctx_t *app = task_arg->app;

    vTaskDelay(pdMS_TO_TICKS(10));

#if Serial_
// 홗인용 . . 
    Serial.printf("[MSG] LED Task Run on Core %d..\n", (int)xPortGetCoreID());
    Serial.printf("[MSG] Size = %d, 'is lock free' = %d..\r\n",sizeof(g_pairMask),g_pairMask.is_lock_free());
#endif
    TickType_t last = xTaskGetTickCount();
    const TickType_t led_task_tick = pdMS_TO_TICKS(500);

    for (;;)
    {
        LED_Task(led, app);
        if (!led_drv_is_busy()) (void)led_drv_flush_async();
        //!else 처리 예정 
        vTaskDelayUntil(&last, led_task_tick);
    }
}

void update_pairing_led(ap_led_ctx_t *led, bool paired)
{
    // 보드 내장 LED는 하나라도 paired peer가 있으면 toggle, 없으면 OFF(HIGH)로 둔다.
    if (!led) return;

    if (paired) digitalWrite(LED, led->peer_led.pl_tog_flag ? LOW : HIGH);
    else        digitalWrite(LED, HIGH);
}


void show_led_init(ap_led_ctx_t *led)
{
    // 부팅 시 LED 라인/RMT 구동 확인용 초기 표시다.
    // LED_PURPLE 색을 한 번 표시한 뒤 전체 OFF로 되돌린다.
    if (!led) return;

    led_drv_fill(LED_PURPLE);
    (void)led_drv_flush_async();

    while (led_drv_is_busy())   vTaskDelay(1);

    vTaskDelay(pdMS_TO_TICKS(500));

    led_drv_fill(led_drv_color(0, 0, 0));
    (void)led_drv_flush_async();

    while (led_drv_is_busy())   vTaskDelay(1);
}

void peer_led_setup(ap_led_ctx_t *led)
{
    // peer LED driver를 초기화하고 초기 표시까지 완료한 뒤 ready 상태로 전환한다.
    if (!led) return;

    led_drv_cfg_t drv_cfg =
    {
        .pixel_count = led->cfg.pixel_count,
        .brightness = led->cfg.brightness,
        .pixel_gpio = led->cfg.pixel_gpio,
    };

    if (!led_drv_init(&drv_cfg))
    {
        Serial.printf("[ERR]PEER LED SETUP::DRV INIT FAIL\r\n");
        led->ready = false;
        return;
    }

    led_drv_clear();
    (void)led_drv_flush_async();

    while (led_drv_is_busy())   vTaskDelay(1);

    show_led_init(led);

    Serial.printf("[MSG]PEER LED SETUP::OK\r\n");
    led->ready = true;
}

void set_peer_LED(ap_led_ctx_t *led, int rssi, uint8_t peer_num)
{
    // paired 상태의 peer LED 색을 RSSI 기준으로 결정한다.
    // 현재 정책은 GREEN/YELLOW만 사용하고 RED는 disconnect 표시에서 사용한다.
    if (!led) return;

    led->peer_led.pl_state = GET_RSSI_STATE(rssi);

    switch (led->peer_led.pl_state)
    {
        case RSSI_STATE_GREEN:
            led_drv_set_pixel(peer_num, LED_GREEN);
            break;

        case RSSI_STATE_YELLOW:
            led_drv_set_pixel(peer_num, LED_YELLOW);
            break;

        default:
            led_drv_set_pixel(peer_num, LED_OFF);
            break;
    }
}

void set_peer_LED_state(ap_led_ctx_t *led,
                        uint8_t peer_num,
                        int rssi,
                        bool paired_flag,
                        bool peerLED_tog_flag,
                        bool any_paired)
{
    // peer별 LED state에 따라 실제 pixel 색을 결정한다.
    // NORMAL은 RSSI 색 토글, PAIR_DISC는 red blink, OFF는 항상 소등이다.
    if (!led) return;
    if (peer_num >= MAX_PEER) return;

    switch (led->led_ctrl[peer_num].state)
    {
        case LED_STATE_PAIR_DISC:
            // 과거에 paired 되었던 peer가 끊어진 상태다.
            // 전체 AP에 paired peer가 하나라도 있을 때 red blink로 disconnect를 표시한다.
            if (any_paired)
            {
                if (peerLED_tog_flag) led_drv_set_pixel(peer_num, LED_RED);
                else                  led_drv_set_pixel(peer_num, LED_OFF);
            }
            else    led_drv_set_pixel(peer_num, LED_OFF);
            break;

        case LED_STATE_NORMAL:
            // 현재 paired 상태다. toggle 주기에 맞춰 RSSI 색을 깜빡인다.
            if (paired_flag && peerLED_tog_flag) set_peer_LED(led, rssi, peer_num);
            else                                 led_drv_set_pixel(peer_num, LED_OFF);
            break;

        case LED_STATE_OFF:
        default:
            // 한 번도 paired 되지 않았거나 표시 대상이 아닌 채널은 꺼둔다.
            led_drv_set_pixel(peer_num, LED_OFF);
            break;
    }
}

void LED_Task(ap_led_ctx_t *led, ap_app_ctx_t *app) // 색/상태 계산 
{
    // LED 상태 계산의 핵심 함수다.
    // PairMask와 RSSI를 snapshot으로 잡은 뒤 peer별 state machine을 갱신한다.
    if (!led || !app) return;

    led->peer_led.pl_tog_flag ^= 1;

    const uint32_t mask_snap = PairMask_Snapshot();

    int rssi_snap[MAX_PEER];
    for (uint8_t i = 0; i < MAX_PEER; i++)
    {
        // LED 계산 중 app->peer.rssi[]가 바뀌어도 이번 주기는 같은 snapshot 기준으로 표시한다.
        rssi_snap[i] = app->peer.rssi[i];
    }

    for (uint8_t peer_num = 0; peer_num < MAX_PEER; peer_num++)
    {
        const bool paired = PairMask_Test(mask_snap, peer_num);

        if (led->led_ctrl[peer_num].state == LED_STATE_OFF && paired)
        {
            // 처음 paired된 채널이다. 이후 disconnect 표시를 위해 ever_paired를 남긴다.
            led->led_ctrl[peer_num].state = LED_STATE_NORMAL;
            led->led_ctrl[peer_num].ever_paired = true;
        }
        else if (led->led_ctrl[peer_num].state == LED_STATE_NORMAL && !paired && led->led_ctrl[peer_num].ever_paired)
        {
            // paired 상태였던 peer가 PairMask에서 빠지면 disconnect 표시 상태로 전환한다.
            led->led_ctrl[peer_num].state = LED_STATE_PAIR_DISC;
        }

        else if (led->led_ctrl[peer_num].state == LED_STATE_PAIR_DISC && paired)    
        {
            // disconnect 표시 중이던 peer가 다시 paired되면 정상 표시로 복귀한다.
            led->led_ctrl[peer_num].state = LED_STATE_NORMAL;
        }
        
        set_peer_LED_state(led,peer_num,rssi_snap[peer_num],paired,led->peer_led.pl_tog_flag,(mask_snap != 0));
    }
    update_pairing_led(led, (mask_snap != 0));
}

#if 0
void rssi_display()
{
    for (uint8_t i = 0; i < MAX_PEER; i++)
    {
        if (g_ap.peer.peer[i].pairFlag == true)
        {
            Serial.printf("RSSI[%02d] = %02d, ", i, g_ap.peer.rssi[i]);
        }
        
    }
    
}
#endif 

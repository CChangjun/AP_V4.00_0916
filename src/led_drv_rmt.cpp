#include "led_drv.h"

#if LED_DRV_USE_RMT
//* WS2812/NeoPixel 계열 LED를 ESP32 RMT peripheral로 구동하는 구현부

// 상위 peer_chk_led 코드는 led_drv_set_pixel()/fill()로 논리 색 버퍼만 바꾸고,
// led_drv_flush_async()가 해당 버퍼를 RMT 파형으로 변환해 비동기 전송한다.

#include <Arduino.h>
#include "driver/rmt.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"


namespace
{
    enum class led_tx_state_t : uint8_t //쓸ㄹ때 cast써서 형변환해야함 enum class는 
    {
        IDLE = 0,   //새 전송 가능
        TX_ACTIVE,  //지금 RMT가 전송 중 
        RESET_HOLD, //전송은 끝났고, data latch time 기다리는 중 
    };

    static volatile led_tx_state_t g_tx_state = led_tx_state_t::IDLE; 

    static TaskHandle_t g_service_task_handle = nullptr;
    static int64_t g_reset_deadline_us = 0;                           //reset hold를 끝내도 되는 시각 
    static portMUX_TYPE g_led_state_mux = portMUX_INITIALIZER_UNLOCKED;
    static constexpr rmt_channel_t LED_RMT_CHANNEL = RMT_CHANNEL_0; //일단 0으로 채널 고정 0-7까지 8개 channel 존재 
    static constexpr uint8_t LED_RMT_CLK_DIV = 2;                   // 80MHz/2 = 40MHz -> 25ns/tick
    static constexpr size_t LED_RMT_MAX_PIXELS = 16;                // LED 수 
    static constexpr size_t LED_RMT_ITEMS_PER_PIXEL = 24;           //rgb 각8bit, 총 24bit
    static rmt_item32_t g_items[LED_RMT_MAX_PIXELS * LED_RMT_ITEMS_PER_PIXEL]; //* RMT가 쏠 실제 HW 파형 배열 

    // WS2812 timing @ 800kHz
    // 0 bit: T0H ~0.4us, T0L ~0.85us
    // 1 bit: T1H ~0.8us, T1L ~0.45us
    // 25ns tick 기준 근사
    //* data sheet상... TH + TL이 1.25us ±600ns가 나와야 함 
    static constexpr uint16_t T0H_TICKS = 14;   //0.35us
    static constexpr uint16_t T0L_TICKS = 36;   //0.9us
    static constexpr uint16_t T1H_TICKS = 28;   //0.7us
    static constexpr uint16_t T1L_TICKS = 22;   //0.55us
    static constexpr uint16_t POST_FLUSH_RESET_US = 100;   //! data sheetㅅ상 최소 50us 

    static bool g_inited = false;

    static uint8_t g_pixel_count = 0;
    static uint8_t g_pixel_gpio = 0;
    static uint8_t g_brightness = 0;

    static uint8_t g_pixels[LED_RMT_MAX_PIXELS * 3];    //* 논리 색 버퍼

    //! RMT 전송 끝나면 서비스 task만 깨움 
    static void IRAM_ATTR led_rmt_tx_done_cb(rmt_channel_t channel, void *arg)
    {
        // 전송 완료 알림만 service task로 넘기고, reset-hold 진입은 led_drv_tx_done_service()에서 처리한다.
        if (channel != LED_RMT_CHANNEL) return;

        BaseType_t xTaskWoken = pdFALSE;

        if (g_service_task_handle != nullptr)
        {
            vTaskNotifyGiveFromISR(g_service_task_handle, &xTaskWoken);
            if (xTaskWoken == pdTRUE)    portYIELD_FROM_ISR();
            //바로 Context Switching해서 serviceTask 꺠움 
            //! 근데 바로 함수 점프가 아니라 테스크 스위칭 요청이던디 
            //* 여기서 다시 스케줄링에 따라 다른 task로 넘어가는데, 이거 나중에 조절하면 될 듯. 
        }
    }

    static inline void led_drv_update_rst_hold_state_locked(int64_t now_us)
    {
        // RESET_HOLD는 WS2812 latch 시간을 보장하기 위한 소프트웨어 대기 상태.
        // 지정 시간이 지나면 다음 flush가 가능하도록 IDLE로 복귀한다.
        if (!g_inited) return;
        if (g_tx_state != led_tx_state_t::RESET_HOLD) return;

        if (now_us >= g_reset_deadline_us)
        {
            g_reset_deadline_us = 0;
            g_tx_state = led_tx_state_t::IDLE;
        }
    }


    static inline uint8_t apply_brightness(uint8_t v)
    {
        // g_pixels에는 원본 색을 보관하고, 실제 전송 직전에 brightness만 반영한다.
        return (uint16_t(v) * uint16_t(g_brightness)) / 255u;
    }

    static inline void encode_bit(bool one, rmt_item32_t &item)
    {
        // RMT item 하나가 WS2812 data bit 하나의 high/low pulse를 표현한다.
        if (one)
        {
            item.level0 = 1;
            item.duration0 = T1H_TICKS;
            item.level1 = 0;
            item.duration1 = T1L_TICKS;
        }
        else
        {
            item.level0 = 1;
            item.duration0 = T0H_TICKS;
            item.level1 = 0;
            item.duration1 = T0L_TICKS;
        }
    }

    // GRB -> RMT 형식 배열로 바꾸고 encoding bit 넣고~ GRB 재정렬 
    static size_t build_rmt_items(rmt_item32_t *items, size_t max_items)
    {
        if (!items) return 0;

        const size_t needed = size_t(g_pixel_count) * LED_RMT_ITEMS_PER_PIXEL;
        if (max_items < needed) return 0;

        size_t out = 0;

        for (uint8_t i = 0; i < g_pixel_count; ++i)
        {
            const uint8_t g = apply_brightness(g_pixels[i * 3 + 0]);
            const uint8_t r = apply_brightness(g_pixels[i * 3 + 1]);
            const uint8_t b = apply_brightness(g_pixels[i * 3 + 2]);
            //data sheet: Follow the order of ''GRB' to sent data and the high bit sent at first.라고 적혀있음 
            const uint8_t grb[3] = {g,r,b};

            for (uint8_t c = 0; c < 3; ++c)
            {
                for (int bit = 7; bit >= 0; --bit)
                {
                    encode_bit((grb[c] >> bit) & 0x01, items[out++]);
                }
            }
        }

        return out;
    }

}

//* drv init/deinit 때 전송 상태도 같이 날림 
bool led_drv_init(const led_drv_cfg_t *cfg)
{
    // 드라이버 초기화 시 RMT channel 설정과 내부 상태를 모두 초기화
    // 이미 초기화되어 있으면 기존 channel을 정리하고 다시 설정한다.
    if (!cfg) return false;
    if (cfg->pixel_count == 0) return false;
    if (cfg->pixel_count > LED_RMT_MAX_PIXELS) return false;

    if (g_inited) led_drv_deinit();

    g_pixel_count = cfg->pixel_count;
    g_pixel_gpio = cfg->pixel_gpio;
    g_brightness = cfg->brightness;

    g_reset_deadline_us = 0;

    rmt_config_t rmt_tx = {};
    rmt_tx.rmt_mode = RMT_MODE_TX;
    rmt_tx.channel = LED_RMT_CHANNEL;
    rmt_tx.gpio_num = (gpio_num_t)g_pixel_gpio;
    rmt_tx.mem_block_num = 8;
    rmt_tx.clk_div = LED_RMT_CLK_DIV;
    rmt_tx.tx_config.loop_en = false;
    rmt_tx.tx_config.carrier_en = false;
    rmt_tx.tx_config.idle_output_en = true;
    rmt_tx.tx_config.idle_level = RMT_IDLE_LEVEL_LOW;

    if (rmt_config(&rmt_tx) != ESP_OK) return false;

    if (rmt_driver_install(LED_RMT_CHANNEL, 0, 0) != ESP_OK) return false;

    rmt_register_tx_end_callback(led_rmt_tx_done_cb, nullptr);

    g_inited = true;
    g_tx_state = led_tx_state_t::IDLE;

    return true;
}

//! deinit 중 tx가 진행중이면 어떻게 할지도 보강해야 함.ㅐㅏ
void led_drv_deinit(void)
{
    // deinit 중 새 flush가 시작되지 않도록 먼저 g_inited와 tx state를 닫는다.
    bool was_inited = false;

    portENTER_CRITICAL(&g_led_state_mux);

    was_inited = g_inited;
    g_inited = false;
    g_tx_state = led_tx_state_t::IDLE;
    g_reset_deadline_us = 0;

    portEXIT_CRITICAL(&g_led_state_mux);

    if (was_inited)
    {
        //해당 RMT channel에 이미 시작된 Tx가 있다면, 살짝 대기 
        (void)rmt_wait_tx_done(LED_RMT_CHANNEL, pdMS_TO_TICKS(2));
        (void)rmt_driver_uninstall(LED_RMT_CHANNEL);
    }

    g_pixel_count = 0;
    g_pixel_gpio = 0;
    g_brightness = 0;
}

void led_drv_bind_service_task(TaskHandle_t task_handle)
{
    // RMT TX done callback이 깨울 task handle을 등록한다.
    // 현재 구조에서는 LedDrvServiceTask가 이 역할을 맡는다.
    g_service_task_handle = task_handle;
}

void led_drv_tx_done_service(void)
{
    // RMT 전송 완료 후처리다.
    // LED data는 이미 나갔지만 WS2812 latch 시간 동안 다음 전송을 막기 위해 RESET_HOLD로 전환한다.
    const int64_t now_us = esp_timer_get_time();

    portENTER_CRITICAL(&g_led_state_mux);

    if (!g_inited || (g_tx_state != led_tx_state_t::TX_ACTIVE))
    {
        portEXIT_CRITICAL(&g_led_state_mux);
        return;
    }

    g_tx_state = led_tx_state_t::RESET_HOLD;
    g_reset_deadline_us = now_us + POST_FLUSH_RESET_US;
    //현재 시각 기준 100us 뒤 시각을 저장한 후에 , 나중에 get time()을 읽어서 상태 판단하는 것으로 변경.
    portEXIT_CRITICAL(&g_led_state_mux);
}


void led_drv_clear(void)
{
    // 실제 LED 반영은 flush_async() 호출 시점에 된다.
    if (!g_inited) return;
    memset(g_pixels, 0, size_t(g_pixel_count) * 3);
}

void led_drv_set_pixel(uint8_t idx, uint32_t color)
{
    // 상위 코드는 RGB color를 넘기지만, WS2812 전송 순서는 GRB라서 내부 버퍼는 GRB 순서로 저장한다.
    if (!g_inited) return;
    if (idx >= g_pixel_count) return;

    // color format: 0x00RRGGBB 원래는 GGRRBB
    const uint8_t r = (color >> 16) & 0xFF;
    const uint8_t g = (color >> 8) & 0xFF;
    const uint8_t b = color & 0xFF;

    g_pixels[idx * 3 + 0] = g;
    g_pixels[idx * 3 + 1] = r;
    g_pixels[idx * 3 + 2] = b;
}

void led_drv_fill(uint32_t color)
{
    // 전체 pixel 논리 버퍼를 같은 색으로 채운다. 이 함수 자체는 전송하지 않는다.
    if (!g_inited) return;

    for (uint8_t i = 0; i < g_pixel_count; ++i) led_drv_set_pixel(i, color);
}

uint32_t led_drv_color(uint8_t r, uint8_t g, uint8_t b)
{
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

bool led_drv_is_busy(void)
{
    // TX_ACTIVE/RESET_HOLD 상태면 busy로 봄
    // RESET_HOLD 만료 여부도 이 함수에서 갱신하므로 polling 호출이 상태 진행 역할도 한다.
    const int64_t now_us = esp_timer_get_time();
    bool is_busy = false;

    portENTER_CRITICAL(&g_led_state_mux);

    led_drv_update_rst_hold_state_locked(now_us);
    is_busy = g_inited && (g_tx_state != led_tx_state_t::IDLE);

    portEXIT_CRITICAL(&g_led_state_mux);

    return is_busy;
}

bool led_drv_flush_async(void)
{
    // 논리 색 버퍼를 RMT item 배열로 변환하고 비동기 전송을 시작한다.
    // 전송 중이거나 reset-hold 중이면 false를 반환해서 상위 task가 다음 주기에 다시 시도하게 한다.
    const int64_t now_us = esp_timer_get_time();

    portENTER_CRITICAL(&g_led_state_mux);

    led_drv_update_rst_hold_state_locked(now_us);

    if (!g_inited || (g_tx_state != led_tx_state_t::IDLE))
    {
        portEXIT_CRITICAL(&g_led_state_mux);
        return false;
    }

    g_tx_state = led_tx_state_t::TX_ACTIVE; //다른 경로가 동시에 들어와 중복flush를 시작할 수도 있어서, 먼저 tx_act 선점해야 할 듯
    g_reset_deadline_us = 0;

    portEXIT_CRITICAL(&g_led_state_mux);
    //TP_HIGH();
    const size_t item_count = size_t(g_pixel_count) * LED_RMT_ITEMS_PER_PIXEL;
    if (item_count > (LED_RMT_MAX_PIXELS * LED_RMT_ITEMS_PER_PIXEL))
    {
        // 내부 RMT item 버퍼보다 큰 요청이면 선점해 둔 TX_ACTIVE 상태를 되돌림. 
        portENTER_CRITICAL(&g_led_state_mux);
        if (g_tx_state == led_tx_state_t::TX_ACTIVE)
        {
            g_tx_state = led_tx_state_t::IDLE;
        }
        portEXIT_CRITICAL(&g_led_state_mux);
        return false;
    }

    const size_t built = build_rmt_items(
        g_items,
        LED_RMT_MAX_PIXELS * LED_RMT_ITEMS_PER_PIXEL
    );

    if (built == 0)
    {
        // encoding 실패 시에도 다음 flush가 막히지 않도록 IDLE로 rollback 시켜야 할 듯 , 完
        portENTER_CRITICAL(&g_led_state_mux);
        if (g_tx_state == led_tx_state_t::TX_ACTIVE)
        {
            g_tx_state = led_tx_state_t::IDLE;
        }
        portEXIT_CRITICAL(&g_led_state_mux);
        return false;
    }

    esp_err_t err = rmt_write_items(LED_RMT_CHANNEL, g_items, built, false);
    //TP_LOW();
    //실패할ㄹ경우 rollback해야함 
    if (err != ESP_OK)
    {
        // RMT 전송 시작 자체가 실패하면 TX_ACTIVE를 해제.
        // 성공한 경우에는 TX done callback -> service task에서 RESET_HOLD로 넘어간다.
        portENTER_CRITICAL(&g_led_state_mux);
        if (g_tx_state == led_tx_state_t::TX_ACTIVE)
        {
            g_tx_state = led_tx_state_t::IDLE;
        }
        portEXIT_CRITICAL(&g_led_state_mux);
        return false;
    }

    return true;
}
#endif

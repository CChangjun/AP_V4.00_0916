#pragma once

#include <Arduino.h>
#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_drv_select.h"

typedef struct
{
    uint8_t pixel_count;
    uint8_t brightness;
    uint8_t pixel_gpio;
} led_drv_cfg_t;

bool led_drv_init(const led_drv_cfg_t *cfg);

void led_drv_deinit(void);
void led_drv_clear(void);
void led_drv_set_pixel(uint8_t idx, uint32_t color);
void led_drv_fill(uint32_t color);

bool led_drv_flush_async(void); //상위에서 쓸 비동기 시작 햠ㅁ수 
bool led_drv_is_busy(void);
void led_drv_bind_service_task(TaskHandle_t task_handle);
void led_drv_tx_done_service(void);

uint32_t led_drv_color(uint8_t r, uint8_t g, uint8_t b);
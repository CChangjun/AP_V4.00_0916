#include "led_drv.h"

#if LED_DRV_USE_NEOPIXEL
#include <Adafruit_NeoPixel.h>

namespace
{
    //! 이런!!!!!!!!!!!!!!!!!!!!아래 *g_strip로 바꾸고 상위레벨RGB순서 바꾸니까 됨 
    static Adafruit_NeoPixel *g_strip = nullptr;
    static bool g_inited = false;
    static uint8_t g_pixel_count = 0;
}

bool led_drv_init(const led_drv_cfg_t *cfg)
{
    if (!cfg) return false;

    if (g_strip)
    {
        delete g_strip;
        g_strip = nullptr;
    }

    g_pixel_count = cfg->pixel_count;
    g_strip = new Adafruit_NeoPixel(cfg->pixel_count, cfg->pixel_gpio, NEO_GRB + NEO_KHZ800);
    if (!g_strip) return false;

    g_strip->setBrightness(cfg->brightness);
    g_strip->begin();
    g_strip->clear();
    g_strip->show();

    g_inited = true;
    return true;
}

void led_drv_deinit(void)
{
    g_inited = false;
    g_pixel_count = 0;

    if (g_strip)
    {
        delete g_strip;
        g_strip = nullptr;
    }
}

void led_drv_clear(void)
{
    if (!g_inited || !g_strip) return;
    g_strip->clear();
}

void led_drv_set_pixel(uint8_t idx, uint32_t color)
{
    if (!g_inited || !g_strip) return;
    if (idx >= g_pixel_count) return;

    const uint8_t r = (color >> 16) & 0xFF;
    const uint8_t g = (color >> 8) & 0xFF;
    const uint8_t b = color & 0xFF;

    g_strip->setPixelColor(idx, r, g, b);
}

void led_drv_fill(uint32_t color)
{
    if (!g_inited || !g_strip) return;

    const uint8_t r = (color >> 16) & 0xFF;
    const uint8_t g = (color >> 8) & 0xFF;
    const uint8_t b = color & 0xFF;

    for (uint8_t i = 0; i < g_pixel_count; ++i)
    {
        g_strip->setPixelColor(i, r, g, b);
    }
}

void led_drv_flush(void)
{
    if (!g_inited || !g_strip) return;
    g_strip->show();
}

uint32_t led_drv_color(uint8_t r, uint8_t g, uint8_t b)
{
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

#endif
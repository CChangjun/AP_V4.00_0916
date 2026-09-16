#pragma once

#define LED_DRV_USE_NEOPIXEL    0
#define LED_DRV_USE_RMT         1

#define DEBUG_LED_RMT           0
#if (LED_DRV_USE_NEOPIXEL + LED_DRV_USE_RMT) != 1
#error "Exactly one LED driver backend must be enabled"
#endif

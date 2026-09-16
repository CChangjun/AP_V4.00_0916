#pragma once

#include <Arduino.h>
#include "ap_context.h"

void Set_GPIO(ap_app_ctx_t *app);
void Mux_Sel_16ch(ap_app_ctx_t *app, uint8_t ch);
void Read_Rotary(ap_app_ctx_t *app);

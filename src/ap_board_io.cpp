#include "ap_board_io.h"
#include "ap_config.h"
#include "ap_wifi_runtime.h"
#include "peer_chk_led.h"

extern wifi_send_t wifi_send;

void Set_GPIO(ap_app_ctx_t *app)
{
    if (!app) return;

    // Input Setting
    pinMode(SW, INPUT);
    pinMode(MUXInput1, INPUT);

    // Output Setting
    pinMode(LED, OUTPUT);
    pinMode(NRESET, OUTPUT);
    pinMode(MUX_16EN1, OUTPUT);
    pinMode(MUX_SEL0, OUTPUT);
    pinMode(MUX_SEL1, OUTPUT);
    pinMode(MUX_SEL2, OUTPUT);
    pinMode(MUX_SEL3, OUTPUT);
    // 아직은 유지
    pinMode(PEER_CHK_LED, OUTPUT);

#if OSCILLOSCOPE == 1
    pinMode(TEST_POINT, OUTPUT);
#endif

#if DEBUG_SERIAL_SYSTEM == 1
    Serial.printf("\r\n");
    Serial.printf("[MSG]SYSTEM SETUP::MODULE::Set GPIO OK\r\n");
#endif
}
void Mux_Sel_16ch(ap_app_ctx_t *app, uint8_t ch)
{
    if (!app) return;

    switch (ch)
    {
    case 0:
        digitalWrite(MUX_SEL3, LOW);
        digitalWrite(MUX_SEL2, LOW);
        digitalWrite(MUX_SEL1, LOW);
        digitalWrite(MUX_SEL0, LOW);
        break;
    case 1:
        digitalWrite(MUX_SEL3, LOW);
        digitalWrite(MUX_SEL2, LOW);
        digitalWrite(MUX_SEL1, LOW);
        digitalWrite(MUX_SEL0, HIGH);
        break;
    case 2:
        digitalWrite(MUX_SEL3, LOW);
        digitalWrite(MUX_SEL2, LOW);
        digitalWrite(MUX_SEL1, HIGH);
        digitalWrite(MUX_SEL0, LOW);
        break;
    case 3:
        digitalWrite(MUX_SEL3, LOW);
        digitalWrite(MUX_SEL2, LOW);
        digitalWrite(MUX_SEL1, HIGH);
        digitalWrite(MUX_SEL0, HIGH);
        break;
    case 4:
        digitalWrite(MUX_SEL3, LOW);
        digitalWrite(MUX_SEL2, HIGH);
        digitalWrite(MUX_SEL1, LOW);
        digitalWrite(MUX_SEL0, LOW);
        break;
    case 5:
        digitalWrite(MUX_SEL3, LOW);
        digitalWrite(MUX_SEL2, HIGH);
        digitalWrite(MUX_SEL1, LOW);
        digitalWrite(MUX_SEL0, HIGH);
        break;
    case 6:
        digitalWrite(MUX_SEL3, LOW);
        digitalWrite(MUX_SEL2, HIGH);
        digitalWrite(MUX_SEL1, HIGH);
        digitalWrite(MUX_SEL0, LOW);
        break;
    case 7:
        digitalWrite(MUX_SEL3, LOW);
        digitalWrite(MUX_SEL2, HIGH);
        digitalWrite(MUX_SEL1, HIGH);
        digitalWrite(MUX_SEL0, HIGH);
        break;
    case 8:
        digitalWrite(MUX_SEL3, HIGH);
        digitalWrite(MUX_SEL2, LOW);
        digitalWrite(MUX_SEL1, LOW);
        digitalWrite(MUX_SEL0, LOW);
        break;
    case 9:
        digitalWrite(MUX_SEL3, HIGH);
        digitalWrite(MUX_SEL2, LOW);
        digitalWrite(MUX_SEL1, LOW);
        digitalWrite(MUX_SEL0, HIGH);
        break;
    case 10:
        digitalWrite(MUX_SEL3, HIGH);
        digitalWrite(MUX_SEL2, LOW);
        digitalWrite(MUX_SEL1, HIGH);
        digitalWrite(MUX_SEL0, LOW);
        break;
    case 11:
        digitalWrite(MUX_SEL3, HIGH);
        digitalWrite(MUX_SEL2, LOW);
        digitalWrite(MUX_SEL1, HIGH);
        digitalWrite(MUX_SEL0, HIGH);
        break;
    case 12:
        digitalWrite(MUX_SEL3, HIGH);
        digitalWrite(MUX_SEL2, HIGH);
        digitalWrite(MUX_SEL1, LOW);
        digitalWrite(MUX_SEL0, LOW);
        break;
    case 13:
        digitalWrite(MUX_SEL3, HIGH);
        digitalWrite(MUX_SEL2, HIGH);
        digitalWrite(MUX_SEL1, LOW);
        digitalWrite(MUX_SEL0, HIGH);
        break;
    case 14:
        digitalWrite(MUX_SEL3, HIGH);
        digitalWrite(MUX_SEL2, HIGH);
        digitalWrite(MUX_SEL1, HIGH);
        digitalWrite(MUX_SEL0, LOW);
        break;
    case 15:
        digitalWrite(MUX_SEL3, HIGH);
        digitalWrite(MUX_SEL2, HIGH);
        digitalWrite(MUX_SEL1, HIGH);
        digitalWrite(MUX_SEL0, HIGH);
        break;
    default:
        break;
    }

    delay(10);
}

void Read_Rotary(ap_app_ctx_t *app)
{
    if (!app) return;

    uint8_t i = 0;
    app->board.iAddr1 = 0;
    app->board.iAddr2 = 0;

    uint8_t readR3 = 0, readR4 = 0;

    digitalWrite(MUX_16EN1, HIGH);
    delay(100);

    Mux_Sel_16ch(app, i);
    if (!digitalRead(MUXInput1)) app->board.iAddr2 += 4;
    i++;

    Mux_Sel_16ch(app, i);
    if (!digitalRead(MUXInput1)) app->board.iAddr2 += 1;
    i++;

    Mux_Sel_16ch(app, i);
    if (!digitalRead(MUXInput1)) app->board.iAddr2 += 8;
    i++;

    Mux_Sel_16ch(app, i);
    if (!digitalRead(MUXInput1)) app->board.iAddr2 += 2;
    i++;

    Mux_Sel_16ch(app, i);
    if (!digitalRead(MUXInput1)) app->board.iAddr1 += 4;
    i++;

    Mux_Sel_16ch(app, i);
    if (!digitalRead(MUXInput1)) app->board.iAddr1 += 1;
    i++;

    Mux_Sel_16ch(app, i);
    if (!digitalRead(MUXInput1)) app->board.iAddr1 += 8;
    i++;

    Mux_Sel_16ch(app, i);
    if (!digitalRead(MUXInput1)) app->board.iAddr1 += 2;
    i++;

    Mux_Sel_16ch(app, i);
    if (!digitalRead(MUXInput1)) readR4 += 4;
    i++;

    Mux_Sel_16ch(app, i);
    if (!digitalRead(MUXInput1)) readR4 += 1;
    i++;

    Mux_Sel_16ch(app, i);
    if (!digitalRead(MUXInput1)) readR4 += 8;
    i++;

    Mux_Sel_16ch(app, i);
    if (!digitalRead(MUXInput1)) readR4 += 2;
    i++;

    Mux_Sel_16ch(app, i);
    if (!digitalRead(MUXInput1)) readR3 += 4;
    i++;

    Mux_Sel_16ch(app, i);
    if (!digitalRead(MUXInput1)) readR3 += 1;
    i++;

    Mux_Sel_16ch(app, i);
    if (!digitalRead(MUXInput1)) readR3 += 8;
    i++;

    Mux_Sel_16ch(app, i);
    if (!digitalRead(MUXInput1)) readR3 += 2;

    digitalWrite(MUX_16EN1, LOW);

    if ((readR3 > 14) || (readR3 == 0)) readR3 = 1;
    if (readR4 > 15) readR4 = 1;

    wifi_send.rf_set_channel = readR3; //!
    wifi_send.rf_set_group   = readR4; //! g_ap.wifi로 바꿔야 함. 예정. 
    wifi_send.board_version  = AP_VER_10BIT;

    app->board.iAddr = (app->board.iAddr2 * 16) + app->board.iAddr1;

#if DEBUG_SERIAL_SYSTEM == 1
    Serial.printf("[MSG]SYSTEM  INIT::VERSION : %.2f, Address : %d, WIFI Group : %d,WIFI Channel : %d\r\n",
                (float)wifi_send.board_version / 100.0,
                app->board.iAddr,
                wifi_send.rf_set_group,
                wifi_send.rf_set_channel);
#endif
}


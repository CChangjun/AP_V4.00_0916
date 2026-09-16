
#include "common.h"

void output_toggle( uint8_t PIN )
{
    if( digitalRead(PIN) )          digitalWrite(PIN, 0);
    else if( !digitalRead(PIN) )    digitalWrite(PIN, 1);

}



uint16_t crc16_modbus(uint16_t init_crc, uint8_t* dat, uint16_t len)
{
    uint8_t crc[2];
    uint16_t tmp;
    crc[0] = init_crc >> 8;
    crc[1] = init_crc & 0xFF;
    for (uint16_t i = 0; i < len; i++) {
        tmp = crc_16_table[crc[0] ^ dat[i]];
        crc[0] = (tmp & 0xFF) ^ crc[1];
        crc[1] = tmp >> 8;
    }
    return crc[0] | ((uint16_t)crc[1] << 8);
}

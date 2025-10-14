#line 1 "C:\\개인폴더\\FirmWare\\무선화FW\\TEST\\revisionBoard\\AP_ver3.31_PEERLED추가TEST\\ApToEthercat_V3.31_20251014\\AP_SLAVE.h"
#ifndef CUSTOM_PDO_NAME_H
#define CUSTOM_PDO_NAME_H

//-------------------------------------------------------------------//
//                                                                   //
//     This file has been created by the Easy Configurator tool      //
//                                                                   //
//     Easy Configurator project AP_SLAVE.prj
//                                                                   //
//-------------------------------------------------------------------//


#define CUST_BYTE_NUM_OUT	50
#define CUST_BYTE_NUM_IN	50
#define TOT_BYTE_NUM_ROUND_OUT	52
#define TOT_BYTE_NUM_ROUND_IN	52


typedef union												//---- output buffer ----
{
	uint8_t  Byte [TOT_BYTE_NUM_ROUND_OUT];
	struct
	{
		uint16_t    pairing_bit;	// Pairing Bit
		uint16_t    data1;			// IO 0 ~
		uint16_t    data2;
		uint16_t    data3;
		uint16_t    data4;
		uint16_t    data5;
		uint16_t    data6;
		uint16_t    data7;
		uint16_t    data8;
		uint16_t    data9;
		uint16_t    data10;
		uint16_t    data11;			
		uint16_t    data12;			// ~ IO 12
		uint16_t    data13;			// Serial IO 0 ~
		uint16_t    data14;
		uint16_t    data15;
		uint16_t    data16;			// ~ Serial IO 3
		uint16_t    com1;			// Serial Data 0 ~
		uint16_t    com2;
		uint16_t    com3;
		uint16_t    com4;
		uint16_t    com5;
		uint16_t    com6;
		uint16_t    com7;
		uint16_t    com8;			// ~ Serial Data 7
	}Cust;
} PROCBUFFER_OUT;


typedef union												//---- input buffer ----
{
	uint8_t  Byte [TOT_BYTE_NUM_ROUND_IN];
	struct
	{
		uint16_t    pairing_bit;
		uint16_t    data1;
		uint16_t    data2;
		uint16_t    data3;
		uint16_t    data4;
		uint16_t    data5;
		uint16_t    data6;
		uint16_t    data7;
		uint16_t    data8;
		uint16_t    data9;
		uint16_t    data10;
		uint16_t    data11;
		uint16_t    data12;
		uint16_t    data13;
		uint16_t    data14;
		uint16_t    data15;
		uint16_t    data16;
		uint16_t    com1;
		uint16_t    com2;
		uint16_t    com3;
		uint16_t    com4;
		uint16_t    com5;
		uint16_t    com6;
		uint16_t    com7;
		uint16_t    com8;
	}Cust;
} PROCBUFFER_IN;



typedef enum _WIFI_STATE_MACHINE_T
{
	WIFI_STATE_READY = 0,
	WIFI_STATE_RETRY,
	WIFI_STATE_PAIRING_ADD,
	WIFI_STATE_PAIRING_DEL,
	WIFI_STATE_PAIRED,
	WIFI_STATE_SEND,
	WIFI_STATE_RECEIVE,
	WIFI_STATE_WAIT,
} WIFI_STATE_MACHINE;


typedef enum _WIFI_PEER_TYPE_T
{
	WIFI_PEER_IO		= 0,
	WIFI_PEER_SERIAL,

} WIFI_PEER_TYPE_T;



typedef enum _WIFI_PACKET_T
{
	WIFI_PACKET_GROUP 	= 0,
	WIFI_PACKET_CHANNEL,
	WIFI_PACKET_COMMAND,
	WIFI_PACKET_TYPE,
	WIFI_PACKET_ADDRESS,
	WIFI_PACKET_LENGTH,
	WIFI_PACKET_DATA,

	WIFI_PACKET_MAX = 50,
} WIFI_PACKET_T;



#endif
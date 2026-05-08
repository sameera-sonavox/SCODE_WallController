#ifndef CAN_CONTROLLER_H
#define CAN_CONTROLLER_H

#include "../Lib/CAN/NXP_CAN_ProjDef.h"
#include "../Lib/CAN/NXP_CAN_API.h"

#define BOOTLOADER_MSG_QUEUE_MAX_MESSAGEs               16
#define BOOTLOADER_THREAD_STACK_SIZE_BYTEs              1024
#define BOOTLOADER_THREAD_PRIORITY                      5
#define BOOTLOADER_DATA_PACKET_LENGTH                   16

extern void vInit_CANController( void );
extern void vSend_CANMessage( sT_CAN_TXMsg_t stTMsg );

#endif
#ifndef BOOTLOADER_CTRL_H
#define BOOTLOADER_CTRL_H

#include <zephyr/storage/flash_map.h>
#include "../CAN_Controller/CAN_Controller.h"
#include "Bootloader_TypeDef.h"

#define FLASH_AREA_FW_IMAGE_STORE_ID                PARTITION_ID(slot1_partition)
#define FW_IMG_WRITE_BYTE_LENGTH                    16
#define FW_IMG_PACKET_DATA_LENGTH                   50

#define TIMEOUT_FW_UPREQ_TO_FW_UPDATE_ms            500
#define TIMEOUT_FW_UPDATE_NextPacket_ms             300
#define TIMEOUT_REBOOT_CONFIRMATION_ms              100

#define DEBUG_BOOTLOADER

//Do not change
#define FW_IMAGE_SECONDARY_SLOT_WRITE_OFFSET        0x2000

//************************ */

extern void vInit_BootloaderController( void );
extern void vUpdateBootloader( sT_Bootloader_CtrlMsg_t * pstTBootMsg );
extern bool bIsBootloader_Initialized( void );
extern bool bIsFW_ImageWrite_InProgress( void );
extern void vConfirm_MCUbootImage( void );

#endif
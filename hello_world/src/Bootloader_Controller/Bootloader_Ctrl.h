#ifndef BOOTLOADER_CTRL_H
#define BOOTLOADER_CTRL_H

#include <zephyr/storage/flash_map.h>
#include "../CAN_Controller/CAN_Controller.h"
#include "Bootloader_TypeDef.h"
#include "Bootloader_ProjDef.h"

//************************ */

extern void vInit_BootloaderController( void );
extern void vUpdateBootloader( sT_Bootloader_CtrlMsg_t * pstTBootMsg );
extern bool bIsBootloader_Initialized( void );
extern bool bIsFW_ImageWrite_InProgress( void );
extern void vConfirm_MCUbootImage( void );
extern void vHandle_HostAcknowledgements( sT_Bootloader_CtrlMsg_t * pstTBootMsg );

#endif
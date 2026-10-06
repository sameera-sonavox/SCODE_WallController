/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <zephyr/device.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/reboot.h>
#include "fsl_clock.h"
#include "fsl_port.h"
#include "CPULoad/NXP_CPU_LoadMon.h"
#include "CAN_Controller/CAN_Controller.h"
#include "WallController/WallController_Comm_Manager/WallController_Comm_Manager.h"
#include "WallController/WallController_StateMachine/Wallcontroller_StateMachine.h"
#include "Bootloader_Controller/Bootloader_Ctrl.h"
#include "PC_UART_API/PC_UART_API.h"
#include "PC_UART_API/PC_UART_API_ProjDef.h"
#include "PC_UART_API/PC_UART_API_ValidateTest.h"
#include "SPI/NXP_SPI_API.h"
#include "LVGL_Controller/LVGL_Display_Controller.h"
#include "ExtFlash_Controller/ExtFlash_Controller.h"
#include "ExtFlash_Controller/Flash_Memory_ValidateTest/Flash_Memory_ValidateTest_ProjDef.h"
#include "ExtFlash_Controller/LittleFsController/LittleFs_Controller.h"

bool bInit_System( void );
void vCheck_For_FileSystem( void );
static void vRun_System( void );

int main(void)
{
#if defined(DEBUG_TOGGLE_LPSPI0_SDO_AS_GPIO)
	vDebug_Toggle_LPSPI0_SDO_Pin();
#endif

	vConfirm_MCUbootImage();
	if(!bInit_System())
	{
		FHALT("SYstem Initialization Failed.");
		printf("System Restarting...\n\r");
		sys_reboot(SYS_REBOOT_COLD);

		CODE_UNREACHABLE;
	}

	while (1)
	{
		vRun_System();
		k_msleep(1);
	}
	
}

static void vRun_System( void )
{
	vRun_UI();
	vRun_WallCtrl_StateMachine();
}

bool bInit_System( void )
{
	if(!bInit_BootloaderController())
		return false;
	if(!bInit_WallController_Comm_Manager(NULL))
		return false;
	if(!bInit_CANController())
		return false;
	vInit_PC_UART_API();

	if(!bInit_ExtFlash_FsController())
		return false;
	vCheck_For_FileSystem();
	
	if(!bInit_LVGLDisplay())
	{
		return false;
	}

	return true;
}

void vCheck_For_FileSystem( void )
{
	printf("Getting File Details...\n\r\n\r");
	const sT_FileInfo_t *pstFiles = pstGetFileInfo();
	if(pstFiles != NULL)
	{
		printf("\n\r");
		for(uint8_t i = 0; i < MAX_FILE_COUNT; i++)
		{
			if(strlen(pstFiles[i].caFilePath) == 0)
				continue;
			printf("File[%d]: %s @Size: %d\n\r", i, pstFiles[i].caFilePath, pstFiles[i].uiFileSize);
		}
		printf("\n\r");
	}
}

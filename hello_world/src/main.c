/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <zephyr/device.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/drivers/gpio.h>
#include "fsl_clock.h"
#include "fsl_port.h"
#include "CPULoad/NXP_CPU_LoadMon.h"
#include "PWM/NXP_PWM_API.h"
#include "CAN_Controller/CAN_Controller.h"
#include "Bootloader_Controller/Bootloader_Ctrl.h"
#include "UART_CAN_Bridge/UART_CAN_Bridge.h"
#include "DAC/NXP_DAC_API.h"
#include "ADC_Controller/ADC_Controller.h"
#include "SPI_Controller/SPI_Controller_Master.h"
#include "SPI_Controller/SPI_Controller_Slave.h"
#include "SPI/NXP_SPI_API.h"
#include "LVGL_Controller/LVGL_Display_Controller.h"
#include "ExtFlash_Controller/ExtFlash_Controller.h"

void vInit_System( void );

int main(void)
{
#if defined(DEBUG_TOGGLE_LPSPI0_SDO_AS_GPIO)
	vDebug_Toggle_LPSPI0_SDO_Pin();
#endif

	vConfirm_MCUbootImage();
	vInit_System();

	while (1)
	{
		vRun_UI();
		k_msleep(1);
	}
	
}

void vInit_System( void )
{
	vInit_BootloaderController();
	vInit_CANController();
 	vInit_UART_CAN_Bridge();

	if(!bInit_ExtFlash())
	{
		FHALT("Failed to initialize External Flash");
	}

	vInit_LVGLDisplay();
}

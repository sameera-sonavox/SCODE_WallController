/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include "GPIO/Amp_GPIO.h"
#include "Lib/PWM/NXP_PWM_API.h"
#include "CAN_Controller/CAN_Controller.h"
#include "Bootloader_Controller/Bootloader_Ctrl.h"

void vInit_Amp( void );

int main(void)
{
	vInit_Amp();

	uint8_t uiaData_Mgmt[4] = {0x34, 0x22, 0x55, 0xEE};
	uint8_t uiaData_Boot[4] = {0x55, 0x66, 0x77, 0x88};

	sT_CAN_TXMsg_t stMsg = {
		.uiID = CAN_NODE_0_ID,
		.uiLen = 4,
		.puiData = uiaData_Mgmt
	};
	sT_CAN_TXMsg_t stMsg_Boot = {
		.uiID = CAN_RX_BOOTLOADER_ID,
		.uiLen = 4,
		.puiData = uiaData_Boot
	};

	while (1)
	{
		k_msleep(50);
/* 		k_msleep(50);
		vSend_CANMessage(stMsg);
		k_msleep(50);
		vSend_CANMessage(stMsg_Boot); */
	}
	
}

void vInit_Amp( void )
{
	vInit_BootloaderController();
	vInit_Amp_GPIO();
	vInit_CANController();

	SET_AMP_SD();
	//bUpdate_PWM_Duty(eCTPWM1, 50);
}


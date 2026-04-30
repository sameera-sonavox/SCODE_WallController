/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include "GPIO/Amp_GPIO.h"
#include "Lib/PWM/NXP_PWM_API.h"
#include "CAN_Controller/CAN_Controller.h"

void vInit_Amp( void );

int main(void)
{
	vInit_Amp();

	while (1)
	{
 		k_msleep(1000);
		bUpdate_PWM_Frequency_Hz(eCTPWM1, 1000);
		k_msleep(2000);
		bUpdate_PWM_Frequency_Hz(eCTPWM1, 2000);
		k_msleep(4000);
		bUpdate_PWM_Frequency_Hz(eCTPWM1, 3000);
		k_msleep(6000);
		bUpdate_PWM_Frequency_Hz(eCTPWM1, 4000);		
/* 		k_msleep(2000);
		bUpdate_PWM_Duty(eCTPWM1, 10);
		k_msleep(5000);
		bStop_PWM(eCTPWM1);
		k_msleep(5000);
		bUpdate_PWM_Duty(eCTPWM1, 60);
		k_msleep(5000);
		bUpdate_PWM_Duty(eCTPWM1, 0); */
	}
	
}

void vInit_Amp( void )
{
	vInit_Amp_GPIO();
	vInit_PWM();
	vInit_CANController();

	SET_AMP_SD();
	bUpdate_PWM_Duty(eCTPWM1, 50);
}


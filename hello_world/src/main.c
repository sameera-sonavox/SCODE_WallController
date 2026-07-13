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
#include "Lib/CPULoad/NXP_CPU_LoadMon.h"
#include "GPIO/Amp_GPIO.h"
#include "Lib/PWM/NXP_PWM_API.h"
#include "CAN_Controller/CAN_Controller.h"
#include "Bootloader_Controller/Bootloader_Ctrl.h"
#include "UART_CAN_Bridge/UART_CAN_Bridge.h"
#include "Lib/DAC/NXP_DAC_API.h"
#include "ADC_Controller/ADC_Controller.h"
#include "SPI_Controller/SPI_Controller_Master.h"
#include "SPI_Controller/SPI_Controller_Slave.h"

/* #define USE_SPI_MASTER */
/* #define DEBUG_TOGGLE_LPSPI0_SDO_AS_GPIO */

uint8_t uiaCMDData[8] = {0x34, 0x22, 0x55, 0xEE, 0x11, 0x22, 0x33, 0x44};

void vInit_Amp( void );
void vConfigure_DAC( void );


int main(void)
{
#if defined(DEBUG_TOGGLE_LPSPI0_SDO_AS_GPIO)
	vDebug_Toggle_LPSPI0_SDO_Pin();
#endif

	vInit_Amp();
	vConfirm_MCUbootImage();
	printk("FW Img Booting over UART....\n\r");
/* 	uint8_t uiaData_Mgmt[4] = {0x34, 0x22, 0x55, 0xEE};

	sT_CAN_TXMsg_t stMsg = {
		.uiID = CAN_NODE_0_ID,
		.uiLen = 4,
		.puiData = uiaData_Mgmt
	}; */

	while (1)
	{
		#ifdef USE_SPI_MASTER
			bSPI_SendData(eSPI_Slave_0, uiaCMDData, 8);
			k_msleep(2);
			bSPI_ReceiveData(eSPI_Slave_0, NULL, 0, 8);
			k_msleep(50);
		#endif
	}
	
}

void vInit_Amp( void )
{
	vInit_BootloaderController();
	vInit_Amp_GPIO();
	vInit_CANController();
 	vInit_UART_CAN_Bridge();
	vConfigure_DAC();
	//vInitialize_ADCModule();

	#ifdef USE_SPI_MASTER
		vConfigure_SPI();
	#else
		vConfigure_SPISLave();
	#endif

	SET_AMP_SD();
	//bUpdate_PWM_Duty(eCTPWM1, 50);
}

void vConfigure_DAC( void )
{
	sT_DAC_Config_t stDACConfig = {0};
	stDACConfig.eRefVoltSrc = eDAC_RefVoltSrc_VREF_VDD_ANA; // Adjust as needed based on actual hardware configuration
	stDACConfig.stOutputBuffConfig.bEnableOutputBuffer = true;
	stDACConfig.stOutputBuffConfig.eOutputBuffLowPowerMode = eDAC_OutputBuff_Higher_LowPowerMode;
	stDACConfig.stOutputConfig.eWaveFormType = eDAC_WaveForm_Sine;
	stDACConfig.stOutputConfig.uOutputConfig.stWaveFormOutput.eFIFOWorkMode = eMode_FIFO;
	stDACConfig.stOutputConfig.uOutputConfig.stWaveFormOutput.stTHWTrigSrc.eTrigSrcGroup = eTrigSrcGroup_CTIMER;
	stDACConfig.stOutputConfig.uOutputConfig.stWaveFormOutput.stTHWTrigSrc.uTrigSrc.eCTimerTrigSrc = eTrigSrc_CTIMER0_MAT0;
	stDACConfig.stOutputConfig.uOutputConfig.stWaveFormOutput.uiPeakVoltage_mV = 1000;
	stDACConfig.stOutputConfig.uOutputConfig.stWaveFormOutput.uiDCOffset_mV = 1010;
	stDACConfig.stOutputConfig.uOutputConfig.stWaveFormOutput.uiFrequencyHz = 1000;
	stDACConfig.stOutputConfig.uOutputConfig.stWaveFormOutput.pvErrorCallback = NULL;

	vDAC_Init(&stDACConfig);
	//bDAC_UpdateOutputValue(2500U);
	// Configure other fields of stDACConfig as needed
}

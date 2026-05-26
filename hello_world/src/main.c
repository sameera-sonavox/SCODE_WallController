/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <zephyr/dfu/mcuboot.h>
#include "Lib/CPULoad/NXP_CPU_LoadMon.h"
#include "GPIO/Amp_GPIO.h"
#include "Lib/PWM/NXP_PWM_API.h"
#include "CAN_Controller/CAN_Controller.h"
#include "Bootloader_Controller/Bootloader_Ctrl.h"
#include "UART_CAN_Bridge/UART_CAN_Bridge.h"
#include "Lib/DAC/NXP_DAC_API.h"

void vInit_Amp( void );
void vConfigure_DAC( void );

int main(void)
{
	uint32_t uiCount = 0, turn = 0;
	uint8_t uiIndex = 0;
	bool bispaused = false;

	vInit_Amp();
	vConfirm_MCUbootImage();
	printk("Missig FW Img Booting over UART....\n\r");
/* 	uint8_t uiaData_Mgmt[4] = {0x34, 0x22, 0x55, 0xEE};
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
	}; */

	while (1)
	{
		k_msleep(50);
		if(uiCount < 25)
			uiCount++;
		if(uiCount >= 25)
		{
			switch (uiIndex)
			{
				case 0:
					vPrint_CPU_Load("before update");
					vUpdate_WaveForm_Frequency(2000);
					vPrint_CPU_Load("after update");
					uiIndex++;
					break;
				case 1:
					vGet_Execution_Time_uS("DAC BENCH", eGetTime_T0, eTime_mS);
					vUpdate_WaveForm_Frequency(3000);
					vGet_Execution_Time_uS("DAC BENCH", eGetTime_T1, eTime_mS);
					uiIndex++;
					break;
				case 2:
					vUpdate_WaveForm_Frequency(4000);
					uiIndex++;
					break;
				case 3:
					vUpdate_WaveForm_Frequency(5000);
					uiIndex++;
					break;
				case 4:
					vUpdate_WaveForm_Frequency(6000);
					uiIndex++;
					break;
				case 5:
					vUpdate_WaveForm_Frequency(7000);
					uiIndex++;
					break;
				case 6:
					vUpdate_WaveForm_Frequency(8000);
					uiIndex++;
					break;
				case 7:
					vUpdate_WaveForm_Frequency(7000);
					uiIndex++;
					break;
				case 8:
					vUpdate_WaveForm_Frequency(6000);
					uiIndex++;
					break;
				case 9:
					vUpdate_WaveForm_Frequency(5000);
					uiIndex++;
					break;
				case 10:
					vUpdate_WaveForm_Frequency(4000);
					uiIndex++;
					break;
				case 11:
					vUpdate_WaveForm_Frequency(3000);
					uiIndex++;
					break;
				case 12:
					vUpdate_WaveForm_Frequency(2000);
					uiIndex++;
					break;
				case 13:
					if(!bispaused)
					{
						vUpdate_WaveForm_Frequency(1000);
						vStop_WaveGen(eDAC_DefaultOut_Custom, 1000);						
						printk("Waveform Generation Paused.\n");
						turn = 0;
						bispaused = true;
						break;
					}
					if(bispaused && turn < 20)
					{
						turn++;
						printk("Waveform Generation Paused @Restart Counting = %d.\n", turn);
						break;
					}
					bispaused = false;
					vReStart_WaveGen();
					printk("Waveform Generation Resumed.\n");
					uiIndex = 0;
					break;					
				default:
					break;
			}
			uiCount = 0;
		}
	}
	
}

void vInit_Amp( void )
{
	//vInit_BootloaderController();
	vInit_Amp_GPIO();
	//vInit_CANController();
	//vInit_UART_CAN_Bridge();
	vConfigure_DAC();

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
	stDACConfig.stOutputConfig.uOutputConfig.stWaveFormOutput.stTHWTrigSrc.eTrigSrcGroup = eDAC_TrigSrcGroup_CTIMER;
	stDACConfig.stOutputConfig.uOutputConfig.stWaveFormOutput.stTHWTrigSrc.uTrigSrc.eCTimerTrigSrc = eDAC_TrigSrc_CTIMER0_MAT0;
	stDACConfig.stOutputConfig.uOutputConfig.stWaveFormOutput.uiPeakVoltage_mV = 1000;
	stDACConfig.stOutputConfig.uOutputConfig.stWaveFormOutput.uiDCOffset_mV = 1010;
	stDACConfig.stOutputConfig.uOutputConfig.stWaveFormOutput.uiFrequencyHz = 1000;
	stDACConfig.stOutputConfig.uOutputConfig.stWaveFormOutput.pvErrorCallback = NULL;

	vDAC_Init(&stDACConfig);

	//bDAC_UpdateOutputValue(2500U);
	// Configure other fields of stDACConfig as needed
}

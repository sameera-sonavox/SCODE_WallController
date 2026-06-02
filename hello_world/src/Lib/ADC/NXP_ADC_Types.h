#ifndef NXP_ADC_TYPES_H
#define NXP_ADC_TYPES_H

#include <stdint.h>
#include <stdbool.h>

typedef enum
{
    eADC_Ch_0 = 0,
    eADC_Ch_1,
    eADC_Ch_2,
    eADC_Ch_3,
    eADC_Ch_4,
    eADC_Ch_5,
    eADC_Ch_6,
    eADC_Ch_7,
    eADC_Ch_8,
    eADC_Ch_9,
    eADC_Ch_10,
    eADC_Ch_11,
    eADC_Ch_12,
    eADC_Ch_13,
    eADC_Ch_14,
    eADC_Ch_15,
    eADC_Ch_16,
    eADC_Ch_17,
    eADC_Ch_18,
    eADC_Ch_19,
    eADC_Ch_20,
    eADC_Ch_21,
    eADC_Ch_22,
    eADC_Ch_23,
    eADC_Ch_24,
    eADC_Ch_25,
    eADC_Ch_26,
    eADC_Ch_27,
    eADC_Ch_28,
    eADC_Ch_29,
    eADC_Ch_30,
    eADC_Ch_31,
    eNUMBER_OF_ADC_CHANNELs
} eADC_Channel_t;

typedef enum
{
    eADC_ADC0 = 0,
    eADC_ADC1,
    eNUMBER_OF_ADC_MODULEs
} eADC_Module_t;

typedef enum
{
    eADC_CH_Conn_Pin = 0,
    eADC_CH_Conn_Reserved,
    eADC_CH_Conn_OpAmp0_Int,
    eADC_CH_Conn_VSSA,
    eADC_CH_Conn_Temp,
    eADC_CH_Conn_PMCBG,
    eADC_CH_Conn_OpAmp0_BS,
    eADC_CH_Conn_VDD_4,
    eADC_CH_Conn_ATX0,
    eADC_CH_Conn_ATX1,
    eNUMBER_OF_ADC_CONN_ASSIGNMENTs
} eADC_AssignmentType_t;

typedef enum
{
    eADC_VrefSrc_VDD_ANA = 0,    
    eADC_VrefSrc_Ext,      /* DACREF_2 reference source. */
    eADC_VrefSrc_Int,     /* DACREF_1 reference source. */
    eNUMBER_OF_ADC_REF_VOLT_SRCs
} eADC_RefVoltSrc_t;

typedef enum
{
    eADC_TrigSrc_None = 0x00,
    eADC_TrigSrc_ARM_TXEV = 0x01,
    eADC_TrigSrc_AOI0_OUT0 = 0x02,
    eADC_TrigSrc_AOI0_OUT1 = 0x03,
    eADC_TrigSrc_AOI0_OUT2 = 0x04,
    eADC_TrigSrc_AOI0_OUT3 = 0x05,
    eADC_TrigSrc_CMP0_OUT = 0x06,
    eADC_TrigSrc_CMP1_OUT = 0x07,
    eADC_TrigSrc_CTimer0_MAT0 = 0x09,
    eADC_TrigSrc_CTimer0_MAT1 = 0x0A,
    eADC_TrigSrc_CTimer1_MAT0 = 0x0B,
    eADC_TrigSrc_CTimer1_MAT1 = 0x0C,
    eADC_TrigSrc_CTimer2_MAT0 = 0x0D,
    eADC_TrigSrc_CTimer2_MAT1 = 0x0E,
    eADC_TrigSrc_LPTMR0 = 0x0F,
    eADC_TrigSrc_QDC0_POS_MATCH0 = 0x11,
    eADC_TrigSrc_PWM0_SM0_OUT_TRIG0 = 0x12,
    eADC_TrigSrc_PWM0_SM0_OUT_TRIG1 = 0x13,
    eADC_TrigSrc_PWM0_SM1_OUT_TRIG0 = 0x14,
    eADC_TrigSrc_PWM0_SM1_OUT_TRIG1 = 0x15,
    eADC_TrigSrc_PWM0_SM2_OUT_TRIG0 = 0x16,
    eADC_TrigSrc_PWM0_SM2_OUT_TRIG1 = 0x17,
    eADC_TrigSrc_GPIO0_PIN_EVENT_TRIG0 = 0x1A,
    eADC_TrigSrc_GPIO1_PIN_EVENT_TRIG0 = 0x1B,
    eADC_TrigSrc_GPIO2_PIN_EVENT_TRIG0 = 0x1C,
    eADC_TrigSrc_GPIO3_PIN_EVENT_TRIG0 = 0x1D,
    eADC_TrigSrc_GPIO4_PIN_EVENT_TRIG0 = 0x1E,
    eADC_TrigSrc_WUU = 0x1F,
    eADC_TrigSrc_AOI1_OUT0 = 0x21,
    eADC_TrigSrc_AOI1_OUT1 = 0x22,
    eADC_TrigSrc_AOI1_OUT2 = 0x23,
    eADC_TrigSrc_AOI1_OUT3 = 0x24,
    eADC_TrigSrc_OtherADC_TCOMP0 = 0x25,
    eADC_TrigSrc_OtherADC_TCOMP1 = 0x26,
    eADC_TrigSrc_OtherADC_TCOMP2 = 0x27,
    eADC_TrigSrc_OtherADC_TCOMP3 = 0x28,
    eADC_TrigSrc_CTimer3_MAT0 = 0x29,
    eADC_TrigSrc_CTimer3_MAT1 = 0x2A,
    eADC_TrigSrc_CTimer4_MAT0 = 0x2B,
    eADC_TrigSrc_CTimer4_MAT1 = 0x2C,
    eADC_TrigSrc_FlexIO_CH0 = 0x2D,
    eADC_TrigSrc_FlexIO_CH1 = 0x2E,
    eADC_TrigSrc_FlexIO_CH2 = 0x2F,
    eADC_TrigSrc_FlexIO_CH3 = 0x30,
    eADC_TrigSrc_QDC1_POS_MATCH0 = 0x31,
    eADC_TrigSrc_PWM1_SM0_MUX_TRIG0 = 0x32,
    eADC_TrigSrc_PWM1_SM0_MUX_TRIG1 = 0x33,
    eADC_TrigSrc_PWM1_SM1_MUX_TRIG0 = 0x34,
    eADC_TrigSrc_PWM1_SM1_MUX_TRIG1 = 0x35,
    eADC_TrigSrc_PWM1_SM2_MUX_TRIG0 = 0x36,
    eADC_TrigSrc_PWM1_SM2_MUX_TRIG1 = 0x37,
    eNUMBER_OF_ADC_TRIG_SOURCEs = 0x3A
} eADC_TrigSource_t;

typedef enum
{
    eADC_CMD_None = 0,//No command
    eADC_CMD_1,
    eADC_CMD_2,
    eADC_CMD_3,
    eADC_CMD_4,
    eADC_CMD_5,
    eADC_CMD_6,
    eADC_CMD_7,
    eADC_CMD_8,
    eADC_CMD_9,
    eADC_CMD_10,
    eADC_CMD_11,
    eADC_CMD_12,
    eADC_CMD_13,
    eADC_CMD_14,
    eADC_CMD_15,
    eNUMBER_OF_ADC_COMMANDs
} eADC_Command_t;

typedef enum
{
    eTrig_Slot_0 = 0,
    eTrig_Slot_1,
    eTrig_Slot_2,
    eTrig_Slot_3,
    eNUMBER_OF_ADC_TRIG_SLOTs
}eADC_TrigSlot_t;

typedef enum
{
    eADC_Resolution_12Bit = 0,
    eADC_Resolution_16Bit,
    eNUMBER_OF_ADC_RESOLUTIONs
}eADC_ResolutionType_t;

typedef enum
{
    eADC_Mode_SingleEnded = 0,
    eADC_Mode_Differential,
    eNUMBER_OF_ADC_MODEs
} eADC_ModeType_t;

typedef enum
{
   eNotification_None = 0,//Polling
   eNotification_Interrupt,
   eNotification_DMA,
    eNUMBER_OF_ADC_NOTIFICATION_TYPEs 
}eADC_NotificationType_t;

typedef enum
{
    eADC_AVG_ConvCount_0 = 0,//No averaging
    eADC_AVG_ConvCount_2,
    eADC_AVG_ConvCount_4,
    eADC_AVG_ConvCount_8,
    eADC_AVG_ConvCount_16,
    eADC_AVG_ConvCount_32,
    eADC_AVG_ConvCount_64,
    eADC_AVG_ConvCount_128,
    eADC_AVG_ConvCount_256,
    eADC_AVG_ConvCount_512,
    eADC_AVG_ConvCount_1024,
    eNUMBER_OF_ADC_AVG_CONVCOUNTs
} eADC_AvgConvCount_t;

typedef enum
{
    eADC_SampleTime_3_5_ADCKCycles = 0,
    eADC_SampleTime_5_5_ADCKCycles,// 5.5 ADCK cycles total sample time.
    eADC_SampleTime_7_5_ADCKCycles,// 7.5 ADCK cycles total sample time.
    eADC_SampleTime_11_5_ADCKCycles,// 11.5 ADCK cycles total sample time.
    eADC_SampleTime_19_5_ADCKCycles,// 19.5 ADCK cycles total sample time.
    eADC_SampleTime_35_5_ADCKCycles,// 35.5 ADCK cycles total sample time.
    eADC_SampleTime_67_5_ADCKCycles,// 67.5 ADCK cycles total sample time.
    eADC_SampleTime_131_5_ADCKCycles,// 131.5 ADCK cycles total sample time.
    eNUMBER_OF_ADC_SAMPLE_TIMEs
} eADC_SampleTime_t;

typedef enum
{
    eADC_CVReg_None = 0,//No compare value register
    eADC_CVReg_1,//Compare value register 1
    eADC_CVReg_2,
    eADC_CVReg_3,
    eADC_CVReg_4,
    eADC_CVReg_5,
    eADC_CVReg_6,
    eADC_CVReg_7,
    eNUMBER_OF_ADC_CV_REGs
} eADC_CVReg_t;

typedef struct sT_ADC_CommandConfig_t
{
    eADC_Command_t eCommandId;
    eADC_Channel_t eChannel;
    eADC_ResolutionType_t eResolution;
    eADC_ModeType_t eMode;
    eADC_AvgConvCount_t eAvgSampleCount;
    eADC_SampleTime_t eSampleTime;
    eADC_CVReg_t eCompareValueReg;
    struct sT_ADC_CommandConfig_t *pstNextCommandConfig;
} sT_ADC_CommandConfig_t;

typedef struct
{
    bool bIsTrigSlotEnabled;
    eADC_TrigSlot_t eTrigSlot;
    eADC_TrigSource_t eTrigSrc;
    sT_ADC_CommandConfig_t stTHeadCmdConfig;
} sT_ADC_TrigConfig_t;

typedef union
{
    struct{
        unsigned reserved : 6;
        unsigned CFG2_Tune_0 : 1;
        unsigned CFG2_Tune_1 : 1;
    }bits;
    uint8_t uiValue;
} CFG2_ADCConversionCycleTune_t;

typedef struct
{
    bool bIsHighSpeed_Enabled;
    bool bIsHighSpeedExtra_Enabled;
    CFG2_ADCConversionCycleTune_t stConvCycleTune;
} sT_ADC_HighSpeedConfig_t;

typedef struct
{
    bool bIsConfigOk;
    eADC_Module_t eADCModule;
    eADC_RefVoltSrc_t eRefSrc;
    eADC_NotificationType_t eNotificationType;
    uint8_t uiWaterMarkLevel;
    sT_ADC_HighSpeedConfig_t stHighSpeedConfig;
    sT_ADC_TrigConfig_t staTrigConfig[eNUMBER_OF_ADC_TRIG_SLOTs];
} sT_ADC_ModuleConfig_t;

#endif

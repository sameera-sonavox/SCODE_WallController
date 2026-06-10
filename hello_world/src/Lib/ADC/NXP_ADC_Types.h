#ifndef NXP_ADC_TYPES_H
#define NXP_ADC_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include "fsl_lpadc.h"

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
    eADC_CH_Conn_Pin_VREFI,
    eADC_CH_Conn_Reserved,
    eADC_CH_Conn_OpAmp0_Int,
    eADC_CH_Conn_VSSA,
    eADC_CH_Conn_Temp,
    eADC_CH_Conn_PMCBG,
    eADC_CH_Conn_OpAmp0_BS,
    eADC_CH_Conn_VDD_4,
    eADC_CH_Conn_Pin_VDD_P3,
    eADC_CH_Conn_ADC1_A20_A22_P3,
    eADC_CH_Conn_ATX0,
    eADC_CH_Conn_ATX1,
    eADC_CH_Conn_ATX2,
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
    eNUMBER_OF_ADC_TRIG_SOURCEs
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
   eNotification_Polling = 0,//Polling
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
    eADC_SampleTime_3_ADCKCycles = 0,
    eADC_SampleTime_5_ADCKCycles,// 5.5 ADCK cycles total sample time.
    eADC_SampleTime_7_ADCKCycles,// 7.5 ADCK cycles total sample time.
    eADC_SampleTime_11_ADCKCycles,// 11.5 ADCK cycles total sample time.
    eADC_SampleTime_19_ADCKCycles,// 19.5 ADCK cycles total sample time.
    eADC_SampleTime_35_ADCKCycles,// 35.5 ADCK cycles total sample time.
    eADC_SampleTime_67_ADCKCycles,// 67.5 ADCK cycles total sample time.
    eADC_SampleTime_131_ADCKCycles,// 131.5 ADCK cycles total sample time.
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

typedef struct
{
    bool bIsLoopWithChIncrementEnabled;//LWI : This means the successive conversions is performed on consecutive channels based on loop count automatically by the hardware
    bool bIsNewTrig_Req_For_NextConv;//'1': enable 'WAIT_TRIG'. It means only one command in the chain is executed per one trigger and HW waits for 
                                     //next trigger before executing the next command in the sequence 
    uint8_t uiLoopCount;//Relate with 'bIsNewTrig_Req_For_NextConv'. If 'bIsNewTrig_Req_For_NextConv = 1': Starts from 'eChannel' and loops until 'eChannel + uiLoopCount' number of channels in one trigger
                        //If 'bIsNewTrig_Req_For_NextConv =0': Conversion is performed on 'eChannel', number of 'uiLoopCount' automatically at HW level    
    eADC_Command_t eCommandId;
    eADC_Channel_t eChannel;
    eADC_ResolutionType_t eResolution;
    eADC_AvgConvCount_t eHWAvgSampleCount;//Performs averaging at HW level automatically when this is set above 'eADC_AVG_ConvCount_0'. 'eADC_AVG_ConvCount_0' means no HW averaing
    eADC_SampleTime_t eSampleTime;//ADC will wait defined number of ADC clock cycles before the conversion. Default or Minimum is 3 ADCK cycles
    eADC_CVReg_t eCompareValueReg;
    uint32_t uiADCMax_ReleaseTime_ms;
    uint32_t uiADCMin_ReleaseTime_ms;
    uint16_t uiSWAvgSampleCount;
}sT_ADC_CMDData_t;

typedef struct sT_ADC_CommandConfig_t
{
    sT_ADC_CMDData_t stTCMDData;
    struct sT_ADC_CommandConfig_t *pstNextCommandConfig;
} sT_ADC_CommandConfig_t;

typedef enum
{
    eTrig_Prio_Lev_0 = 0,//Highest Priority
    eTrig_Prio_Lev_1,
    eTrig_Prio_Lev_2,
    eTrig_Prio_Lev_3,
    eNUMBER_OF_PRIORITY_LEVELs
} eADC_TrigPrio_t;

typedef enum
{
    eTrigSrc_None = 0,
    eTrigSrc_Software,
    eTrigSrc_Hardware,
    eNUMBER_OF_TRIGGER_TYPEs
} eADC_TrigSrcType_t;

typedef enum
{
    eADC_Val,
    eADC_Max,
    eADC_Min,
    eADC_Avg,
    eADC_RMS,
    eNUMBER_OF_ADC_VAL_TYPEs
} eADC_ValueType_t;

typedef enum
{
    eADC_Stat_Max,
    eADC_Stat_Min,
    eNUMBER_OF_ADC_STAT_TYPEs
} eADC_StatType_t;

typedef void (*ADC_TrigCompCallback_t)(eADC_Module_t eADCmodule, uint32_t uiTrigMask, void *pvUserdata);

typedef struct
{
    bool bIsTrigSlotEnabled;
    bool bEnTrigCompletionNotifyReq;
    eADC_TrigSlot_t eTrigSlot;
    eADC_TrigSrcType_t eTrigSrcType;
    eADC_TrigSource_t eTrigSrc;
    eADC_TrigPrio_t ePrioLevel;
    uint8_t uiTrigDelay;
    sT_ADC_CommandConfig_t *pstTHeadCmdConfig;
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
    bool bIsHighSpeed_Enabled;//
    bool bIsHighSpeedExtra_Enabled;
    CFG2_ADCConversionCycleTune_t stConvCycleTune;
} sT_ADC_HighSpeedConfig_t;

typedef struct
{
    uint8_t uiIntrPriority;
}sT_ADCNotify_Interrupt_t;

typedef struct
{
    uint32_t *uiaResultBuffer;
    uint8_t uiLen;
}sT_ADCNotify_DMA_t;

typedef struct
{
    eADC_NotificationType_t eNotificationType;
    union
    {
        sT_ADCNotify_Interrupt_t stTInterruptCtrl;
        sT_ADCNotify_DMA_t stTDMACtrl;
    } ADCNotify_t;
    
}sT_ADCNotify_Ctrl_t;

typedef struct
{
    bool bIsConfigOk;
    _Atomic bool *pbOverflowFlag;
    eADC_Module_t eADCModule;
    eADC_RefVoltSrc_t eRefSrc;
    uint8_t uiWaterMarkLevel;
    sT_ADCNotify_Ctrl_t stTNotifyCtrl;
    sT_ADC_HighSpeedConfig_t stHighSpeedConfig;
    ADC_TrigCompCallback_t pvTrigCompltCallbackFn;
    sT_ADC_TrigConfig_t staTrigConfig[eNUMBER_OF_ADC_TRIG_SLOTs];
} sT_ADC_ModuleConfig_t;

typedef struct
{
    bool bIsAvailable;
    bool bIsChUsed;
    uint32_t uiLPADCChannelNumber;
    eADC_AssignmentType_t eAssignmentType;
} sT_ADC_ChannelInfo_t;

typedef struct
{
    eADC_TrigSlot_t eTrigSlot;
    eADC_Command_t eCommandId;
    eADC_Channel_t eChannel;
} sT_ADC_ChannelOwner_t;

typedef struct
{
    _Atomic uint16_t uiADCVal;
} sT_ADC_ChannelValue_t;

typedef struct
{
    _Atomic uint16_t uiADCVal;
    uint64_t uiLastTriggerTime_ms;
    _Atomic uint32_t uiReleaseDelay_ms;
} sT_ADC_ChMinMax_t;

typedef struct
{
    _Atomic uint16_t uiADCVal;
    uint64_t uiADCVal_Sum;
    uint16_t uiSampleCount;
    _Atomic uint16_t uiMaxSampleCount;
} sT_ADC_ChAvgRMS_t;

typedef struct
{
    sT_ADC_ChAvgRMS_t stTAvgVal;
    sT_ADC_ChAvgRMS_t stTRMSVal;
    sT_ADC_ChMinMax_t stTMinVal;
    sT_ADC_ChMinMax_t stTMaxVal;
} sT_ADC_ChannelStats_t;

typedef struct
{
    sT_ADC_ChannelInfo_t stInfo;
    sT_ADC_ChannelOwner_t stOwner;
    sT_ADC_ChannelValue_t stValue;
    sT_ADC_ChannelStats_t stStats;
} sT_ADC_ChannelMap_t;

typedef struct
{
    eADC_Module_t eModule;
    eADC_Channel_t eChannel;
    eADC_StatType_t eStatType;
    uint32_t uiRelTime_ms;
} sT_ADC_ChRelTimeUpdate_t;

typedef struct
{
    eADC_Command_t eCMD;
    eADC_Channel_t eChannel;
} sT_CMDChannel_t;

#endif

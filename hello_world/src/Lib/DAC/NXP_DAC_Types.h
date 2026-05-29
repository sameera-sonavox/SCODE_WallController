#ifndef NXP_DAC_TYPES_H
#define NXP_DAC_TYPES_H

#include <stdbool.h>
#include <stdint.h>
#include "DAC_ProjDef.h"

#define DAC_RESOLUTION_BITS                 (12U)
#define DAC_MAX_CODE_VALUE                  (4095U)
#define DAC_FIFO_MAX_VALUE_COUNT            (16U)
#define DAC_MIN_OUTPUT_SETTLING_TIME_NS     (40U)

typedef enum
{
    eDACErr_DMA_Error = 1,
    eDACErr_FIFO_UnderFlow,
    eDACErr_FIFO_OverFlow,
    eNUMBER_OF_DAC_ERRORs
} eDAC_Error;

typedef enum
{
    eDAC_RefVoltSrc_VREF_VDD_ANA = 0,    
    eDAC_RefVoltSrc_VREF_INTERNAL,      /* DACREF_2 reference source. */
    eDAC_RefVoltSrc_VREF_EXT,     /* DACREF_1 reference source. */
    eNUMBER_OF_DAC_REF_VOLT_SRCs
} eDAC_RefVoltSrc_t;

typedef enum
{
    eDAC_TrigSrc_Hardware = 0,
    eDAC_TrigSrc_Software,
    eNUMBER_OF_DAC_TRIG_SRCs
} eDAC_TrigSrc_t;

typedef enum
{
    eDAC_BufferWatermark_1Word = 0,
    eDAC_BufferWatermark_2Words,
    eDAC_BufferWatermark_3Words,
    eDAC_BufferWatermark_4Words,
    eNUMBER_OF_DAC_BUFFER_WATERMARKs
} eDAC_BufferWatermark_t;

typedef enum
{
    eDAC_TrigSrcGroup_None = 0,
    eDAC_TrigSrcGroup_CTIMER,
    eDAC_TrigSrcGroup_LPTIMER,
    eDAC_TrigSrcGroup_AOI,
    eDAC_TrigSrcGroup_GPIO,
    eDAC_TrigSrcGroup_CPU,
    eDAC_TrigSrcGroup_ADC,
    eNUMBER_OF_DAC_TRIG_SRC_GROUPs
} eDAC_TrigSrcGroup_t;

typedef enum
{
    eDAC_TrigSrc_CTIMER0_MAT0 = 0,
    eDAC_TrigSrc_CTIMER0_MAT1,
    eDAC_TrigSrc_CTIMER1_MAT0,
    eDAC_TrigSrc_CTIMER1_MAT1,
    eDAC_TrigSrc_CTIMER2_MAT0,
    eDAC_TrigSrc_CTIMER2_MAT1,
    eDAC_TrigSrc_CTIMER3_MAT0,
    eDAC_TrigSrc_CTIMER3_MAT1,
    eNUMBER_OF_DAC_CTIMER_TRIG_SRCs
} eDAC_TrigSrc_CTimer_t;

typedef enum
{
    eDAC_TrigSrc_AOI_0 = 0,
    eDAC_TrigSrc_AOI_1,
    eNUMBER_OF_DAC_AOI_TRIG_SRCs
} eDAC_TrigSrc_AOI_t;

typedef enum
{
    eDAC_TrigSrc_WUU = 0,
    eDAC_TrigSrc_ARM_TXEV,
    eNUMBER_OF_DAC_CPU_TRIG_SRCs
} eDAC_TrigSrc_CPU_t;

typedef struct
{
    uint8_t uiGPIONum;
    uint32_t uiPinNum;
} sT_DAC_TrigSrc_GPIO_t;

typedef enum
{
    eDAC_TrigSrc_ADC_0 = 0,
    eDAC_TrigSrc_ADC_1,
    eNUMBER_OF_DAC_ADC_TRIG_SRCs
} eDAC_TrigSrc_ADC_t;

typedef struct
{
    eDAC_TrigSrcGroup_t eTrigSrcGroup;
    union
    {
        eDAC_TrigSrc_CTimer_t eCTimerTrigSrc;
        eDAC_TrigSrc_AOI_t eAOITrigSrc;
        eDAC_TrigSrc_CPU_t eCPUTrigSrc;
        eDAC_TrigSrc_ADC_t eADCTrigSrc;
        sT_DAC_TrigSrc_GPIO_t stGPIOTrigSrc;
    } uTrigSrc;
} sT_DAC_TrigSrc_Mux_t;

typedef enum
{
    eDAC_WaveForm_DC = 0,
    eDAC_WaveForm_Triangle,
    eDAC_WaveForm_Sawtooth,
    eDAC_WaveForm_Sine,
    eDAC_WaveForm_WhiteNoise,
    eDAC_WaveForm_PinkNoise,
    eNUMBER_OF_DAC_WAVEFORMs
} eDAC_WaveFormType_t;

typedef enum
{
    eDAC_OUT_Route_Internal = 0,
    eDAC_OUT_Route_External,
    eNUMBER_OF_DAC_OUT_ROUTEs
} eDAC_OUT_RouteType_t;

typedef enum{
    eDAC_Route_Internal_ADC = 0,
    eDAC_Route_Internal_CMP,
    eNUMBER_OF_DAC_INTERNAL_ROUTEs
} eDAC_InternalRoute_t;

typedef enum
{
    eNoiseBuf_Free = 0,      // CPU may write
    eNoiseBuf_Filling,       // CPU is generating samples
    eNoiseBuf_Ready,         // filled, ready for DMA
    eNoiseBuf_Queued,        // handed to DMA/TCD queue
    eNoiseBuf_Active        // currently being consumed
} eNoiseBufState_t;

typedef struct 
{
    eDAC_OUT_RouteType_t eOutRouteType;
    eDAC_InternalRoute_t eInternalRoute; /* Valid only if eOutRouteType is eDAC_OUT_Route_Internal. */
    union{
        uint8_t uiADCChannel;
        uint8_t uiCMPChannel;
    } uInternalRouteConfig; /* Valid only if eOutRouteType is eDAC_OUT_Route_Internal. */
} sT_DAC_OutRouteConfig_t;

typedef enum
{
    eDAC_OutputBuff_Lower_LowPowerMode = 0,
    eDAC_OutputBuff_Higher_LowPowerMode,
    eNUMBER_OF_DAC_OUTPUT_BUFF_MODEs
} eDAC_OutputBuffLowPowerMode_t;

typedef enum
{
    eMode_FIFO = 0,
    eMode_SwingBack,
    eMode_SwingBackWithPeriodic,
    eNUMBER_OF_DAC_FIFO_WORK_MODEs
} eDAC_FIFOWorkMode_t;

typedef enum
{
    eDAC_DefaultOut_Low = 0,
    eDAC_DefaultOut_High,
    eDAC_DefaultOut_Custom,
    eNUMBER_OF_DAC_DEFAULT_OUT_LEVELs
} eDAC_DefaultOutLevel_t;

typedef struct
{
    bool bEnableOutputBuffer;
    eDAC_OutputBuffLowPowerMode_t eOutputBuffLowPowerMode; /* Valid only if bEnableOutputBuffer is true. */
} sT_DAC_OutputBufferConfig_t;

typedef struct{
    uint32_t uiOutputValue_mV;
} sT_DCOutput_t;

typedef struct{
    eT_TCDBuff_t eBuffId;
    _Atomic eNoiseBufState_t eBuffState;
    uint32_t uiBuffer[DMA_NOISEGEN_SAMPLE_COUNT];
} sT_TCDBuffCtrl_t;

typedef void (*DACError_Callback_t)(eDAC_Error eError, void *pUserData);
typedef void (*DACParam_UpdateComplete_Callback_t)(bool status, void *pUserData);

typedef struct{
    eDAC_FIFOWorkMode_t eFIFOWorkMode;
    sT_DAC_TrigSrc_Mux_t stTHWTrigSrc;
    uint32_t uiFrequencyHz;
    uint16_t uiPeakVoltage_mV;
    uint16_t uiDCOffset_mV;
    DACError_Callback_t pvErrorCallback;
} sT_WaveFormOutput_t;

typedef struct
{
    eDAC_WaveFormType_t eWaveFormType;

    union{
        sT_DCOutput_t stDCOutput;
        sT_WaveFormOutput_t stWaveFormOutput;
    } uOutputConfig;
} sT_DAC_OutputConfig_t;

typedef struct
{
    uint32_t uiDMAFlags;
    uint32_t uiDACFlags;
} sT_DAC_DMA_Flags;

typedef struct
{
    eDAC_RefVoltSrc_t eRefVoltSrc;
    sT_DAC_OutRouteConfig_t stOutRouteConfig;
    sT_DAC_OutputBufferConfig_t stOutputBuffConfig;
    sT_DAC_OutputConfig_t stOutputConfig;
    uint32_t uiOutputSettlingTime_ns;
    bool bIsConfigured;
} sT_DAC_Config_t;

#endif /* NXP_DAC_TYPES_H */

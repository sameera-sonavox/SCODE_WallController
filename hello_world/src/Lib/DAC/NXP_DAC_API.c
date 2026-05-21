#include <string.h>
#include <stdatomic.h>
#include <zephyr/kernel.h>

#include "fsl_clock.h"
#include "fsl_reset.h"
#include "fsl_ctimer.h"
#include "fsl_inputmux.h"
#include "fsl_inputmux_connections.h"
#include "fsl_dac.h"

#include "NXP_DAC_API.h"
#include "NXP_DAC_DMAConfig.h"
#include "../GenericMacro.h"

#if defined(DEBUG_DAC_WAVEGEN_SAWTOOTH)
    #define Print_Sawtooth                  printk
#else
    #define Print_Sawtooth(...)
#endif

typedef struct
{
    CTIMER_Type *pstCTimerBase;
    uint32_t uiCTimerId;
    ctimer_match_t eMatchChannel;
    inputmux_connection_t eInputMuxConnection;
    clock_attach_id_t eClockAttach;
    clock_div_name_t eClockDiv;
} sT_DAC_CTimerTrigSource_t;

typedef struct
{
    eDAC_TrigSrcGroup_t eTrigSrcType;
    union{
        sT_DAC_CTimerTrigSource_t stTCTimerConfig;
    } stTrigSrcConfig_t;
} sT_DACHWTrigConfig_t;

typedef struct
{
    uint32_t uiTriggerFrequency_Hz;
    uint32_t uiaBuffer[DAC_MAX_CODE_VALUE + 1];
    uint16_t uiMaxOutput_mV;
    uint16_t uiMinOutput_mv;
    uint16_t uiMaxCode;
    uint16_t uiMinCode;
    uint16_t uiNumberofSamples_Period;
    float fSettlingTime_us;
    sT_DACHWTrigConfig_t stTDACHWConfig;
} sT_DAC_OutputCode_Ctrl_t;

ctimer_match_t caTimerChannelMap[eNUMBER_OF_DAC_CTIMER_TRIG_SRCs] = {
    kCTIMER_Match_0,//eDAC_TrigSrc_CTIMER0_MAT0
    kCTIMER_Match_1,//eDAC_TrigSrc_CTIMER0_MAT1
    kCTIMER_Match_0,//eDAC_TrigSrc_CTIMER1_MAT0
    kCTIMER_Match_1,//eDAC_TrigSrc_CTIMER1_MAT1
    kCTIMER_Match_0,//eDAC_TrigSrc_CTIMER2_MAT0
    kCTIMER_Match_1,//eDAC_TrigSrc_CTIMER2_MAT1
    kCTIMER_Match_0,//eDAC_TrigSrc_CTIMER3_MAT0
    kCTIMER_Match_1,//eDAC_TrigSrc_CTIMER3_MAT1
};

inputmux_connection_t caTimerInputMuxMap[eNUMBER_OF_DAC_CTIMER_TRIG_SRCs] = {
    kINPUTMUX_Ctimer0M0ToDac0Trigger,//eDAC_TrigSrc_CTIMER0_MAT0
    kINPUTMUX_Ctimer0M1ToDac0Trigger,//eDAC_TrigSrc_CTIMER0_MAT1
    kINPUTMUX_Ctimer1M0ToDac0Trigger,//eDAC_TrigSrc_CTIMER1_MAT0
    kINPUTMUX_Ctimer1M1ToDac0Trigger,//eDAC_TrigSrc_CTIMER1_MAT1
    kINPUTMUX_Ctimer2M0ToDac0Trigger,//eDAC_TrigSrc_CTIMER2_MAT0
    kINPUTMUX_Ctimer2M1ToDac0Trigger,//eDAC_TrigSrc_CTIMER2_MAT1
    kINPUTMUX_Ctimer3M0ToDac0Trigger,//eDAC_TrigSrc_CTIMER3_MAT0
    kINPUTMUX_Ctimer3M1ToDac0Trigger,//eDAC_TrigSrc_CTIMER3_MAT1
};

sT_DAC_Config_t stTDACConfig_t = {0};
sT_DAC_OutputCode_Ctrl_t stTDACOutputCodeCtrl_t = {0};

typedef enum
{
    eDAC_InternalMode_Direct = 0,
    eDAC_InternalMode_WaveGen,
    eDAC_InternalMode_Unsupported
} eDAC_InternalMode_t;

#define DACHWTrigCTIMER_t                   (stTDACOutputCodeCtrl_t.stTDACHWConfig.stTrigSrcConfig_t.stTCTimerConfig)

static eDAC_InternalMode_t eTDACInternalMode = eDAC_InternalMode_Unsupported;

#define IsDACConfigured()                   (stTDACConfig_t.bIsConfigured)
#define eGetDACOperationMode()              (eTDACInternalMode)

static void vConfigure_DAC_DirectMode(sT_DAC_Config_t *pstConfig);
static uint32_t uiGetReferenceVoltage_mV(eDAC_RefVoltSrc_t eRefVoltSrc, int *piret);
static void vConfigure_DAC_SawtoothMode(sT_DAC_Config_t *pstConfig);
void vCompute_Waveform_Sawtooth_Params(sT_DAC_Config_t *pstConfig, int *piret);
void vCompute_SawtoothDataBuffer(sT_DAC_Config_t *pstConfig, int *piret);

void vCompute_DAC_OutputTiming(sT_DAC_Config_t *pstConfig, int *piret);
void vConfigure_FIFOWorkMode(dac_config_t *pstDACConfig, sT_DAC_Config_t *pstConfig, int *piret);
void vConfigure_FIFO_NormalMode(dac_config_t *pstDACConfig, sT_DAC_Config_t *pstConfig, int *piret);

void vConfigure_DACTrigSrc_CTIMER(eDAC_TrigSrc_CTimer_t eTrigCTimer, int *piret);
void vConfigure_DACTrigSrc(sT_DAC_Config_t *pstConfig, int *piret);
static void vStart_Timer( void );
static void vStop_Timer( void );

void vDeInit_TimerConfiguration(void);
void vAssign_CTimer_ToConfig(eDAC_TrigSrc_CTimer_t eTrigCTimer, int *piret);

static bool bConfigure_ReferenceSource(sT_DAC_Config_t *pstConfig, dac_config_t *pstDACConfig);
static uint32_t uiCalculateDACCode(uint16_t uiOutput_mV, int *piret);
static void vConfigure_InternalRoute(sT_DAC_Config_t *pstConfig, dac_config_t *pstDACConfig);
static bool bConfigure_RouteToADC(sT_DAC_Config_t *pstConfig, dac_config_t *pstDACConfig);
static bool bConfigure_RouteToCMP(sT_DAC_Config_t *pstConfig, dac_config_t *pstDACConfig);

bool bDAC_UpdateOutputValue( uint16_t uiOutput_mV )
{
    int ret = 0;

    if(!IsDACConfigured())
    {
        FHALT("DAC is not properly configured. Cannot update DAC output value.");
        return false;
    }
    if(eGetDACOperationMode() != eDAC_InternalMode_Direct)
    {
        FHALT("Unsupported DAC operation mode. Only direct mode is supported for updating output value.");
        return false;
    }

    uint32_t code = uiCalculateDACCode(uiOutput_mV, &ret);   
    if(ret != 0)
    {
        FHALT("Failed to calculate DAC code for the requested output voltage.");
        return false;
    }
    DAC_SetData(DAC0, code);
    return true;
}

static uint32_t uiGetReferenceVoltage_mV(eDAC_RefVoltSrc_t eRefVoltSrc, int *piret)
{
    uint32_t uiReferenceVoltage_mV = 0U;
    switch(eRefVoltSrc)
    {
        case eDAC_RefVoltSrc_VREF_VDD_ANA:
            uiReferenceVoltage_mV = VREF_VDD_ANA_mV;
            break;
        case eDAC_RefVoltSrc_VREF_INTERNAL:
            #if defined(VREF_INTERNAL_mV)
                uiReferenceVoltage_mV = VREF_INTERNAL_mV;
            #else
                FHALT("VREF_INTERNAL_mV is not defined. Please define it in DAC_ProjDef.h according to your hardware configuration.");
                *piret = -1;
                return 0U;
            #endif
            break;
        case eDAC_RefVoltSrc_VREF_EXT:
            uiReferenceVoltage_mV = VREF_EXTERNAL_mV;
            break;
        default:
            FHALT("Unsupported DAC reference voltage source.");
            *piret = -1;
            return 0U;
    }
    *piret = 0;
    return uiReferenceVoltage_mV;
}

static uint32_t uiCalculateDACCode(uint16_t uiOutput_mV, int *piret)
{
    uint32_t uiReferenceVoltage_mV = uiGetReferenceVoltage_mV(stTDACConfig_t.eRefVoltSrc, piret);
    if(*piret != 0)
    {
        FHALT("Failed to get reference voltage for DAC code calculation.");
        return 0U;
    }

    if(uiOutput_mV > uiReferenceVoltage_mV)
    {
        FHALT("Requested output voltage exceeds reference voltage.");
        *piret = -1;
        return DAC_MAX_CODE_VALUE;
    }

    uint32_t uiDACCode = ((uint32_t)uiOutput_mV * DAC_MAX_CODE_VALUE) / uiReferenceVoltage_mV;
    *piret = 0;
    return uiDACCode;
}

static void vConfigure_DAC_DirectMode(sT_DAC_Config_t *pstConfig)
{
    int ret = 0;
    dac_config_t stDACConfig;
    DAC_GetDefaultConfig(&stDACConfig);
    pstConfig->bIsConfigured = false;

    stDACConfig.enableOpampBuffer = pstConfig->stOutputBuffConfig.bEnableOutputBuffer;
    if(pstConfig->stOutputBuffConfig.bEnableOutputBuffer)
    {
        stDACConfig.enableLowerLowPowerMode = 
        (pstConfig->stOutputBuffConfig.eOutputBuffLowPowerMode == eDAC_OutputBuff_Lower_LowPowerMode) ? true : false;
    }
    if(!bConfigure_ReferenceSource(pstConfig, &stDACConfig))
    {
        FHALT("Failed to configure DAC reference voltage source.");
        return;
    }

    stDACConfig.fifoTriggerMode = kDAC_FIFOTriggerBySoftwareMode;
    stDACConfig.fifoWorkMode = kDAC_FIFODisabled;
    stDACConfig.syncTime = 1U;

    if(pstConfig->stOutRouteConfig.eOutRouteType == eDAC_OUT_Route_Internal)
    {
        vConfigure_InternalRoute(pstConfig, &stDACConfig);
        if(!pstConfig->bIsConfigured)
        {
            FHALT("Failed to configure internal DAC route.");
            return;
        }
        pstConfig->bIsConfigured = false;
    }

    uint32_t uiCode = uiCalculateDACCode(pstConfig->stOutputConfig.uOutputConfig.stDCOutput.uiOutputValue_mV, &ret);
    if(ret != 0)
    {
        FHALT("Failed to calculate DAC code for the initial output voltage.");
        pstConfig->bIsConfigured = false;
        return;
    }

    DAC_Init(DAC0, &stDACConfig);
    DAC_Enable(DAC0, true);
    DAC_SetData(DAC0, uiCode);    
    pstConfig->bIsConfigured = true;
}

static void vConfigure_InternalRoute(sT_DAC_Config_t *pstConfig, dac_config_t *pstDACConfig)
{
    bool result = false;

    switch(pstConfig->stOutRouteConfig.eInternalRoute)
    {
        case eDAC_Route_Internal_ADC:
            result = bConfigure_RouteToADC(pstConfig, pstDACConfig);
             if(!result)
            {
                FHALT("Failed to configure internal route to ADC.");
                pstConfig->bIsConfigured = false;
                return;
            }
            break;
        case eDAC_Route_Internal_CMP:
            result = bConfigure_RouteToCMP(pstConfig, pstDACConfig);
             if(!result)
            {
                FHALT("Failed to configure internal route to CMP.");
                pstConfig->bIsConfigured = false;
                return;
            }            
            break;
        default:
            FHALT("Unsupported internal route for DAC output.");
            pstConfig->bIsConfigured = false;
            return;
    }
    pstConfig->bIsConfigured = true;
}

static bool bConfigure_RouteToADC(sT_DAC_Config_t *pstConfig, dac_config_t *pstDACConfig)
{
    /* This is a placeholder function. The actual implementation will depend on the specific hardware and SDK being used. */
    FHALT("Routing DAC output to ADC is not implemented yet.");
    return false;
}

static bool bConfigure_RouteToCMP(sT_DAC_Config_t *pstConfig, dac_config_t *pstDACConfig)
{
    /* This is a placeholder function. The actual implementation will depend on the specific hardware and SDK being used. */
    FHALT("Routing DAC output to CMP is not implemented yet.");
    return false;
}

static bool bConfigure_ReferenceSource(sT_DAC_Config_t *pstConfig, dac_config_t *pstDACConfig)
{
    switch(pstConfig->eRefVoltSrc)
    {
        case eDAC_RefVoltSrc_VREF_VDD_ANA:
        #if defined(VREF_VDD_ANA_mV)
            pstDACConfig->referenceVoltageSource = kDAC_ReferenceVoltageSourceAlt1;
        #else
            FHALT("VREF_VDD_ANA_mV is not defined. Please define it in DAC_ProjDef.h according to your hardware configuration.");
            pstConfig->bIsConfigured = false;
            return false;
        #endif
            break;
        case eDAC_RefVoltSrc_VREF_INTERNAL:
        #if defined(VREF_INTERNAL_mV)
            pstDACConfig->referenceVoltageSource = kDAC_ReferenceVoltageSourceAlt2;
        #else
            FHALT("VREF_INTERNAL_mV is not defined. Please define it in DAC_ProjDef.h according to your hardware configuration.");
            pstConfig->bIsConfigured = false;
            return false;
        #endif
            break;
        case eDAC_RefVoltSrc_VREF_EXT:
        #if defined(VREF_EXTERNAL_mV)
            pstDACConfig->referenceVoltageSource = kDAC_ReferenceVoltageSourceAlt3;
        #else
            FHALT("VREF_EXTERNAL_mV is not defined. Please define it in DAC_ProjDef.h according to your hardware configuration.");
            pstConfig->bIsConfigured = false;
            return false;
        #endif
            break;
        default:
            FHALT("Unsupported DAC reference voltage source.");
            pstConfig->bIsConfigured = false;
            return false;
    }
    return true;
}

void vDAC_Init(sT_DAC_Config_t *pstConfig)
{
    if(pstConfig == NULL)
    {
        FHALT("Invalid pointer to DAC configuration structure.");
        return;
    }

    memcpy(&stTDACConfig_t, pstConfig, sizeof(sT_DAC_Config_t));
    stTDACConfig_t.bIsConfigured = false;
    eTDACInternalMode = eDAC_InternalMode_Unsupported;

    switch (stTDACConfig_t.stOutputConfig.eWaveFormType)
    {
        case eDAC_WaveForm_DC:
            eTDACInternalMode = eDAC_InternalMode_Direct;
            vConfigure_DAC_DirectMode(&stTDACConfig_t);
            break;
        case eDAC_WaveForm_Sawtooth:
             eTDACInternalMode = eDAC_InternalMode_WaveGen;
            vConfigure_DAC_SawtoothMode(&stTDACConfig_t);
            break;        
        default:
            stTDACConfig_t.bIsConfigured = false;
            FHALT("Unsupported DAC output waveform.");
            break;
    }
    printk("DAC initialized in %d mode.\n", eTDACInternalMode);
    pstConfig->bIsConfigured = stTDACConfig_t.bIsConfigured;
}

static void vConfigure_DAC_SawtoothMode(sT_DAC_Config_t *pstConfig)
{
    int iret = 0;    
    dac_config_t stDACConfig;

    if(pstConfig == NULL)
    {
        FHALT("Invalid pointer to DAC configuration structure.");
        return;
    }
    Print_Sawtooth("Wavegen -> Sawtooth Started...\n\r");

    vCompute_Waveform_Sawtooth_Params(pstConfig, &iret);
    if(iret != 0)
    {
        FHALT("Failed to compute buffer control values for DAC sawtooth mode.");
        pstConfig->bIsConfigured = false;
        return;
    }
    Print_Sawtooth("Wavegen -> Parameters Configured\n\r");

    DAC_GetDefaultConfig(&stDACConfig);
    stDACConfig.enableOpampBuffer = true;    
    stDACConfig.enableLowerLowPowerMode = 
                        (pstConfig->stOutputBuffConfig.eOutputBuffLowPowerMode == eDAC_OutputBuff_Lower_LowPowerMode) ? true : false;
    stDACConfig.syncTime = 1U;
    if(!bConfigure_ReferenceSource(pstConfig, &stDACConfig))
    {
        FHALT("Failed to configure DAC reference voltage source.");
        pstConfig->bIsConfigured = false;
        return;
    }
    Print_Sawtooth("Wavegen -> Reference Source Configured\n\r");

    vConfigure_FIFOWorkMode(&stDACConfig, pstConfig, &iret);
    if(iret != 0)
    {
        pstConfig->bIsConfigured = false;
        return;
    }
    Print_Sawtooth("Wavegen -> WorkMode Configured\n\r");

    DAC_Init(DAC0, &stDACConfig);
    DAC_Enable(DAC0, true);

    vConfigure_DACTrigSrc(pstConfig, &iret);
    if(iret != 0)
    {
        pstConfig->bIsConfigured = false;
        return;
    }
    Print_Sawtooth("Wavegen -> Trigger Source Configured\n\r");

    pstConfig->bIsConfigured = true;
}

void vConfigure_FIFOWorkMode(dac_config_t *pstDACConfig, sT_DAC_Config_t *pstConfig, int *piret)
{
    if(piret ==NULL)
    {
        FHALT("Null Pointer reference for return value.");
        return;        
    }
    if(pstConfig == NULL)
    {
        FHALT("Null Pointer reference for Configuration.");
        *piret = -1;
        return;        
    }
    if(pstDACConfig == NULL)
    {
        FHALT("Null Pointer reference for DAC configuration");
        pstConfig->bIsConfigured = false;
        *piret = -1;
        return;        
    }

    eDAC_FIFOWorkMode_t fifoWorkMode = pstConfig->stOutputConfig.uOutputConfig.stWaveFormOutput.eFIFOWorkMode;
    
    switch(fifoWorkMode)
    {
        case eMode_FIFO:
            vConfigure_FIFO_NormalMode(pstDACConfig, pstConfig, piret);
            break;
        case eMode_SwingBack:
            break;
        case eMode_SwingBackWithPeriodic:
            break;
        default:
            FHALT("Invalid FIFO Work Mode : %d", fifoWorkMode);
            pstConfig->bIsConfigured = false;
            *piret = -1;
            return;
    }

    if(*piret < 0)
        pstConfig->bIsConfigured = false;
}

void vConfigure_FIFO_NormalMode(dac_config_t *pstDACConfig, sT_DAC_Config_t *pstConfig, int *piret)
{
    ARG_UNUSED(pstConfig);

    pstDACConfig->fifoWorkMode = kDAC_FIFOWorkAsNormalMode;
    pstDACConfig->fifoTriggerMode = kDAC_FIFOTriggerByHardwareMode;
    pstDACConfig->fifoWatermarkLevel = 4U;
    *piret = 0;
}

void vConfigure_DACTrigSrc(sT_DAC_Config_t *pstConfig, int *piret)
{
    #define stTrigSrcMux_t      (pstConfig->stOutputConfig.uOutputConfig.stWaveFormOutput.stTHWTrigSrc)

    switch(stTrigSrcMux_t.eTrigSrcGroup)
    {
        case eDAC_TrigSrcGroup_CTIMER:
            vConfigure_DACTrigSrc_CTIMER(stTrigSrcMux_t.uTrigSrc.eCTimerTrigSrc, piret);
            break;
        case eDAC_TrigSrcGroup_LPTIMER:
            break;
        case eDAC_TrigSrcGroup_AOI:
            break;
        case eDAC_TrigSrcGroup_GPIO:
            break;
        case eDAC_TrigSrcGroup_CPU:
            break;
        case eDAC_TrigSrcGroup_ADC:
            break;
        case eDAC_TrigSrcGroup_None:
        default:
            FHALT("Invalid HW Trigger Configuration\n\r");
            *piret = -1;
            return;
    }
    
    if(*piret != 0)
        return;
}

void vConfigure_DACTrigSrc_CTIMER(eDAC_TrigSrc_CTimer_t eTrigCTimer, int *piret)
{
    ctimer_config_t stTimerConfig;
    ctimer_match_config_t stMatchConfig;
    uint32_t uiTimerClock_Hz;
    uint32_t uiMatchValue;

    vAssign_CTimer_ToConfig(eTrigCTimer, piret);
    if(*piret != 0)
    {
        FHALT("Invalid CTIMER Configuration");
        return;
    }

    CLOCK_AttachClk(DACHWTrigCTIMER_t.eClockAttach);
    CLOCK_SetClockDiv(DACHWTrigCTIMER_t.eClockDiv, 1U);

    uiTimerClock_Hz = CLOCK_GetCTimerClkFreq(DACHWTrigCTIMER_t.uiCTimerId);
    if(uiTimerClock_Hz == 0)
    {
        FHALT("CTimer[%d] Base Clock is not set", DACHWTrigCTIMER_t.uiCTimerId);
        *piret = -1;
        return;
    }
    uiMatchValue = uiTimerClock_Hz / stTDACOutputCodeCtrl_t.uiTriggerFrequency_Hz;
    if(uiMatchValue == 0U)
    {
        FHALT("CTimer[%d] Match value is zero", DACHWTrigCTIMER_t.uiCTimerId);
        *piret = -1;
        return;        
    }
    uiMatchValue -= 1U;

    CTIMER_GetDefaultConfig(&stTimerConfig);
    stTimerConfig.mode = kCTIMER_TimerMode;
    stTimerConfig.prescale = 0U;
    CTIMER_Init(DACHWTrigCTIMER_t.pstCTimerBase, &stTimerConfig);

    stMatchConfig.matchValue = uiMatchValue;
    stMatchConfig.enableCounterReset = true;
    stMatchConfig.enableCounterStop = false;
    stMatchConfig.outControl = kCTIMER_Output_Toggle;
    stMatchConfig.outPinInitState = false;
    stMatchConfig.enableInterrupt = false;
    CTIMER_SetupMatch(DACHWTrigCTIMER_t.pstCTimerBase,
                      DACHWTrigCTIMER_t.eMatchChannel,
                      &stMatchConfig);
    
    INPUTMUX_Init(INPUTMUX0);
    INPUTMUX_AttachSignal(INPUTMUX0, 0U, DACHWTrigCTIMER_t.eInputMuxConnection);
    
    if(!bSetup_DAC_DMA_Circular(stTDACOutputCodeCtrl_t.uiaBuffer, 
                       stTDACOutputCodeCtrl_t.uiNumberofSamples_Period, 
                       (uintptr_t)DAC0))
    {
        vDeInit_TimerConfiguration();
        *piret = -1;
        return;         
    }

    vStart_Timer();
    k_busy_wait(100U);
    *piret = 0;
}

static void vStart_Timer( void )
{
    if(DACHWTrigCTIMER_t.pstCTimerBase == NULL)
    {
        FHALT("Invalid Operation : Timer is NULL");
        return;
    }
    CTIMER_StartTimer(DACHWTrigCTIMER_t.pstCTimerBase);
}

static void vStop_Timer( void )
{
    if(DACHWTrigCTIMER_t.pstCTimerBase == NULL)
    {
        FHALT("Invalid Operation : Timer is NULL");
        return;
    }

    CTIMER_StopTimer(DACHWTrigCTIMER_t.pstCTimerBase);
}

void vDeInit_TimerConfiguration(void)
{
    if (DACHWTrigCTIMER_t.pstCTimerBase == NULL)
    {
        return;
    }

    vStop_Timer();
    CTIMER_Reset(DACHWTrigCTIMER_t.pstCTimerBase);
    CTIMER_Deinit(DACHWTrigCTIMER_t.pstCTimerBase);

    DACHWTrigCTIMER_t.pstCTimerBase = NULL;
}

void vAssign_CTimer_ToConfig(eDAC_TrigSrc_CTimer_t eTrigCTimer, int *piret)
{
    switch (eTrigCTimer)
    {
        case eDAC_TrigSrc_CTIMER0_MAT0:
        case eDAC_TrigSrc_CTIMER0_MAT1: 
            DACHWTrigCTIMER_t.pstCTimerBase = CTIMER0;
            DACHWTrigCTIMER_t.uiCTimerId = 0;
            DACHWTrigCTIMER_t.eMatchChannel = caTimerChannelMap[eTrigCTimer];
            DACHWTrigCTIMER_t.eInputMuxConnection = caTimerInputMuxMap[eTrigCTimer];
            DACHWTrigCTIMER_t.eClockAttach = kFRO_HF_to_CTIMER0;
            DACHWTrigCTIMER_t.eClockDiv = kCLOCK_DivCTIMER0;
            *piret = 0;
            break;
        case eDAC_TrigSrc_CTIMER1_MAT0:
        case eDAC_TrigSrc_CTIMER1_MAT1:
            DACHWTrigCTIMER_t.pstCTimerBase = CTIMER1;
            DACHWTrigCTIMER_t.uiCTimerId = 1;
            DACHWTrigCTIMER_t.eMatchChannel = caTimerChannelMap[eTrigCTimer];
            DACHWTrigCTIMER_t.eInputMuxConnection = caTimerInputMuxMap[eTrigCTimer];
            DACHWTrigCTIMER_t.eClockAttach = kFRO_HF_to_CTIMER1;
            DACHWTrigCTIMER_t.eClockDiv = kCLOCK_DivCTIMER1;
            *piret = 0;             
            break;
        case eDAC_TrigSrc_CTIMER2_MAT0:
        case eDAC_TrigSrc_CTIMER2_MAT1:
            DACHWTrigCTIMER_t.pstCTimerBase = CTIMER2;
            DACHWTrigCTIMER_t.uiCTimerId = 2;
            DACHWTrigCTIMER_t.eMatchChannel = caTimerChannelMap[eTrigCTimer];
            DACHWTrigCTIMER_t.eInputMuxConnection = caTimerInputMuxMap[eTrigCTimer];
            DACHWTrigCTIMER_t.eClockAttach = kFRO_HF_to_CTIMER2;
            DACHWTrigCTIMER_t.eClockDiv = kCLOCK_DivCTIMER2;
            *piret = 0;              
            break;
        case eDAC_TrigSrc_CTIMER3_MAT0:
        case eDAC_TrigSrc_CTIMER3_MAT1:
            DACHWTrigCTIMER_t.pstCTimerBase = CTIMER3;
            DACHWTrigCTIMER_t.uiCTimerId = 3;
            DACHWTrigCTIMER_t.eMatchChannel = caTimerChannelMap[eTrigCTimer];
            DACHWTrigCTIMER_t.eInputMuxConnection = caTimerInputMuxMap[eTrigCTimer];
            DACHWTrigCTIMER_t.eClockAttach = kFRO_HF_to_CTIMER3;
            DACHWTrigCTIMER_t.eClockDiv = kCLOCK_DivCTIMER3;
            *piret = 0;              
            break;     
        default:
            FHALT("Invalid Timer Module/Channel Selection for DAC HW Trigger. Defined TimerConfig: %d", eTrigCTimer);
            *piret = -1;
            break;
    }
}

void vCompute_Waveform_Sawtooth_Params(sT_DAC_Config_t *pstConfig, int *piret)
{
    if(pstConfig == NULL || piret == NULL)
    {
        FHALT("Invalid pointer to DAC configuration structure or return value pointer.");
        *piret = -1;
        return;
    }

    sT_WaveFormOutput_t *outputConfig = &pstConfig->stOutputConfig.uOutputConfig.stWaveFormOutput;
    
    if(outputConfig->uiAmplitude_mV == 0)
    {
        FHALT("Amplitude for sawtooth waveform cannot be zero.");
        *piret = -1;
        pstConfig->bIsConfigured = false;
        return;
    }
    
    uint16_t uiReferenceVoltage_mV = uiGetReferenceVoltage_mV(pstConfig->eRefVoltSrc, piret);
    if(*piret != 0)
    {
        FHALT("Failed to get reference voltage for computing buffer control values.");
        pstConfig->bIsConfigured = false;
        return;
    }

    stTDACOutputCodeCtrl_t.uiMaxOutput_mV = outputConfig->uiDCOffset_mV + outputConfig->uiAmplitude_mV;
    stTDACOutputCodeCtrl_t.uiMinOutput_mv = outputConfig->uiDCOffset_mV;
    if(stTDACOutputCodeCtrl_t.uiMaxOutput_mV > uiReferenceVoltage_mV)
    {
        FHALT("Calculated maximum output voltage exceeds reference voltage.");
        *piret = -1;
        return;
    }

    stTDACOutputCodeCtrl_t.uiMaxCode = uiCalculateDACCode(stTDACOutputCodeCtrl_t.uiMaxOutput_mV, piret);
    if(*piret != 0)    {
        pstConfig->bIsConfigured = false;
        FHALT("Failed to calculate DAC code for maximum output voltage.");
        return;
    }

    stTDACOutputCodeCtrl_t.uiMinCode = uiCalculateDACCode(stTDACOutputCodeCtrl_t.uiMinOutput_mv, piret);
    if(*piret != 0)    {
        pstConfig->bIsConfigured = false;
        FHALT("Failed to calculate DAC code for minimum output voltage.");
        return;
    }

    vCompute_DAC_OutputTiming(pstConfig, piret);
    if(*piret != 0)    {
        pstConfig->bIsConfigured = false;
        FHALT("Failed to compute DAC output settling time for sawtooth mode.");
        return;
    }
    
    vCompute_SawtoothDataBuffer(pstConfig, piret);
    if(*piret != 0)    {
        pstConfig->bIsConfigured = false;
        FHALT("Failed to compute sawtooth data buffer.");
        return;
    }
    *piret = 0;
}

void vCompute_SawtoothDataBuffer(sT_DAC_Config_t *pstConfig, int *piret)
{
    #define uiminCode       (stTDACOutputCodeCtrl_t.uiMinCode)
    #define uimaxCode       (stTDACOutputCodeCtrl_t.uiMaxCode)
    #define uiNumSamples    (stTDACOutputCodeCtrl_t.uiNumberofSamples_Period)
    uint16_t uiCodeSpan = uimaxCode - uiminCode;

    if(uiNumSamples < 2U)
    {
        FHALT("Sawtooth waveform requires at least two samples per period.");
        *piret = -1;
        return;
    }

    if(uiNumSamples > (DAC_MAX_CODE_VALUE + 1U))
    {
        uiNumSamples = DAC_MAX_CODE_VALUE + 1U;
    }

    if(uiNumSamples > (uiCodeSpan + 1U))
    {
        uiNumSamples = uiCodeSpan + 1U;
    }

    if(uiNumSamples < 2U)
    {
        FHALT("Number of samples per period is invalid @Samples = %d", uiNumSamples);
        *piret = -1;
        return;
    }

    uint32_t *pBuffer = stTDACOutputCodeCtrl_t.uiaBuffer;

    memset(pBuffer, 0, sizeof(stTDACOutputCodeCtrl_t.uiaBuffer));

    for(uint16_t i = 0; i < uiNumSamples; i++)
    {
        pBuffer[i] = (uint32_t)uiminCode + (uint32_t)(((uint32_t)i * uiCodeSpan) / (uiNumSamples - 1U));
    }
    *piret = 0;
}

void vCompute_DAC_OutputTiming(sT_DAC_Config_t *pstConfig, int *piret)
{
    float ffreq_hz = 0.0f;
    if(pstConfig == NULL || piret == NULL)
    {
        FHALT("Invalid pointer to DAC configuration structure or return value pointer.");
        *piret = -1;
        return;
    }
    if(pstConfig->stOutputConfig.uOutputConfig.stWaveFormOutput.uiFrequencyHz == 0)
    {
        FHALT("Frequency for sawtooth waveform cannot be zero.");
        *piret = -1;
        return;
    }

    #define ePowerMode        (pstConfig->stOutputBuffConfig.eOutputBuffLowPowerMode)
    switch (ePowerMode)
    {
        case eDAC_OutputBuff_Lower_LowPowerMode:
            stTDACOutputCodeCtrl_t.fSettlingTime_us = (float)LOWER_LOWER_POWER_MODE_SETTLING_TIME_US;
            break;
        case eDAC_OutputBuff_Higher_LowPowerMode:
            stTDACOutputCodeCtrl_t.fSettlingTime_us = (float)HIGHER_LOWER_POWER_MODE_SETTLING_TIME_US;
            break;
        default:
            FHALT("Unsupported output buffer low power mode for calculating DAC output settling time.");
            *piret = -1;
            return;
    }
    stTDACOutputCodeCtrl_t.fSettlingTime_us += (stTDACOutputCodeCtrl_t.fSettlingTime_us * fSETTLING_TIME_MARGIN_PERCENTAGE);
    ffreq_hz = 1.0f / (stTDACOutputCodeCtrl_t.fSettlingTime_us * 1e-6f);
    stTDACOutputCodeCtrl_t.uiTriggerFrequency_Hz = (uint32_t)ffreq_hz;
    stTDACOutputCodeCtrl_t.uiNumberofSamples_Period = (uint16_t)(stTDACOutputCodeCtrl_t.uiTriggerFrequency_Hz / 
                                                      pstConfig->stOutputConfig.uOutputConfig.stWaveFormOutput.uiFrequencyHz);
    if(stTDACOutputCodeCtrl_t.uiNumberofSamples_Period < DAC_MIN_WAVEFORM_SAMPLES_PER_PERIOD)
    {
        FHALT("Calculated number of samples per period for sawtooth waveform is less than the minimum required.");
        *piret = -1;
        return;
    }
    *piret = 0;
}

void vDAC_Enable(void)
{
    if(!stTDACConfig_t.bIsConfigured)
    {
        FHALT("DAC is not properly configured. Cannot enable DAC.");
        return;
    }
    DAC_Enable(DAC0, true);
}

void vDAC_Disable(void)
{
    DAC_Enable(DAC0, false);
}

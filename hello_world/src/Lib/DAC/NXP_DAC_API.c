#include <string.h>
#include <stdatomic.h>
#include <math.h>
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

#define DAC_TWO_PI_F                       (6.28318530717958647692f)

#if defined(DEBUG_DAC_WAVEGEN_SAWTOOTH)
    #define Print_Sawtooth                  printk
#else
    #define Print_Sawtooth(...)
#endif
#if defined(DEBUG_DAC_WAVEGEN_SINE)
    #define Print_Sine                      printk
#else
    #define Print_Sine(...)
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

typedef enum
{
    eDAC_Buffer_A = 0,
    eDAC_Buffer_B,
    eNUMBER_OF_DAC_BUFFERs
} eDAC_Buffer_t;

typedef struct
{
    uint32_t uiTriggerFrequency_Hz;
    eDAC_Buffer_t eCurrentBuffer;
    uint32_t uiaBuffer_A[DAC_MAX_CODE_VALUE + 1];
    uint32_t uiaBuffer_B[DAC_MAX_CODE_VALUE + 1];
    uint16_t uiMaxOutput_mV;
    uint16_t uiMinOutput_mv;
    uint16_t uiMaxCode;
    uint16_t uiMinCode;
    uint16_t uiNumberofSamples_Period;
    float fSettlingTime_us;
    _Atomic bool bIsUpdatePending;
    _Atomic bool bParamUpdateStatus;
    sT_DACHWTrigConfig_t stTDACHWConfig;
} sT_DAC_OutputCode_Ctrl_t;

typedef struct
{
    uint32_t uiTriggerFrequency_Hz;
    eDAC_Buffer_t eCurrentBuffer;
    uint16_t uiMaxOutput_mV;
    uint16_t uiMinOutput_mv;
    uint16_t uiMaxCode;
    uint16_t uiMinCode;
    uint16_t uiNumberofSamples_Period;
    float fSettlingTime_us;
    sT_DACHWTrigConfig_t stTDACHWConfig;
} sT_DAC_OutputCode_Metadata_t;

typedef struct{
    eDAC_Buffer_t eReqBuffSwapId;
    sT_DAC_OutputCode_Metadata_t stTNewBuffConfigs;
    sT_DAC_Config_t stTDACConfigTemp;
} sT_DACBuffSwap_t;

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
sT_DACBuffSwap_t stTBuffSwapData = {0};

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

static struct k_spinlock stLock_DACOutputCodeCtrl;
static struct k_spinlock stLock_WaveFormParamUpdate;

static void vConfigure_DAC_DirectMode(sT_DAC_Config_t *pstConfig);
static uint32_t uiGetReferenceVoltage_mV(eDAC_RefVoltSrc_t eRefVoltSrc, int *piret);

uint32_t * puiGetDACBuffer(eDAC_Buffer_t eBuffer, sT_DAC_OutputCode_Ctrl_t *pstOutputCodeCtrl);
void vSet_ActiveBuffer(eDAC_Buffer_t eBuffer, sT_DAC_OutputCode_Ctrl_t *pstOutputCodeCtrl);
eDAC_Buffer_t eGetInactiveBufferId( const sT_DAC_OutputCode_Metadata_t *pstOutputCodeCtrl );

static void vConfigure_DAC_SawtoothMode(sT_DAC_Config_t *pstConfig);
void vCompute_Waveform_Params(sT_DAC_Config_t *pstConfig, sT_DAC_OutputCode_Metadata_t *pstDACOutCtrl, int *piret);
void vCompute_SawtoothDataBuffer(int *piret, uint32_t *puiBuffer, const sT_DAC_OutputCode_Metadata_t *pstDACOutCtrl);

static void vConfigure_DAC_SineWaveMode(sT_DAC_Config_t *pstConfig);
void vCompute_SineWaveDataBuffer(int *piret, uint32_t *puiBuffer, const sT_DAC_OutputCode_Metadata_t *pstDACOutCtrl);

void vCompute_DAC_OutputTiming(sT_DAC_Config_t *pstConfig, sT_DAC_OutputCode_Metadata_t *pstDACOutCodeCtrl, int *piret);
void vConfigure_FIFOWorkMode(dac_config_t *pstDACConfig, sT_DAC_Config_t *pstConfig, int *piret);
void vConfigure_FIFO_NormalMode(dac_config_t *pstDACConfig, sT_DAC_Config_t *pstConfig, int *piret);
static void vLoad_DACOutputCtrlMetadata(sT_DAC_OutputCode_Metadata_t *pstDest,
                                        const sT_DAC_OutputCode_Ctrl_t *pstSrc);
static void vCommit_DACOutputCtrlMetadata(sT_DAC_OutputCode_Ctrl_t *pstDest,
                                          const sT_DAC_OutputCode_Metadata_t *pstSrc);

void vConfigure_DAC_StructsForParam_Update(sT_DAC_OutputCode_Metadata_t *pstTempDACOutCtrl, 
                                           sT_DAC_Config_t *pstDacConfig, 
                                           eDAC_Buffer_t *peInactiveBuffIndex,
                                           uint32_t **ppuiInactiveBuffer, 
                                           int *piret);
static inline void vSet_DMAUpdate_Pending( int *piret );
static inline void vClear_DMAUpdate_Pending( void );
static inline bool bIsUpdatePending( void );
static void vNotify_DACParameterUpdate_Callback(bool status, void *pUserData);
static inline void vSet_DMAUpdate_Status( bool status );
static inline void vClear_DMAUpdate_Status( void );
static inline bool bIs_DMAUpdate_Success( void );

void vConfigure_DACTrigSrc_CTIMER(eDAC_TrigSrc_CTimer_t eTrigCTimer, int *piret, DACError_Callback_t vCallBackFn);
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

static struct k_work stWorker_ParamUpdateExecute;
static void vDAC_ParamUpdateExecute(struct k_work *work);

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

void vUpdate_WaveForm_Volume(uint16_t uiPeakVolt_mV)
{
    int iret = 0;

    if(!IsDACConfigured() || (eGetDACOperationMode() != eDAC_InternalMode_WaveGen))
    {
        FHALT("DAC is not properly configured for waveform generation. Cannot update waveform volume.");
        return;
    }
    if(bIsUpdatePending())
        return;

    k_spinlock_key_t key = k_spin_lock(&stLock_WaveFormParamUpdate);

    uint32_t *puiInactiveBuffer = NULL;

    memcpy(&stTBuffSwapData.stTDACConfigTemp, &stTDACConfig_t, sizeof(sT_DAC_Config_t));
    vLoad_DACOutputCtrlMetadata(&stTBuffSwapData.stTNewBuffConfigs, &stTDACOutputCodeCtrl_t);
    stTBuffSwapData.eReqBuffSwapId = eNUMBER_OF_DAC_BUFFERs;

    sT_DAC_OutputCode_Metadata_t *pstTempDACOut = &stTBuffSwapData.stTNewBuffConfigs;
    sT_DAC_Config_t *pstTempDACConfig = &stTBuffSwapData.stTDACConfigTemp;

    pstTempDACConfig->stOutputConfig.uOutputConfig.stWaveFormOutput.uiPeakVoltage_mV = uiPeakVolt_mV;
    vConfigure_DAC_StructsForParam_Update(pstTempDACOut, pstTempDACConfig, &stTBuffSwapData.eReqBuffSwapId, &puiInactiveBuffer, &iret);
    if(iret != 0)
    {
        k_spin_unlock(&stLock_WaveFormParamUpdate, key);
        return;
    }

    vSet_DMAUpdate_Pending(&iret);
    if(iret != 0)
    {
        k_spin_unlock(&stLock_WaveFormParamUpdate, key);
        return;        
    }
    if(!bRequest_DMA_BufferSwap(puiInactiveBuffer, 
                              pstTempDACOut->uiNumberofSamples_Period,
                              (uint8_t)stTBuffSwapData.eReqBuffSwapId))
    {
        vClear_DMAUpdate_Pending();
        k_spin_unlock(&stLock_WaveFormParamUpdate, key);
        FHALT("Failed to switch DAC DMA buffer.");
        return;        
    }

    k_spin_unlock(&stLock_WaveFormParamUpdate, key); 

}

void vNotify_DACParameterUpdate_Callback(bool status, void *pUserData)
{
    vSet_DMAUpdate_Status(status);
    k_work_submit(&stWorker_ParamUpdateExecute);
}

static inline void vSet_DMAUpdate_Status( bool status )
{
    atomic_store_explicit(&stTDACOutputCodeCtrl_t.bParamUpdateStatus, status, memory_order_release);
}

static inline void vClear_DMAUpdate_Status( void )
{
    atomic_store_explicit(&stTDACOutputCodeCtrl_t.bParamUpdateStatus, false, memory_order_release);
}

static inline bool bIs_DMAUpdate_Success( void )
{
    bool status = atomic_load_explicit(&stTDACOutputCodeCtrl_t.bParamUpdateStatus, memory_order_acquire);
    return status;
}

void vDAC_ParamUpdateExecute(struct k_work *work)
{
    ARG_UNUSED(work);

    bool status = bIs_DMAUpdate_Success();
    k_spinlock_key_t key = k_spin_lock(&stLock_WaveFormParamUpdate);

    if(status)
    {
        stTBuffSwapData.stTNewBuffConfigs.eCurrentBuffer = stTBuffSwapData.eReqBuffSwapId;
        memcpy(&stTDACConfig_t, &stTBuffSwapData.stTDACConfigTemp, sizeof(sT_DAC_Config_t));
        vCommit_DACOutputCtrlMetadata(&stTDACOutputCodeCtrl_t, &stTBuffSwapData.stTNewBuffConfigs);
    }

    memset(&stTBuffSwapData, 0, sizeof(stTBuffSwapData));
    vClear_DMAUpdate_Pending();
    vClear_DMAUpdate_Status();
    printf("DAC Update Status : %d\n\r", status);
    k_spin_unlock(&stLock_WaveFormParamUpdate, key);
}

void vConfigure_DAC_StructsForParam_Update(sT_DAC_OutputCode_Metadata_t *pstTempDACOutCtrl, 
                                           sT_DAC_Config_t *pstDacConfig, 
                                           eDAC_Buffer_t *peInactiveBuffIndex,
                                           uint32_t **ppuiInactiveBuffer, 
                                           int *piret)
{    
    if(piret == NULL)
    {
        FHALT("Null Pointer reference for the return value");
        return;
    }
    if(pstTempDACOutCtrl == NULL || pstDacConfig == NULL || peInactiveBuffIndex == NULL || ppuiInactiveBuffer == NULL)
    {
        FHALT("NULL Pointer reference");
        *piret = -1;
        return;
    }

    *peInactiveBuffIndex = eNUMBER_OF_DAC_BUFFERs;
    *ppuiInactiveBuffer = NULL;

    vCompute_Waveform_Params(pstDacConfig, pstTempDACOutCtrl, piret);
    if(*piret != 0)
    {
        FHALT("Failed to compute buffer control values for DAC sawtooth mode.");
        return;
    }

    eDAC_WaveFormType_t eCurrentWaveType = pstDacConfig->stOutputConfig.eWaveFormType;
    eDAC_Buffer_t eInactiveBuffer = eGetInactiveBufferId(pstTempDACOutCtrl);
    if(eInactiveBuffer == eNUMBER_OF_DAC_BUFFERs)
    {
        *piret = -1;
        return;
    }
    
    uint32_t *puiBuffer = puiGetDACBuffer(eInactiveBuffer, &stTDACOutputCodeCtrl_t);
    if(puiBuffer == NULL)
    {
        *piret = -1;
        return;    
    }

    switch (eCurrentWaveType)
    {
        case eDAC_WaveForm_Sawtooth:
            vCompute_SawtoothDataBuffer(piret, puiBuffer, pstTempDACOutCtrl);
            break;
        case eDAC_WaveForm_Sine:
            vCompute_SineWaveDataBuffer(piret, puiBuffer, pstTempDACOutCtrl);
            break;        
        default:
            *piret = -1;
            break;
    }
    if(*piret != 0)
        return;
    
    *peInactiveBuffIndex = eInactiveBuffer;
    *ppuiInactiveBuffer = puiBuffer;
    *piret = 0;
}

static inline void vSet_DMAUpdate_Pending( int *piret )
{
    bool bIsPending = bIsUpdatePending();
    if(bIsPending)
    {
        *piret = -1;
        return;
    }

    *piret = 0;
    atomic_store_explicit(&stTDACOutputCodeCtrl_t.bIsUpdatePending, true, memory_order_release);
}

static inline void vClear_DMAUpdate_Pending( void )
{
    atomic_store_explicit(&stTDACOutputCodeCtrl_t.bIsUpdatePending, false, memory_order_release);
}

static inline bool bIsUpdatePending( void )
{
    bool bIsPending = atomic_load_explicit(&stTDACOutputCodeCtrl_t.bIsUpdatePending, memory_order_acquire);
    return bIsPending;
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
    vSet_ActiveBuffer(eDAC_Buffer_A, &stTDACOutputCodeCtrl_t);
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
        case eDAC_WaveForm_Sine:
            eTDACInternalMode = eDAC_InternalMode_WaveGen;
            vConfigure_DAC_SineWaveMode(&stTDACConfig_t);
            break;        
        default:
            stTDACConfig_t.bIsConfigured = false;
            FHALT("Unsupported DAC output waveform.");
            break;
    }
    printk("DAC initialized in %d mode.\n", eTDACInternalMode);
    pstConfig->bIsConfigured = stTDACConfig_t.bIsConfigured;
}

static void vConfigure_DAC_SineWaveMode(sT_DAC_Config_t *pstConfig)
{
    int iret = 0;    
    dac_config_t stDACConfig;

    if(pstConfig == NULL)
    {
        FHALT("Invalid pointer to DAC configuration structure.");
        return;
    }
    Print_Sine("Wavegen -> Sine Started...\n\r");

    sT_DAC_OutputCode_Metadata_t stTempDACOutCtrl;
    vLoad_DACOutputCtrlMetadata(&stTempDACOutCtrl, &stTDACOutputCodeCtrl_t);
    vCompute_Waveform_Params(pstConfig, &stTempDACOutCtrl, &iret);
    if(iret != 0)
    {
        FHALT("Failed to compute buffer control values for DAC sawtooth mode.");
        pstConfig->bIsConfigured = false;
        return;
    }
    vCommit_DACOutputCtrlMetadata(&stTDACOutputCodeCtrl_t, &stTempDACOutCtrl);
        
    uint32_t *puiBuffer = puiGetDACBuffer(stTDACOutputCodeCtrl_t.eCurrentBuffer, &stTDACOutputCodeCtrl_t);
    if(puiBuffer == NULL)
    {
        FHALT("Invalid Data Buffer in Sawtooth Mode.");
        pstConfig->bIsConfigured = false;
        return;        
    }
    vCompute_SineWaveDataBuffer(&iret, puiBuffer, &stTempDACOutCtrl);
    if(iret != 0)    {
        pstConfig->bIsConfigured = false;
        FHALT("Failed to compute SineWave data buffer.");
        return;
    }
    Print_Sine("Wavegen -> Parameters Configured\n\r");    

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
    Print_Sine("Wavegen -> Reference Source Configured\n\r");    

    vConfigure_FIFOWorkMode(&stDACConfig, pstConfig, &iret);
    if(iret != 0)
    {
        pstConfig->bIsConfigured = false;
        return;
    }
    Print_Sine("Wavegen -> WorkMode Configured\n\r");

    DAC_Init(DAC0, &stDACConfig);
    DAC_Enable(DAC0, true);

    vConfigure_DACTrigSrc(pstConfig, &iret);
    if(iret != 0)
    {
        pstConfig->bIsConfigured = false;
        return;
    }
    Print_Sine("Wavegen -> Trigger Source Configured\n\r");

    k_work_init(&stWorker_ParamUpdateExecute, vDAC_ParamUpdateExecute);
    pstConfig->bIsConfigured = true;       
}

void vCompute_SineWaveDataBuffer(int *piret, uint32_t *puiBuffer, const sT_DAC_OutputCode_Metadata_t *pstDACOutCtrl)
{
    k_spinlock_key_t key = k_spin_lock(&stLock_DACOutputCodeCtrl);

    if(puiBuffer == NULL || piret == NULL || pstDACOutCtrl == NULL)
    {
        FHALT("Invalid pointer to DAC configuration structure or return value pointer.");
        if(piret != NULL)
        {
            *piret = -1;
        }
        k_spin_unlock(&stLock_DACOutputCodeCtrl, key);
        return;
    }

    uint16_t uiCenterCode = (uint16_t)(((uint32_t)pstDACOutCtrl->uiMinCode +
                                        (uint32_t)pstDACOutCtrl->uiMaxCode) / 2U);
    uint16_t uiPeakCode = (uint16_t)(((uint32_t)pstDACOutCtrl->uiMaxCode -
                                      (uint32_t)pstDACOutCtrl->uiMinCode) / 2U);
    uint16_t uiNumSamples = pstDACOutCtrl->uiNumberofSamples_Period;

    if(uiNumSamples < 2U)
    {
        FHALT("Sine waveform requires at least two samples per period.");
        *piret = -1;
        k_spin_unlock(&stLock_DACOutputCodeCtrl, key);
        return;
    }

    if(uiNumSamples > (DAC_MAX_CODE_VALUE + 1U))
    {
        uiNumSamples = DAC_MAX_CODE_VALUE + 1U;
    }

    if(uiPeakCode == 0U)
    {
        FHALT("Sine waveform peak code cannot be zero.");
        *piret = -1;
        k_spin_unlock(&stLock_DACOutputCodeCtrl, key);
        return;
    }

    if(uiCenterCode < uiPeakCode)
    {
        FHALT("Sine waveform minimum output would be below zero.");
        *piret = -1;
        k_spin_unlock(&stLock_DACOutputCodeCtrl, key);
        return;
    }

    if(((uint32_t)uiCenterCode + (uint32_t)uiPeakCode) > DAC_MAX_CODE_VALUE)
    {
        FHALT("Sine waveform maximum output would exceed DAC range.");
        *piret = -1;
        k_spin_unlock(&stLock_DACOutputCodeCtrl, key);
        return;
    }

    k_spin_unlock(&stLock_DACOutputCodeCtrl, key);
    memset(puiBuffer, 0, sizeof(stTDACOutputCodeCtrl_t.uiaBuffer_A));

    for(uint16_t i = 0; i < uiNumSamples; i++)
    {
        float fPhase = (DAC_TWO_PI_F * (float)i) / (float)uiNumSamples;
        float fCode = (float)uiCenterCode + ((float)uiPeakCode * sinf(fPhase));

        if(fCode < 0.0f)
        {
            fCode = 0.0f;
        }
        else if(fCode > (float)DAC_MAX_CODE_VALUE)
        {
            fCode = (float)DAC_MAX_CODE_VALUE;
        }

        puiBuffer[i] = (uint32_t)(fCode + 0.5f);
    }

    *piret = 0;
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

    sT_DAC_OutputCode_Metadata_t stTempDACOutCtrl;
    vLoad_DACOutputCtrlMetadata(&stTempDACOutCtrl, &stTDACOutputCodeCtrl_t);
    vCompute_Waveform_Params(pstConfig, &stTempDACOutCtrl, &iret);
    if(iret != 0)
    {
        FHALT("Failed to compute buffer control values for DAC sawtooth mode.");
        pstConfig->bIsConfigured = false;
        return;
    }
    vCommit_DACOutputCtrlMetadata(&stTDACOutputCodeCtrl_t, &stTempDACOutCtrl);
    
    uint32_t *puiBuffer = puiGetDACBuffer(stTDACOutputCodeCtrl_t.eCurrentBuffer, &stTDACOutputCodeCtrl_t);
    if(puiBuffer == NULL)
    {
        FHALT("Invalid Data Buffer in Sawtooth Mode.");
        pstConfig->bIsConfigured = false;
        return;        
    }
    vCompute_SawtoothDataBuffer(&iret, puiBuffer, &stTempDACOutCtrl);
    if(iret != 0)    {
        pstConfig->bIsConfigured = false;
        FHALT("Failed to compute sawtooth data buffer.");
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

    k_work_init(&stWorker_ParamUpdateExecute, vDAC_ParamUpdateExecute);
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
            vConfigure_DACTrigSrc_CTIMER(stTrigSrcMux_t.uTrigSrc.eCTimerTrigSrc, piret,
                                         pstConfig->stOutputConfig.uOutputConfig.stWaveFormOutput.pvErrorCallback);
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

void vConfigure_DACTrigSrc_CTIMER(eDAC_TrigSrc_CTimer_t eTrigCTimer, int *piret, DACError_Callback_t vCallBackFn)
{
    ctimer_config_t stTimerConfig;
    ctimer_match_config_t stMatchConfig;
    uint32_t uiTimerClock_Hz;
    uint32_t uiMatchValue;
    uint32_t uiTimerOutputToggleFrequency_Hz;

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
    if(stTDACOutputCodeCtrl_t.uiTriggerFrequency_Hz > (UINT32_MAX / 2U))
    {
        FHALT("CTimer[%d] Trigger frequency is too high", DACHWTrigCTIMER_t.uiCTimerId);
        *piret = -1;
        return;
    }

    uiTimerOutputToggleFrequency_Hz = stTDACOutputCodeCtrl_t.uiTriggerFrequency_Hz * 2U;
    uiMatchValue = uiTimerClock_Hz / uiTimerOutputToggleFrequency_Hz;
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
    
    uint32_t *puiBuffer = puiGetDACBuffer(stTDACOutputCodeCtrl_t.eCurrentBuffer, &stTDACOutputCodeCtrl_t);
    if(puiBuffer == NULL)
    {
        FHALT("Failed to get active buffer for DAC output.");
        vDeInit_TimerConfiguration();
        *piret = -1;
        return;
    }
    
    if(!bSetup_DAC_DMA_Circular(puiBuffer, 
                       stTDACOutputCodeCtrl_t.uiNumberofSamples_Period, 
                       (uintptr_t)DAC0, vCallBackFn, vNotify_DACParameterUpdate_Callback))
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

void vCompute_Waveform_Params(sT_DAC_Config_t *pstConfig, sT_DAC_OutputCode_Metadata_t *pstDACOutCtrl, int *piret)
{
    int32_t iDiff = 0;

    if(pstConfig == NULL || piret == NULL || pstDACOutCtrl == NULL)
    {
        FHALT("Invalid pointer to DAC configuration structure or return value pointer.");
        if(piret != NULL)
        {
            *piret = -1;
        }
        return;
    }

    sT_WaveFormOutput_t *outputConfig = &pstConfig->stOutputConfig.uOutputConfig.stWaveFormOutput;
    eDAC_WaveFormType_t waveFormType = pstConfig->stOutputConfig.eWaveFormType;
    
    if(outputConfig->uiPeakVoltage_mV == 0)
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

    pstDACOutCtrl->uiMaxOutput_mV = outputConfig->uiDCOffset_mV + outputConfig->uiPeakVoltage_mV;
    switch(waveFormType)
    {
        case eDAC_WaveForm_Sawtooth:
            pstDACOutCtrl->uiMinOutput_mv = outputConfig->uiDCOffset_mV;
            break;
        case eDAC_WaveForm_Sine:
            iDiff = (int32_t)outputConfig->uiDCOffset_mV - (int32_t)outputConfig->uiPeakVoltage_mV;
            if(iDiff < 0)
            {
                FHALT("DC Offset for sine waveform cannot be less than the peak voltage.");
                *piret = -1;
                pstConfig->bIsConfigured = false;
                return;
            }
            pstDACOutCtrl->uiMinOutput_mv = (uint32_t)iDiff;
            break;
        default:
            FHALT("Unsupported waveform type for computing buffer control values.");
            *piret = -1;
            pstConfig->bIsConfigured = false;
            return;
    }
    
    if(pstDACOutCtrl->uiMaxOutput_mV > uiReferenceVoltage_mV)
    {
        FHALT("Calculated maximum output voltage exceeds reference voltage.");
        *piret = -1;
        return;
    }

    pstDACOutCtrl->uiMaxCode = uiCalculateDACCode(pstDACOutCtrl->uiMaxOutput_mV, piret);
    if(*piret != 0)    {
        pstConfig->bIsConfigured = false;
        FHALT("Failed to calculate DAC code for maximum output voltage.");
        return;
    }

    pstDACOutCtrl->uiMinCode = uiCalculateDACCode(pstDACOutCtrl->uiMinOutput_mv, piret);
    if(*piret != 0)    {
        pstConfig->bIsConfigured = false;
        FHALT("Failed to calculate DAC code for minimum output voltage.");
        return;
    }

    vCompute_DAC_OutputTiming(pstConfig, pstDACOutCtrl, piret);
    if(*piret != 0)    {
        pstConfig->bIsConfigured = false;
        FHALT("Failed to compute DAC output settling time for sawtooth mode.");
        return;
    }

    *piret = 0;
}

void vCompute_SawtoothDataBuffer(int *piret, uint32_t *puiBuffer, const sT_DAC_OutputCode_Metadata_t *pstDACOutCtrl)
{
    if(piret == NULL)
    {
        FHALT("Null Pointer reference'piret'.");
        return;
    }
    if(puiBuffer == NULL)
    {
        FHALT("Null Pointer reference'puiBuffer'.");
        *piret = -1;
        return;
    }
    if(pstDACOutCtrl == NULL)
    {
        FHALT("Null Pointer reference'pstDACOutCtrl'.");
        *piret = -1;
        return;
    }

    uint16_t uiMinCode = pstDACOutCtrl->uiMinCode;
    uint16_t uiMaxCode = pstDACOutCtrl->uiMaxCode;
    uint16_t uiNumSamples = pstDACOutCtrl->uiNumberofSamples_Period;
    uint16_t uiCodeSpan = uiMaxCode - uiMinCode;

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

    memset(puiBuffer, 0, sizeof(stTDACOutputCodeCtrl_t.uiaBuffer_A));

    for(uint16_t i = 0; i < uiNumSamples; i++)
    {
        puiBuffer[i] = (uint32_t)uiMinCode + (uint32_t)(((uint32_t)i * uiCodeSpan) / (uiNumSamples - 1U));
    }
    *piret = 0;
}

uint32_t * puiGetDACBuffer(eDAC_Buffer_t eBuffer, sT_DAC_OutputCode_Ctrl_t *pstOutputCodeCtrl)
{
    if(pstOutputCodeCtrl == NULL)
    {
        FHALT("Invalid pointer to DAC output code control structure.");
        return NULL;
    }

    switch(eBuffer)
    {
        case eDAC_Buffer_A:
            return pstOutputCodeCtrl->uiaBuffer_A;
        case eDAC_Buffer_B:
            return pstOutputCodeCtrl->uiaBuffer_B;
        default:
            FHALT("Invalid DAC buffer selection.");
            return NULL;
    }
}

eDAC_Buffer_t eGetInactiveBufferId( const sT_DAC_OutputCode_Metadata_t *pstOutputCodeCtrl )
{
    if(pstOutputCodeCtrl == NULL)
    {
        FHALT("Invalid Pointer");
        return eNUMBER_OF_DAC_BUFFERs;
    }
    switch(pstOutputCodeCtrl->eCurrentBuffer)
    {
        case eDAC_Buffer_A:
            return eDAC_Buffer_B;
        case eDAC_Buffer_B:
            return eDAC_Buffer_A;
        default:
            return eNUMBER_OF_DAC_BUFFERs;
    }
}

void vSet_ActiveBuffer(eDAC_Buffer_t eBuffer, sT_DAC_OutputCode_Ctrl_t *pstOutputCodeCtrl)
{
    if(pstOutputCodeCtrl == NULL)
    {
        FHALT("Invalid pointer to DAC output code control structure.");
        return;
    }

    switch(eBuffer)
    {
        case eDAC_Buffer_A:
        case eDAC_Buffer_B:
            pstOutputCodeCtrl->eCurrentBuffer = eBuffer;
            break;
        default:
            FHALT("Invalid DAC buffer selection.");
            break;
    }
}

static void vLoad_DACOutputCtrlMetadata(sT_DAC_OutputCode_Metadata_t *pstDest,
                                        const sT_DAC_OutputCode_Ctrl_t *pstSrc)
{
    if(pstDest == NULL || pstSrc == NULL)
    {
        FHALT("Invalid DAC output metadata pointer.");
        return;
    }

    pstDest->uiTriggerFrequency_Hz = pstSrc->uiTriggerFrequency_Hz;
    pstDest->eCurrentBuffer = pstSrc->eCurrentBuffer;
    pstDest->uiMaxOutput_mV = pstSrc->uiMaxOutput_mV;
    pstDest->uiMinOutput_mv = pstSrc->uiMinOutput_mv;
    pstDest->uiMaxCode = pstSrc->uiMaxCode;
    pstDest->uiMinCode = pstSrc->uiMinCode;
    pstDest->uiNumberofSamples_Period = pstSrc->uiNumberofSamples_Period;
    pstDest->fSettlingTime_us = pstSrc->fSettlingTime_us;
    pstDest->stTDACHWConfig = pstSrc->stTDACHWConfig;
}

static void vCommit_DACOutputCtrlMetadata(sT_DAC_OutputCode_Ctrl_t *pstDest,
                                          const sT_DAC_OutputCode_Metadata_t *pstSrc)
{
    if(pstDest == NULL || pstSrc == NULL)
    {
        FHALT("Invalid DAC output metadata pointer.");
        return;
    }

    pstDest->uiTriggerFrequency_Hz = pstSrc->uiTriggerFrequency_Hz;
    pstDest->eCurrentBuffer = pstSrc->eCurrentBuffer;
    pstDest->uiMaxOutput_mV = pstSrc->uiMaxOutput_mV;
    pstDest->uiMinOutput_mv = pstSrc->uiMinOutput_mv;
    pstDest->uiMaxCode = pstSrc->uiMaxCode;
    pstDest->uiMinCode = pstSrc->uiMinCode;
    pstDest->uiNumberofSamples_Period = pstSrc->uiNumberofSamples_Period;
    pstDest->fSettlingTime_us = pstSrc->fSettlingTime_us;
    pstDest->stTDACHWConfig = pstSrc->stTDACHWConfig;
}

void vCompute_DAC_OutputTiming(sT_DAC_Config_t *pstConfig, sT_DAC_OutputCode_Metadata_t *pstDACOutCodeCtrl, int *piret)
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
            pstDACOutCodeCtrl->fSettlingTime_us = (float)LOWER_LOWER_POWER_MODE_SETTLING_TIME_US;
            break;
        case eDAC_OutputBuff_Higher_LowPowerMode:
            pstDACOutCodeCtrl->fSettlingTime_us = (float)HIGHER_LOWER_POWER_MODE_SETTLING_TIME_US;
            break;
        default:
            FHALT("Unsupported output buffer low power mode for calculating DAC output settling time.");
            *piret = -1;
            return;
    }
    pstDACOutCodeCtrl->fSettlingTime_us += (pstDACOutCodeCtrl->fSettlingTime_us * fSETTLING_TIME_MARGIN_PERCENTAGE);
    ffreq_hz = 1.0f / (pstDACOutCodeCtrl->fSettlingTime_us * 1e-6f);
    pstDACOutCodeCtrl->uiTriggerFrequency_Hz = (uint32_t)ffreq_hz;
    pstDACOutCodeCtrl->uiNumberofSamples_Period = (uint16_t)(pstDACOutCodeCtrl->uiTriggerFrequency_Hz / 
                                                      pstConfig->stOutputConfig.uOutputConfig.stWaveFormOutput.uiFrequencyHz);
    if(pstDACOutCodeCtrl->uiNumberofSamples_Period < DAC_MIN_WAVEFORM_SAMPLES_PER_PERIOD)
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

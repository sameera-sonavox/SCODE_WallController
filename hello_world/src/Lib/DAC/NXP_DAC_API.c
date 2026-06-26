#include "../API_Usage_Definition.h"

#if defined(USE_DAC)

#include <string.h>
#include <stdatomic.h>
#include <math.h>
#include <stdlib.h>
#include <zephyr/kernel.h>

#include "fsl_clock.h"
#include "fsl_reset.h"
#include "fsl_ctimer.h"
#include "fsl_inputmux.h"
#include "fsl_inputmux_connections.h"
#include "fsl_dac.h"

#include "NXP_DAC_API.h"
#include "NXP_DAC_DMAConfig.h"
#include "../TrigSrcControl/TrigSrcControl.h"
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

#if defined(DEBUG_DAC_WAVEGEN_NOISE)
    #define Print_Noise                     printk
#else
    #define Print_Noise(...)
#endif

static uint32_t uiNoiseSeed = 0x12345678U;

typedef struct
{
    eTrigSrc_CTimer_t eTrigSource;
    inputmux_connection_t eInputMuxConnection;
} sT_DAC_CTimerTrigSource_t;

typedef struct
{
    eTrigSrcGroup_t eTrigSrcType;
    union{
        sT_DAC_CTimerTrigSource_t stTCTimerConfig;
    } stTrigSrcConfig_t;
} sT_DACHWTrigConfig_t;

typedef enum
{
    eDAC_Buffer_None = 0,
    eDAC_Buffer_A,
    eDAC_Buffer_B,
    eNUMBER_OF_DAC_BUFFERs
} eDAC_Buffer_t;

typedef enum
{
    eDAC_InternalMode_Direct = 0,
    eDAC_InternalMode_WaveGen_CTimer,
    eDAC_InternalMode_Unsupported
} eDAC_InternalMode_t;

typedef struct
{
    uint32_t *puiBuffer_A;
    uint32_t *puiBuffer_B;
    uint16_t uiSampleBufferLength;
} sT_WaveFormConfig_t;

typedef struct
{
    uint32_t uiSampleRate_S_s;    
    uint32_t uiBlockRepeatTime_us;
    sT_TCDBuffCtrl_t staTCDBuff[DMA_TCD_RING_BUFF_COUNT];
} sT_NoiseConfig_t;

typedef struct
{
    eDAC_WaveFormType_t eWaveType;
    bool bIsDACStopped;
    bool bIsDACDisabled;
    bool bIsDACPaused;
    uint32_t uiTriggerFrequency_Hz;
    eDAC_Buffer_t eCurrentBuffer;
    eT_TCDBuff_t eCurrentTCDBuffer;
    uint16_t uiMaxOutput_mV;
    uint16_t uiMinOutput_mv;
    uint16_t uiMaxCode;
    uint16_t uiMinCode;
    uint16_t uiNumberofSamples_Period;
    float fSettlingTime_us;
    _Atomic bool bIsUpdatePending;
    _Atomic bool bParamUpdateStatus;
    eDAC_InternalMode_t eTDACInternalMode;
    sT_DACHWTrigConfig_t stTDACHWConfig;

    union{
        sT_WaveFormConfig_t stTWaveConfig;
        sT_NoiseConfig_t stTNoiseConfig;
    } dacout_perWave_t;

} sT_DAC_OutputCode_Ctrl_t;

typedef struct
{
    uint32_t uiTriggerFrequency_Hz;
    uint32_t uiSampleRate_S_s;//Sample rate for White/Pink Noise
    uint32_t uiBlockRepeatTime_us;
    eDAC_Buffer_t eCurrentBuffer;
    eT_TCDBuff_t eCurrentTCDBuffer;
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
} sT_DACBuffSwap_Wave_t;

typedef struct{
    sT_DAC_OutputCode_Metadata_t stTNewBuffConfigs;
    sT_DAC_Config_t stTDACConfigTemp;
} sT_DACBuffSwap_Noise_t;

static const inputmux_connection_t caTimerInputMuxMap[eNUMBER_OF_CTIMER_TRIG_SRCs] = {
    kINPUTMUX_Ctimer0M0ToDac0Trigger,//eTrigSrc_CTIMER0_MAT0
    kINPUTMUX_Ctimer0M1ToDac0Trigger,//eTrigSrc_CTIMER0_MAT1
    kINPUTMUX_Ctimer1M0ToDac0Trigger,//eTrigSrc_CTIMER1_MAT0
    kINPUTMUX_Ctimer1M1ToDac0Trigger,//eTrigSrc_CTIMER1_MAT1
    kINPUTMUX_Ctimer2M0ToDac0Trigger,//eTrigSrc_CTIMER2_MAT0
    kINPUTMUX_Ctimer2M1ToDac0Trigger,//eTrigSrc_CTIMER2_MAT1
    kINPUTMUX_Ctimer3M0ToDac0Trigger,//eTrigSrc_CTIMER3_MAT0
    kINPUTMUX_Ctimer3M1ToDac0Trigger,//eTrigSrc_CTIMER3_MAT1
};

sT_DAC_Config_t stTDACConfig_t = {0};
sT_DAC_OutputCode_Ctrl_t stTDACOutputCodeCtrl_t = {
    .stTDACHWConfig.stTrigSrcConfig_t.stTCTimerConfig.eTrigSource = eNUMBER_OF_CTIMER_TRIG_SRCs
};
sT_DACBuffSwap_Wave_t stTBuffSwapData = {0};
sT_DACBuffSwap_Noise_t stTBuffSwapData_Noise = {0};

#define DACHWTrigCTIMER_t                   (stTDACOutputCodeCtrl_t.stTDACHWConfig.stTrigSrcConfig_t.stTCTimerConfig)
#define getNoiseBufSize()                   (stTDACOutputCodeCtrl_t.dacout_perWave_t.stTNoiseConfig.staTCDBuff[0].uiBuffer)
#define stNoiseConfigCtrl                   (stTDACOutputCodeCtrl_t.dacout_perWave_t.stTNoiseConfig)

#define IsDACConfigured()                   (stTDACConfig_t.bIsConfigured)
#define eGetDACOperationMode()              (stTDACOutputCodeCtrl_t.eTDACInternalMode)
#define vSetDACOperationMode(eOpMode)       (stTDACOutputCodeCtrl_t.eTDACInternalMode = eOpMode)
#define eGetWaveFormType()                  (stTDACConfig_t.stOutputConfig.eWaveFormType)

#define bIsDACDisabled()                    (stTDACOutputCodeCtrl_t.bIsDACDisabled)
#define vSet_DAC_Disable()                  (stTDACOutputCodeCtrl_t.bIsDACDisabled = true)
#define vClear_DAC_Disable()                (stTDACOutputCodeCtrl_t.bIsDACDisabled = false)

#define bIsDACStopped()                     (stTDACOutputCodeCtrl_t.bIsDACStopped)
#define vSet_DACStop_Flag()                 (stTDACOutputCodeCtrl_t.bIsDACStopped = true)
#define vClear_DACStop_Flag()               (stTDACOutputCodeCtrl_t.bIsDACStopped = false)

#define bIsDACPaused()                      (stTDACOutputCodeCtrl_t.bIsDACPaused)
#define vSet_DAC_Pause()                    (stTDACOutputCodeCtrl_t.bIsDACPaused = true)
#define vClear_DAC_Pause()                  (stTDACOutputCodeCtrl_t.bIsDACPaused = false)

static struct k_spinlock stLock_DACOutputCodeCtrl;
static struct k_spinlock stLock_WaveFormParamUpdate;
static struct k_spinlock stLock_TCDBufferUpdate;

static void vConfigure_DAC_DirectMode(sT_DAC_Config_t *pstConfig);
static uint32_t uiGetReferenceVoltage_mV(eDAC_RefVoltSrc_t eRefVoltSrc, int *piret);

uint32_t * puiGetDACBuffer(eDAC_Buffer_t eBuffer, sT_DAC_OutputCode_Ctrl_t *pstOutputCodeCtrl);
uint32_t * puiGetBuffer_Waveform(eDAC_Buffer_t eBuffer, sT_DAC_OutputCode_Ctrl_t *pstOutputCodeCtrl);
void vSet_ActiveBuffer(eDAC_Buffer_t eBuffer, eT_TCDBuff_t eTCDActiveBuffer, sT_DAC_OutputCode_Ctrl_t *pstOutputCodeCtrl);
void vSet_ActiveBuffer_Waveform(eDAC_Buffer_t eBuffer, sT_DAC_OutputCode_Ctrl_t *pstOutputCodeCtrl);
eDAC_Buffer_t eGetInactiveBufferId( const sT_DAC_OutputCode_Metadata_t *pstOutputCodeCtrl );
uint32_t *puiGetNextBuffer( eDAC_WaveFormType_t eWaveType, sT_DAC_OutputCode_Metadata_t *pstTempDACOutCtrl, sT_DAC_OutputCode_Ctrl_t *pstDACOutCtrl );

static uint32_t *puiGet_Active_TCDBuffer( void );
static eT_TCDBuff_t eGet_Active_TCDBufferId( void );
static eT_TCDBuff_t eGet_Free_TCDBufferId( void );
static inline eNoiseBufState_t eGet_TCDBuffState( eT_TCDBuff_t eBuffId );
static void vSet_Active_TCDBuffer(eT_TCDBuff_t eBufferId);
static void vSet_TCDBuffer_Free(eT_TCDBuff_t eBufferId);
static void vInit_TCDBuffers( void );
static bool bTry_Claim_TCDBuffer(eT_TCDBuff_t eBuffId, eNoiseBufState_t eNewState);
static void vSet_TCDBuffer_State(eT_TCDBuff_t eBufferId, eNoiseBufState_t eState);
static bool bTry_Claim_TCDBuffer_ForCPUFill(eT_TCDBuff_t eBuffId);

static void vSet_WaveformType(sT_DAC_Config_t *pstConfig, sT_DAC_OutputCode_Ctrl_t *pstDACOutCodeCtrl, int *piret);
static void vConfigure_DAC_SawtoothMode(sT_DAC_Config_t *pstConfig);
void vCompute_Waveform_Params(sT_DAC_Config_t *pstConfig, sT_DAC_OutputCode_Metadata_t *pstDACOutCtrl, int *piret);
void vCompute_SawtoothDataBuffer(int *piret, uint32_t *puiBuffer, const sT_DAC_OutputCode_Metadata_t *pstDACOutCtrl);
static void vAllocate_MemoryForSampleBuffers(sT_DAC_OutputCode_Ctrl_t *pstDACOutCtrl, int *piret);
static void vFree_MemoryFor_SampleBuffers(sT_DAC_OutputCode_Ctrl_t *pstDACOutCtrl);

static void vConfigure_DAC_SineWaveMode(sT_DAC_Config_t *pstConfig);
void vCompute_SineWaveDataBuffer(int *piret, uint32_t *puiBuffer, const sT_DAC_OutputCode_Metadata_t *pstDACOutCtrl);

static void vConfigure_DAC_WhiteNoiseMode(sT_DAC_Config_t *pstConfig);
void vFill_TCD_Buffers( int *piret, const sT_DAC_OutputCode_Metadata_t *pstDACOutCtrl );
static uint32_t uiRandom_Range(uint32_t uiMin, uint32_t uiMax);

void vCompute_DAC_OutputTiming(sT_DAC_Config_t *pstConfig, sT_DAC_OutputCode_Metadata_t *pstDACOutCodeCtrl, int *piret);
void vConfigure_FIFOWorkMode(dac_config_t *pstDACConfig, sT_DAC_Config_t *pstConfig, int *piret);
void vConfigure_FIFO_NormalMode(dac_config_t *pstDACConfig, sT_DAC_Config_t *pstConfig, int *piret);
static void vLoad_DACOutputCtrlMetadata(sT_DAC_OutputCode_Metadata_t *pstDest,
                                        const sT_DAC_OutputCode_Ctrl_t *pstSrc);
static void vCommit_DACOutputCtrlMetadata(sT_DAC_OutputCode_Ctrl_t *pstDest,
                                          const sT_DAC_OutputCode_Metadata_t *pstSrc);

void vConfigure_DAC_Structs_ForWaveFromParam_Update(sT_DAC_OutputCode_Metadata_t *pstTempDACOutCtrl, 
                                           sT_DAC_Config_t *pstDacConfig, 
                                           eDAC_Buffer_t *peInactiveBuffIndex,
                                           uint32_t **ppuiInactiveBuffer, 
                                           int *piret);
void vConfigure_DAC_Structs_ForNoiseGenParam_Update(sT_DAC_OutputCode_Metadata_t *pstTempDACOutCtrl, 
                                           sT_DAC_Config_t *pstDacConfig,  
                                           int *piret);
void vUpdate_WaveGenFreq_WithCurrentBuffers(sT_DAC_OutputCode_Metadata_t *pstTempDACOutCtrl, 
                                           sT_DAC_Config_t *pstDacConfig, 
                                           eDAC_Buffer_t *peInactiveBuffIndex,
                                           uint32_t **ppuiInactiveBuffer, 
                                           int *piret);
void vUpdate_WaveGenFreq_WithNewBufferSizes(sT_DAC_OutputCode_Metadata_t *pstTempDACOut, 
                                           sT_DAC_Config_t *pstTempDACConfig);                                           

static inline void vSet_DMAUpdate_Pending( int *piret );
static inline void vClear_DMAUpdate_Pending( void );
static inline bool bIsUpdatePending( void );
static void vNotify_DACParameterUpdate_Callback(bool status, void *pUserData);
static inline void vSet_DMAUpdate_Status( bool status );
static inline void vClear_DMAUpdate_Status( void );
static inline bool bIs_DMAUpdate_Success( void );

void vConfigure_DACTrigSrc_CTIMER(eTrigSrc_CTimer_t eTrigCTimer, int *piret, DACError_Callback_t vCallBackFn);
void vConfigure_DACTrigSrc(sT_DAC_Config_t *pstConfig, int *piret);
static void vStart_CTimer( void );
static void vStop_CTimer( void );

void vDeInit_CTimer_Configuration(void);
void vAssign_CTimer_ToConfig(eTrigSrc_CTimer_t eTrigCTimer, int *piret);

static bool bConfigure_ReferenceSource(sT_DAC_Config_t *pstConfig, dac_config_t *pstDACConfig);
static uint32_t uiCalculateDACCode(uint16_t uiOutput_mV, int *piret);
static void vConfigure_InternalRoute(sT_DAC_Config_t *pstConfig, dac_config_t *pstDACConfig);
static bool bConfigure_RouteToADC(sT_DAC_Config_t *pstConfig, dac_config_t *pstDACConfig);
static bool bConfigure_RouteToCMP(sT_DAC_Config_t *pstConfig, dac_config_t *pstDACConfig);

static struct k_work stWorker_ParamUpdateExecute;
static void vDAC_ParamUpdateExecute(struct k_work *work);

static struct k_work stWorker_NoiseBlockRefill;
static struct k_work_sync stSync_NoiseBlockRefill;
static void vDAC_NoiseBlockRefill(struct k_work *work);
void vInit_TCDRefill_Worker( eDAC_WaveFormType_t eWaveType );
void vCancel_TCDWorker( eDAC_WaveFormType_t eWaveType );

static void vDisable_DACConfig_with_CTimer( void );
static void vForce_DAC_FIFO_Output(uint32_t uiDACCode);

void vWaveGen_VolumeUpdate(uint16_t uiPeakVolt_mV);
void vNoiseGen_VolumeUpdate(uint16_t uiPeakVolt_mV);
void vWaveGen_FrequencyUpdate(uint32_t uiFreq_Hz);

void vNoiseGen_FrequencyUpdate(uint32_t uiFreq_Hz);
static void vUpdate_TrigSrcFrequency_ForNoiseGen(sT_DAC_Config_t *pstConfig, sT_DAC_OutputCode_Metadata_t *pstTempDACOut, int *piret);
static void vUpdate_CTimer_TrigFreq_ForNoiseGen(eTrigSrc_CTimer_t cTimerTrigSrc, sT_DAC_OutputCode_Metadata_t *pstTempDACOut, int *piret);

void vStop_WaveGenerator( void );
void vStop_WaveFormGenerator( void );
void vStop_NoiseGenerator( void );
void vRestart_At_CTimerConfig( void );
void vSet_DefaultDACOutput_WithWaveGen( eDAC_DefaultOutLevel_t eDefaultLevel, uint32_t uiCustomVal_mV );

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

void vUpdate_WaveForm_Frequency(uint32_t uiFreq_Hz)
{
    if(!IsDACConfigured() || (eGetDACOperationMode() != eDAC_InternalMode_WaveGen_CTimer))
    {
        FHALT("DAC is not properly configured for waveform generation. Cannot update waveform volume.");
        return;
    }
    if(bIsDACDisabled() || bIsDACStopped() || bIsDACPaused())
    {
        FHALT("Wavegen is not running. Start/resume before updating waveform parameters.");
        return;
    }    
    if(bIsUpdatePending())
        return;

    eDAC_WaveFormType_t eWaveType = eGetWaveFormType();
    switch(eWaveType)
    {
        case eDAC_WaveForm_Triangle:
        case eDAC_WaveForm_Sawtooth:
        case eDAC_WaveForm_Sine:
            vWaveGen_FrequencyUpdate(uiFreq_Hz);
            break;
        case eDAC_WaveForm_WhiteNoise:
        case eDAC_WaveForm_PinkNoise:
            vNoiseGen_FrequencyUpdate(uiFreq_Hz);
            break;
        default:
            FHALT("Invalid WaveType @Type: %d", eWaveType);
            break;
    }
    
}

void vNoiseGen_FrequencyUpdate(uint32_t uiFreq_Hz)
{
    int iret = 0;

    k_spinlock_key_t key = k_spin_lock(&stLock_WaveFormParamUpdate);

    memcpy(&stTBuffSwapData_Noise.stTDACConfigTemp, &stTDACConfig_t, sizeof(sT_DAC_Config_t));
    vLoad_DACOutputCtrlMetadata(&stTBuffSwapData_Noise.stTNewBuffConfigs, &stTDACOutputCodeCtrl_t);

    sT_DAC_OutputCode_Metadata_t *pstTempDACOut = &stTBuffSwapData_Noise.stTNewBuffConfigs;
    sT_DAC_Config_t *pstTempDACConfig = &stTBuffSwapData_Noise.stTDACConfigTemp;
    sT_WaveFormOutput_t *pstWaveOutput = &pstTempDACConfig->stOutputConfig.uOutputConfig.stWaveFormOutput;

    pstWaveOutput->uiFrequencyHz = uiFreq_Hz;

    vConfigure_DAC_Structs_ForNoiseGenParam_Update(pstTempDACOut, pstTempDACConfig, &iret);
    if(iret != 0)
    {
        k_spin_unlock(&stLock_WaveFormParamUpdate, key);
        return;
    }

    vSet_DMAUpdate_Pending(&iret);
    if(iret != 0)
    {
        k_spin_unlock(&stLock_WaveFormParamUpdate, key);
        vClear_DMAUpdate_Pending();
        return;
    }
    k_spin_unlock(&stLock_WaveFormParamUpdate, key); 

    vUpdate_TrigSrcFrequency_ForNoiseGen(pstTempDACConfig, pstTempDACOut, &iret);
    if(iret != 0)
    {
        vClear_DMAUpdate_Pending();
        return;        
    }

    vCommit_DACOutputCtrlMetadata(&stTDACOutputCodeCtrl_t, pstTempDACOut);
    vClear_DMAUpdate_Pending();
}

static void vUpdate_TrigSrcFrequency_ForNoiseGen(sT_DAC_Config_t *pstConfig, sT_DAC_OutputCode_Metadata_t *pstTempDACOut, int *piret)
{
    #define stTrigSrcMux_t      (pstConfig->stOutputConfig.uOutputConfig.stWaveFormOutput.stTHWTrigSrc)

    switch(stTrigSrcMux_t.eTrigSrcGroup)
    {
        case eTrigSrcGroup_CTIMER:
            vSetDACOperationMode(eDAC_InternalMode_WaveGen_CTimer);
            vUpdate_CTimer_TrigFreq_ForNoiseGen(stTrigSrcMux_t.uTrigSrc.eCTimerTrigSrc, pstTempDACOut, piret);
            break;
        case eTrigSrcGroup_LPTIMER:
            break;
        case eTrigSrcGroup_AOI:
            break;
        case eTrigSrcGroup_GPIO:
            break;
        case eTrigSrcGroup_CPU:
            break;
        case eTrigSrcGroup_ADC:
            break;
        case eTrigSrcGroup_None:
        default:
            FHALT("Invalid HW Trigger Configuration\n\r");
            *piret = -1;
            return;
    }
    
    if(*piret != 0)
        return;
}

static void vUpdate_CTimer_TrigFreq_ForNoiseGen(eTrigSrc_CTimer_t cTimerTrigSrc, sT_DAC_OutputCode_Metadata_t *pstTempDACOut, int *piret)
{
    ARG_UNUSED(cTimerTrigSrc);

    if(!bTrigSrc_UpdateCTimerFrequency(DACHWTrigCTIMER_t.eTrigSource,
                                       eTrigConsumer_DAC0_WaveGen,
                                       pstTempDACOut->uiTriggerFrequency_Hz))
    {
        FHALT("DAC exclusive CTIMER trigger frequency update failed @Freq: %d",
              pstTempDACOut->uiTriggerFrequency_Hz);
        *piret = -1;
        return;
    }
    *piret = 0;
}

void vWaveGen_FrequencyUpdate(uint32_t uiFreq_Hz)
{
    int iret = 0;
    if(uiFreq_Hz == 0)
    {
        FHALT("Frequency cannot be zero");
        return;
    }

    k_spinlock_key_t key = k_spin_lock(&stLock_WaveFormParamUpdate);

    uint32_t *puiInactiveBuffer = NULL;

    memcpy(&stTBuffSwapData.stTDACConfigTemp, &stTDACConfig_t, sizeof(sT_DAC_Config_t));
    vLoad_DACOutputCtrlMetadata(&stTBuffSwapData.stTNewBuffConfigs, &stTDACOutputCodeCtrl_t);
    stTBuffSwapData.eReqBuffSwapId = eNUMBER_OF_DAC_BUFFERs;

    sT_DAC_OutputCode_Metadata_t *pstTempDACOut = &stTBuffSwapData.stTNewBuffConfigs;
    sT_DAC_Config_t *pstTempDACConfig = &stTBuffSwapData.stTDACConfigTemp;

    sT_WaveFormOutput_t *pstWaveOutput = &pstTempDACConfig->stOutputConfig.uOutputConfig.stWaveFormOutput;
    pstWaveOutput->uiFrequencyHz = uiFreq_Hz;

    uint32_t uiSamplesPerPeriod = (uint32_t)(stTDACOutputCodeCtrl_t.uiTriggerFrequency_Hz / uiFreq_Hz);
    if(uiSamplesPerPeriod <= stTDACOutputCodeCtrl_t.dacout_perWave_t.stTWaveConfig.uiSampleBufferLength)
    {
        vUpdate_WaveGenFreq_WithCurrentBuffers(pstTempDACOut, pstTempDACConfig, &stTBuffSwapData.eReqBuffSwapId, &puiInactiveBuffer, &iret);
    }
    else
    {
        vUpdate_WaveGenFreq_WithNewBufferSizes(pstTempDACOut, pstTempDACConfig);
    }
    
    k_spin_unlock(&stLock_WaveFormParamUpdate, key); 
}

void vUpdate_WaveGenFreq_WithNewBufferSizes(sT_DAC_OutputCode_Metadata_t *pstTempDACOut, 
                                           sT_DAC_Config_t *pstTempDACConfig)
{
    int iret = 0;
    eDAC_WaveFormType_t eWaveType = pstTempDACConfig->stOutputConfig.eWaveFormType;
    DACError_Callback_t pvErrorCallback = pstTempDACConfig->stOutputConfig.uOutputConfig.stWaveFormOutput.pvErrorCallback;

    vCompute_Waveform_Params(pstTempDACConfig, pstTempDACOut, &iret);
    if(iret != 0)
    {
        FHALT("Failed to compute waveform parameters for frequency update");
        return;
    }
    pstTempDACOut->eCurrentBuffer = eDAC_Buffer_A;

    vStop_CTimer();
    vDisable_DAC_DMA_Circular();

    memcpy(&stTDACConfig_t, pstTempDACConfig, sizeof(sT_DAC_Config_t));
    vCommit_DACOutputCtrlMetadata(&stTDACOutputCodeCtrl_t, pstTempDACOut);

    vAllocate_MemoryForSampleBuffers(&stTDACOutputCodeCtrl_t, &iret);
    if(iret != 0)
    {
        FHALT("Failed to allocate larger waveform buffers");
        return;
    }

    vSet_ActiveBuffer(eDAC_Buffer_A, eNUMBER_OF_BUFFERs, &stTDACOutputCodeCtrl_t);

    uint32_t *puiBuffer = puiGetDACBuffer(stTDACOutputCodeCtrl_t.eCurrentBuffer,
                                          &stTDACOutputCodeCtrl_t);
    if(puiBuffer == NULL)
    {
        FHALT("Failed to get waveform buffer after frequency resize");
        return;
    }

    switch(eWaveType)
    {
        case eDAC_WaveForm_Sawtooth:
            vCompute_SawtoothDataBuffer(&iret, puiBuffer, pstTempDACOut);
            break;

        case eDAC_WaveForm_Sine:
            vCompute_SineWaveDataBuffer(&iret, puiBuffer, pstTempDACOut);
            break;

        default:
            iret = -1;
            break;
    }

    if(iret != 0)
    {
        FHALT("Failed to rebuild waveform buffer after frequency resize");
        return;
    }

    DAC_SetReset(DAC0, kDAC_ResetFIFO);
    DAC_ClearReset(DAC0, kDAC_ResetFIFO);
    DAC_ClearStatusFlags(DAC0,
                         kDAC_FIFOOverflowFlag | kDAC_FIFOUnderflowFlag);
    DAC0->FCR = LPDAC_FCR_WML(4U);
    DAC0->GCR = (DAC0->GCR | LPDAC_GCR_FIFOEN_MASK) & ~LPDAC_GCR_TRGSEL_MASK;

    if(!bSetup_DAC_DMA_Circular(puiBuffer,
                                stTDACOutputCodeCtrl_t.uiNumberofSamples_Period,
                                eWaveType,
                                stTDACOutputCodeCtrl_t.eCurrentTCDBuffer,
                                (uintptr_t)DAC0,
                                pvErrorCallback,
                                vNotify_DACParameterUpdate_Callback))
    {
        FHALT("Failed to rebuild DAC DMA after frequency resize");
        return;
    }

    vStart_CTimer();   
    
}


void vUpdate_WaveGenFreq_WithCurrentBuffers(sT_DAC_OutputCode_Metadata_t *pstTempDACOutCtrl, 
                                           sT_DAC_Config_t *pstDacConfig, 
                                           eDAC_Buffer_t *peInactiveBuffIndex,
                                           uint32_t **ppuiInactiveBuffer, 
                                           int *piret)
{
    vConfigure_DAC_Structs_ForWaveFromParam_Update(pstTempDACOutCtrl, pstDacConfig, peInactiveBuffIndex, ppuiInactiveBuffer, piret);
    if(*piret != 0)
    {
        return;
    }

    vSet_DMAUpdate_Pending(piret);
    if(*piret != 0)
    {
        return;        
    }
    if(!bRequest_DMA_BufferSwap(*ppuiInactiveBuffer, 
                            pstTempDACOutCtrl->uiNumberofSamples_Period,
                            (uint8_t)stTBuffSwapData.eReqBuffSwapId))
    {
        *piret = -1;
        vClear_DMAUpdate_Pending();
        FHALT("Failed to switch DAC DMA buffer.");
        return;        
    }
    *piret = 0;
}

void vUpdate_WaveForm_Volume(uint16_t uiPeakVolt_mV)
{
    if(!IsDACConfigured() || (eGetDACOperationMode() != eDAC_InternalMode_WaveGen_CTimer))
    {
        FHALT("DAC is not properly configured for waveform generation. Cannot update waveform volume.");
        return;
    }
    if(bIsDACDisabled() || bIsDACStopped() || bIsDACPaused())
    {
        FHALT("Wavegen is not running. Start/resume before updating waveform parameters.");
        return;
    }

    if(bIsUpdatePending())
        return;

    eDAC_WaveFormType_t eWaveType = eGetWaveFormType();

    switch (eWaveType)
    {
        case eDAC_WaveForm_Triangle:
        case eDAC_WaveForm_Sawtooth:
        case eDAC_WaveForm_Sine:
            vWaveGen_VolumeUpdate(uiPeakVolt_mV);
            break;
        case eDAC_WaveForm_WhiteNoise:
        case eDAC_WaveForm_PinkNoise:
            vNoiseGen_VolumeUpdate(uiPeakVolt_mV);
            break;
        default:
            FHALT("Invalid Waveform Type @Type: %d", eWaveType);
            break;
    } 

}

void vNoiseGen_VolumeUpdate(uint16_t uiPeakVolt_mV)
{
    int iret = 0;

    k_spinlock_key_t key = k_spin_lock(&stLock_WaveFormParamUpdate);

    memcpy(&stTBuffSwapData_Noise.stTDACConfigTemp, &stTDACConfig_t, sizeof(sT_DAC_Config_t));
    vLoad_DACOutputCtrlMetadata(&stTBuffSwapData_Noise.stTNewBuffConfigs, &stTDACOutputCodeCtrl_t);

    sT_DAC_OutputCode_Metadata_t *pstTempDACOut = &stTBuffSwapData_Noise.stTNewBuffConfigs;
    sT_DAC_Config_t *pstTempDACConfig = &stTBuffSwapData_Noise.stTDACConfigTemp;
    sT_WaveFormOutput_t *pstWaveOutput = &pstTempDACConfig->stOutputConfig.uOutputConfig.stWaveFormOutput;
    
    pstWaveOutput->uiPeakVoltage_mV = uiPeakVolt_mV;
    pstWaveOutput->uiDCOffset_mV = pstTempDACOut->uiMinOutput_mv + uiPeakVolt_mV;

    vConfigure_DAC_Structs_ForNoiseGenParam_Update(pstTempDACOut, pstTempDACConfig, &iret);
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
    vCommit_DACOutputCtrlMetadata(&stTDACOutputCodeCtrl_t, pstTempDACOut);
    k_spin_unlock(&stLock_WaveFormParamUpdate, key);
    
    for(uint8_t i = eTCDBuff_0; i < DMA_TCD_RING_BUFF_COUNT; i++)
    {

        if(!bTry_Claim_TCDBuffer_ForCPUFill((eT_TCDBuff_t)i))
            continue;

        uint32_t *puiBuffer = stNoiseConfigCtrl.staTCDBuff[i].uiBuffer;
        memset(puiBuffer, 0, sizeof(stNoiseConfigCtrl.staTCDBuff[i].uiBuffer));

        for(uint16_t j = 0; j < stTDACOutputCodeCtrl_t.uiNumberofSamples_Period; j++)
        {
            puiBuffer[j] = uiRandom_Range(stTDACOutputCodeCtrl_t.uiMinCode, stTDACOutputCodeCtrl_t.uiMaxCode);
        }
        vSet_TCDBuffer_State(i, eNoiseBuf_Ready);        
    }
    
    vClear_DMAUpdate_Pending();
}

void vConfigure_DAC_Structs_ForNoiseGenParam_Update(sT_DAC_OutputCode_Metadata_t *pstTempDACOutCtrl, 
                                           sT_DAC_Config_t *pstDacConfig,  
                                           int *piret)
{
    if(piret == NULL)
    {
        FHALT("Null Pointer reference for the return value");
        return;
    }
    if(pstTempDACOutCtrl == NULL || pstDacConfig == NULL)
    {
        FHALT("NULL Pointer reference");
        *piret = -1;
        return;
    }

    vCompute_Waveform_Params(pstDacConfig, pstTempDACOutCtrl, piret);
    if(*piret != 0)
    {
        FHALT("Failed to compute buffer control values for DAC sawtooth mode.");
        return;
    }

    *piret = 0;
}

void vWaveGen_VolumeUpdate(uint16_t uiPeakVolt_mV)
{
    int iret = 0;
    k_spinlock_key_t key = k_spin_lock(&stLock_WaveFormParamUpdate);

    uint32_t *puiInactiveBuffer = NULL;
    memcpy(&stTBuffSwapData.stTDACConfigTemp, &stTDACConfig_t, sizeof(sT_DAC_Config_t));
    vLoad_DACOutputCtrlMetadata(&stTBuffSwapData.stTNewBuffConfigs, &stTDACOutputCodeCtrl_t);
    stTBuffSwapData.eReqBuffSwapId = eNUMBER_OF_DAC_BUFFERs;

    sT_DAC_OutputCode_Metadata_t *pstTempDACOut = &stTBuffSwapData.stTNewBuffConfigs;
    sT_DAC_Config_t *pstTempDACConfig = &stTBuffSwapData.stTDACConfigTemp;

    sT_WaveFormOutput_t *pstWaveOutput = &pstTempDACConfig->stOutputConfig.uOutputConfig.stWaveFormOutput;
    pstWaveOutput->uiPeakVoltage_mV = uiPeakVolt_mV;
    switch(pstTempDACConfig->stOutputConfig.eWaveFormType)
    {
        case eDAC_WaveForm_Sawtooth:
            pstWaveOutput->uiDCOffset_mV = pstTempDACOut->uiMinOutput_mv;
            break;
        case eDAC_WaveForm_Sine:
            pstWaveOutput->uiDCOffset_mV = pstTempDACOut->uiMinOutput_mv + uiPeakVolt_mV;
            break;
        default:
            break;
    }

    vConfigure_DAC_Structs_ForWaveFromParam_Update(pstTempDACOut, pstTempDACConfig, &stTBuffSwapData.eReqBuffSwapId, &puiInactiveBuffer, &iret);
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
    //printf("DAC Update Status : %d\n\r", status);
    k_spin_unlock(&stLock_WaveFormParamUpdate, key);
}

void vConfigure_DAC_Structs_ForWaveFromParam_Update(sT_DAC_OutputCode_Metadata_t *pstTempDACOutCtrl, 
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
    
    uint32_t *puiBuffer = puiGetNextBuffer(eCurrentWaveType, pstTempDACOutCtrl, &stTDACOutputCodeCtrl_t);
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

    eDAC_Buffer_t eInactiveBuffer = eGetInactiveBufferId(pstTempDACOutCtrl);
    *peInactiveBuffIndex = eInactiveBuffer;
    *ppuiInactiveBuffer = puiBuffer;
    *piret = 0;
}

uint32_t *puiGetNextBuffer( eDAC_WaveFormType_t eWaveType, sT_DAC_OutputCode_Metadata_t *pstTempDACOutCtrl, sT_DAC_OutputCode_Ctrl_t *pstDACOutCtrl )
{
    switch(eWaveType)
    {
        case eDAC_WaveForm_Triangle:
        case eDAC_WaveForm_Sawtooth:
        case eDAC_WaveForm_Sine:
            eDAC_Buffer_t eInactiveBuffer = eGetInactiveBufferId(pstTempDACOutCtrl);
            if(eInactiveBuffer == eNUMBER_OF_DAC_BUFFERs)
            {
                return NULL;
            }
            return puiGetDACBuffer(eInactiveBuffer, pstDACOutCtrl);
            break;
        case eDAC_WaveForm_WhiteNoise:
        case eDAC_WaveForm_PinkNoise:
            return NULL;
        default:
            FHALT("");
            return NULL;        
    }
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

    if(DACHWTrigCTIMER_t.eTrigSource < eNUMBER_OF_CTIMER_TRIG_SRCs)
    {
        pstConfig->bIsConfigured = false;
        FHALT("Disable the active DAC waveform generator before reconfiguring it.");
        return;
    }

    memcpy(&stTDACConfig_t, pstConfig, sizeof(sT_DAC_Config_t));
    stTDACConfig_t.bIsConfigured = false;
    vClear_DAC_Disable();
    vClear_DACStop_Flag();
    vClear_DAC_Pause();

    vSetDACOperationMode(eDAC_InternalMode_Unsupported);

    switch (stTDACConfig_t.stOutputConfig.eWaveFormType)
    {
        case eDAC_WaveForm_DC:
            vSetDACOperationMode(eDAC_InternalMode_Direct);
            vConfigure_DAC_DirectMode(&stTDACConfig_t);
            break;
        case eDAC_WaveForm_Sawtooth:
            vConfigure_DAC_SawtoothMode(&stTDACConfig_t);
            break;
        case eDAC_WaveForm_Sine:
            vConfigure_DAC_SineWaveMode(&stTDACConfig_t);
            break;
        case eDAC_WaveForm_WhiteNoise:
            vConfigure_DAC_WhiteNoiseMode(&stTDACConfig_t);
            break;      
        default:
            stTDACConfig_t.bIsConfigured = false;
            FHALT("Unsupported DAC output waveform.");
            break;
    }
    printk("DAC initialized in %d mode.\n", eGetDACOperationMode());
    pstConfig->bIsConfigured = stTDACConfig_t.bIsConfigured;
}

static void vConfigure_DAC_WhiteNoiseMode(sT_DAC_Config_t *pstConfig)
{
    int iret = 0;    
    dac_config_t stDACConfig;

    if(pstConfig == NULL)
    {
        FHALT("Invalid pointer to DAC configuration structure.");
        return;
    }
    
    Print_Sine("Wavegen -> Pink Noise Started...\n\r");

    vSet_WaveformType(pstConfig, &stTDACOutputCodeCtrl_t, &iret);
    if(iret != 0)
    {
        return;
    }

    vInit_TCDBuffers();
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

    vFill_TCD_Buffers(&iret, &stTempDACOutCtrl);
    if(iret != 0)
    {
        pstConfig->bIsConfigured = false;
        return;        
    }
    vSet_ActiveBuffer(eDAC_Buffer_None, eTCDBuff_0, &stTDACOutputCodeCtrl_t);

    Print_Noise("Wavegen -> Parameters Configured\n\r");

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
    Print_Noise("Wavegen -> Reference Source Configured\n\r");

    vConfigure_FIFOWorkMode(&stDACConfig, pstConfig, &iret);
    if(iret != 0)
    {
        pstConfig->bIsConfigured = false;
        return;
    }
    Print_Noise("Wavegen -> WorkMode Configured\n\r");

    DAC_Init(DAC0, &stDACConfig);
    DAC_Enable(DAC0, true);

    vConfigure_DACTrigSrc(pstConfig, &iret);
    if(iret != 0)
    {
        pstConfig->bIsConfigured = false;
        return;
    }
    Print_Sine("Wavegen -> Trigger Source Configured\n\r");
    pstConfig->bIsConfigured = true;      
}

void vFill_TCD_Buffers( int *piret, const sT_DAC_OutputCode_Metadata_t *pstDACOutCtrl )
{
    if(piret == NULL)
    {
        FHALT("Null Pointer Reference");
        return;
    }
    if(pstDACOutCtrl == NULL)
    {
        FHALT("Null Pointer Reference");
        *piret = -1;
        return;        
    }

    k_spinlock_key_t key = k_spin_lock(&stLock_DACOutputCodeCtrl);

    for(uint8_t i = eTCDBuff_0; i < DMA_TCD_RING_BUFF_COUNT; i++)
    {
        uint32_t *puiBuffer = stNoiseConfigCtrl.staTCDBuff[i].uiBuffer;
        memset(puiBuffer, 0, sizeof(stNoiseConfigCtrl.staTCDBuff[i].uiBuffer));

        for(uint16_t i = 0; i < pstDACOutCtrl->uiNumberofSamples_Period; i++)
        {
            puiBuffer[i] = uiRandom_Range(pstDACOutCtrl->uiMinCode, pstDACOutCtrl->uiMaxCode);
        }
        stNoiseConfigCtrl.staTCDBuff[i].eBuffState = eNoiseBuf_Ready;
    }

    k_spin_unlock(&stLock_DACOutputCodeCtrl, key);
    *piret = 0;
}

void vNotify_DMANoiseBuffer_Completed( eT_TCDBuff_t eCompletedBuff )
{
    if(bIsDACStopped())
    {
        return;
    }

    vSet_TCDBuffer_Free(eCompletedBuff);
    k_work_submit(&stWorker_NoiseBlockRefill);
}

static void vDAC_NoiseBlockRefill(struct k_work *work)
{
    ARG_UNUSED(work);

    if(bIsDACStopped())
    {
        return;
    }
    
    while(true)
    {
        if(bIsDACStopped())
        {
            break;
        }

        eT_TCDBuff_t eBuffId = eGet_Free_TCDBufferId();
        if(eBuffId >= DMA_TCD_RING_BUFF_COUNT)
        {
            break;
        }
                
        vSet_TCDBuffer_State(eBuffId, eNoiseBuf_Filling);
        uint32_t *puiBuffer = stNoiseConfigCtrl.staTCDBuff[eBuffId].uiBuffer;
        memset(puiBuffer, 0, sizeof(stNoiseConfigCtrl.staTCDBuff[eBuffId].uiBuffer));

        for(uint16_t i = 0; i < stTDACOutputCodeCtrl_t.uiNumberofSamples_Period; i++)
        {
            puiBuffer[i] = uiRandom_Range(stTDACOutputCodeCtrl_t.uiMinCode, stTDACOutputCodeCtrl_t.uiMaxCode);
        }
        vSet_TCDBuffer_State(eBuffId, eNoiseBuf_Ready);
    }
}


static uint32_t uiRandom_Range(uint32_t uiMin, uint32_t uiMax)
{
    uiNoiseSeed = (1664525U * uiNoiseSeed) + 1013904223U;

    uint32_t uiRange = uiMax - uiMin + 1U;
    return uiMin + (uiNoiseSeed % uiRange);
}

static void vSet_WaveformType(sT_DAC_Config_t *pstConfig, sT_DAC_OutputCode_Ctrl_t *pstDACOutCodeCtrl, int *piret)
{
    if(piret == NULL)
    {
        FHALT("NULL Pointer Reference");
        return;
    }
    if(pstConfig == NULL || pstDACOutCodeCtrl == NULL)
    {
        FHALT("NULL Pointer Reference");
        *piret = -1;
        return;
    }

    if(pstConfig->stOutputConfig.eWaveFormType == eDAC_WaveForm_DC || 
       pstConfig->stOutputConfig.eWaveFormType >= eNUMBER_OF_DAC_WAVEFORMs)
    {
        FHALT("Invalid Waveform Type @Type: %d", pstConfig->stOutputConfig.eWaveFormType);
        *piret = -1;
        return;        
    }

    pstDACOutCodeCtrl->eWaveType = pstConfig->stOutputConfig.eWaveFormType;
    *piret = 0;
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

    vSet_WaveformType(pstConfig, &stTDACOutputCodeCtrl_t, &iret);
    if(iret != 0)
    {
        return;
    }
    vSet_ActiveBuffer(eDAC_Buffer_A, eNUMBER_OF_BUFFERs, &stTDACOutputCodeCtrl_t);

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

    vAllocate_MemoryForSampleBuffers(&stTDACOutputCodeCtrl_t, &iret);
    if(iret != 0)
    {
        vFree_MemoryFor_SampleBuffers(&stTDACOutputCodeCtrl_t);
        FHALT("Failed to callocate memory for the buffers.");
        pstConfig->bIsConfigured = false;
        return;        
    }    
        
    uint32_t *puiBuffer = puiGetDACBuffer(stTDACOutputCodeCtrl_t.eCurrentBuffer, &stTDACOutputCodeCtrl_t);
    if(puiBuffer == NULL)
    {
        FHALT("Invalid Data Buffer in Sawtooth Mode.");
        vFree_MemoryFor_SampleBuffers(&stTDACOutputCodeCtrl_t);
        pstConfig->bIsConfigured = false;
        return;        
    }
    vCompute_SineWaveDataBuffer(&iret, puiBuffer, &stTempDACOutCtrl);
    if(iret != 0)    
    {
        vFree_MemoryFor_SampleBuffers(&stTDACOutputCodeCtrl_t);
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
        vFree_MemoryFor_SampleBuffers(&stTDACOutputCodeCtrl_t);
        FHALT("Failed to configure DAC reference voltage source.");
        pstConfig->bIsConfigured = false;
        return;
    }
    Print_Sine("Wavegen -> Reference Source Configured\n\r");    

    vConfigure_FIFOWorkMode(&stDACConfig, pstConfig, &iret);
    if(iret != 0)
    {
        vFree_MemoryFor_SampleBuffers(&stTDACOutputCodeCtrl_t);
        pstConfig->bIsConfigured = false;
        return;
    }
    Print_Sine("Wavegen -> WorkMode Configured\n\r");

    DAC_Init(DAC0, &stDACConfig);
    DAC_Enable(DAC0, true);

    vConfigure_DACTrigSrc(pstConfig, &iret);
    if(iret != 0)
    {
        vFree_MemoryFor_SampleBuffers(&stTDACOutputCodeCtrl_t);
        pstConfig->bIsConfigured = false;
        return;
    }
    Print_Sine("Wavegen -> Trigger Source Configured\n\r");

    k_work_init(&stWorker_ParamUpdateExecute, vDAC_ParamUpdateExecute);
    pstConfig->bIsConfigured = true;       
}

static void vAllocate_MemoryForSampleBuffers(sT_DAC_OutputCode_Ctrl_t *pstDACOutCtrl, int *piret)
{
    if(piret == NULL)
        return;

    if(pstDACOutCtrl == NULL)
    {
        *piret = -1;
        FHALT("Null pointer reference for DAC output control");
        return;
    }

    uint16_t uiSampleCount = pstDACOutCtrl->uiNumberofSamples_Period;
    if((uiSampleCount == 0U) || (uiSampleCount > DAC_MAX_WAVEFORM_SAMPLE_COUNT))
    {
        *piret = -1;
        FHALT("Invalid waveform sample count[%d]", uiSampleCount);
        return;
    }

    vFree_MemoryFor_SampleBuffers(pstDACOutCtrl);

    size_t uiBufferSize = (size_t)uiSampleCount * sizeof(uint32_t);
    uint32_t *puiBufferA = malloc(uiBufferSize);
    if(puiBufferA == NULL)
    {
        *piret = -1;
        FHALT("Memory couldnt be allocated to MemoryBuffer_A");
        return;
    }

    uint32_t *puiBufferB = malloc(uiBufferSize);
    if(puiBufferB == NULL)
    {
        free(puiBufferA);
        *piret = -1;
        FHALT("Memory couldnt be allocated to MemoryBuffer_B");
        return;
    }

    pstDACOutCtrl->dacout_perWave_t.stTWaveConfig.puiBuffer_A = puiBufferA;
    pstDACOutCtrl->dacout_perWave_t.stTWaveConfig.puiBuffer_B = puiBufferB;
    pstDACOutCtrl->dacout_perWave_t.stTWaveConfig.uiSampleBufferLength = uiSampleCount;
    *piret = 0;
}

static void vFree_MemoryFor_SampleBuffers(sT_DAC_OutputCode_Ctrl_t *pstDACOutCtrl)
{
    if(pstDACOutCtrl == NULL)
        return;

    if(pstDACOutCtrl->dacout_perWave_t.stTWaveConfig.puiBuffer_A != NULL)
    {
        free(pstDACOutCtrl->dacout_perWave_t.stTWaveConfig.puiBuffer_A);
        pstDACOutCtrl->dacout_perWave_t.stTWaveConfig.puiBuffer_A = NULL;
    }

    if(pstDACOutCtrl->dacout_perWave_t.stTWaveConfig.puiBuffer_B != NULL)
    {
        free(pstDACOutCtrl->dacout_perWave_t.stTWaveConfig.puiBuffer_B);
        pstDACOutCtrl->dacout_perWave_t.stTWaveConfig.puiBuffer_B = NULL;
    }

    pstDACOutCtrl->dacout_perWave_t.stTWaveConfig.uiSampleBufferLength = 0;
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

    if(uiNumSamples > DAC_MAX_WAVEFORM_SAMPLE_COUNT)
    {
        FHALT("Sine waveform sample count[%d] exceeds buffer size[%d]",
              uiNumSamples,
              DAC_MAX_WAVEFORM_SAMPLE_COUNT);
        *piret = -1;
        k_spin_unlock(&stLock_DACOutputCodeCtrl, key);
        return;
    }
    if(uiNumSamples > stTDACOutputCodeCtrl_t.dacout_perWave_t.stTWaveConfig.uiSampleBufferLength)
    {
        FHALT("Sine waveform sample count[%d] exceeds allocated buffer length[%d]",
              uiNumSamples,
              stTDACOutputCodeCtrl_t.dacout_perWave_t.stTWaveConfig.uiSampleBufferLength);
        *piret = -1;
        k_spin_unlock(&stLock_DACOutputCodeCtrl, key);
        return;
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
    memset(puiBuffer, 0, (size_t)uiNumSamples * sizeof(uint32_t));

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

    vSet_WaveformType(pstConfig, &stTDACOutputCodeCtrl_t, &iret);
    if(iret != 0)
    {
        return;
    }
    vSet_ActiveBuffer(eDAC_Buffer_A, eNUMBER_OF_BUFFERs, &stTDACOutputCodeCtrl_t);

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

    vAllocate_MemoryForSampleBuffers(&stTDACOutputCodeCtrl_t, &iret);
    if(iret != 0)
    {
        vFree_MemoryFor_SampleBuffers(&stTDACOutputCodeCtrl_t);
        pstConfig->bIsConfigured = false;
        return;
    }    
    
    uint32_t *puiBuffer = puiGetDACBuffer(stTDACOutputCodeCtrl_t.eCurrentBuffer, &stTDACOutputCodeCtrl_t);
    if(puiBuffer == NULL)
    {
        vFree_MemoryFor_SampleBuffers(&stTDACOutputCodeCtrl_t);
        FHALT("Invalid Data Buffer in Sawtooth Mode.");
        pstConfig->bIsConfigured = false;
        return;        
    }
    vCompute_SawtoothDataBuffer(&iret, puiBuffer, &stTempDACOutCtrl);
    if(iret != 0)    
    {
        vFree_MemoryFor_SampleBuffers(&stTDACOutputCodeCtrl_t);
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
        vFree_MemoryFor_SampleBuffers(&stTDACOutputCodeCtrl_t);
        FHALT("Failed to configure DAC reference voltage source.");
        pstConfig->bIsConfigured = false;
        return;
    }
    Print_Sawtooth("Wavegen -> Reference Source Configured\n\r");

    vConfigure_FIFOWorkMode(&stDACConfig, pstConfig, &iret);
    if(iret != 0)
    {
        vFree_MemoryFor_SampleBuffers(&stTDACOutputCodeCtrl_t);
        pstConfig->bIsConfigured = false;
        return;
    }
    Print_Sawtooth("Wavegen -> WorkMode Configured\n\r");

    DAC_Init(DAC0, &stDACConfig);
    DAC_Enable(DAC0, true);

    vConfigure_DACTrigSrc(pstConfig, &iret);
    if(iret != 0)
    {
        vFree_MemoryFor_SampleBuffers(&stTDACOutputCodeCtrl_t);
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
        case eTrigSrcGroup_CTIMER:
            vSetDACOperationMode(eDAC_InternalMode_WaveGen_CTimer);
            vConfigure_DACTrigSrc_CTIMER(stTrigSrcMux_t.uTrigSrc.eCTimerTrigSrc, piret,
                                         pstConfig->stOutputConfig.uOutputConfig.stWaveFormOutput.pvErrorCallback);
            break;
        case eTrigSrcGroup_LPTIMER:
            break;
        case eTrigSrcGroup_AOI:
            break;
        case eTrigSrcGroup_GPIO:
            break;
        case eTrigSrcGroup_CPU:
            break;
        case eTrigSrcGroup_ADC:
            break;
        case eTrigSrcGroup_None:
        default:
            FHALT("Invalid HW Trigger Configuration\n\r");
            *piret = -1;
            return;
    }
    
    if(*piret != 0)
        return;
}

void vConfigure_DACTrigSrc_CTIMER(eTrigSrc_CTimer_t eTrigCTimer, int *piret, DACError_Callback_t vCallBackFn)
{
    vAssign_CTimer_ToConfig(eTrigCTimer, piret);
    if(*piret != 0)
    {
        FHALT("Invalid CTIMER Configuration");
        return;
    }

    if(!bTrigSrc_AcquireCTimer(eTrigCTimer,
                               eTrigConsumer_DAC0_WaveGen,
                               eTrigShareMode_Exclusive))
    {
        FHALT("DAC waveform generator cannot exclusively acquire CTIMER trigger source[%d]", eTrigCTimer);
        DACHWTrigCTIMER_t.eTrigSource = eNUMBER_OF_CTIMER_TRIG_SRCs;
        DACHWTrigCTIMER_t.eInputMuxConnection = 0U;
        *piret = -1;
        return;
    }

    if(!bTrigSrc_ConfigureCTimer(eTrigCTimer,
                                 eTrigConsumer_DAC0_WaveGen,
                                 stTDACOutputCodeCtrl_t.uiTriggerFrequency_Hz))
    {
        FHALT("Failed to configure exclusive DAC CTIMER trigger source[%d]", eTrigCTimer);
        vDeInit_CTimer_Configuration();
        *piret = -1;
        return;
    }
    
    INPUTMUX_Init(INPUTMUX0);
    INPUTMUX_AttachSignal(INPUTMUX0, 0U, DACHWTrigCTIMER_t.eInputMuxConnection);
    
    uint32_t *puiBuffer = puiGetDACBuffer(stTDACOutputCodeCtrl_t.eCurrentBuffer, &stTDACOutputCodeCtrl_t);
    
    if(puiBuffer == NULL)
    {
        FHALT("Failed to get active buffer for DAC output.");
        vDeInit_CTimer_Configuration();
        *piret = -1;
        return;
    }

    vInit_TCDRefill_Worker(stTDACOutputCodeCtrl_t.eWaveType);
    if(!bSetup_DAC_DMA_Circular(puiBuffer, 
                       stTDACOutputCodeCtrl_t.uiNumberofSamples_Period,
                       eGetWaveFormType(),
                       stTDACOutputCodeCtrl_t.eCurrentTCDBuffer, 
                       (uintptr_t)DAC0, vCallBackFn, vNotify_DACParameterUpdate_Callback))
    {
        vCancel_TCDWorker(stTDACOutputCodeCtrl_t.eWaveType);
        vDeInit_CTimer_Configuration();
        *piret = -1;
        return;         
    }

    vStart_CTimer();
    k_busy_wait(100U);
    *piret = 0;
}

void vInit_TCDRefill_Worker( eDAC_WaveFormType_t eWaveType )
{
    switch(eWaveType)
    {
        case eDAC_WaveForm_Triangle:
        case eDAC_WaveForm_Sawtooth:
        case eDAC_WaveForm_Sine:
            break;
        case eDAC_WaveForm_WhiteNoise:
        case eDAC_WaveForm_PinkNoise:
            k_work_init(&stWorker_NoiseBlockRefill, vDAC_NoiseBlockRefill);
            break;
        default:
            FHALT("Invalid Waveform Type");
            return;                  
    }
}

void vCancel_TCDWorker( eDAC_WaveFormType_t eWaveType )
{
    switch(eWaveType)
    {
        case eDAC_WaveForm_Triangle:
        case eDAC_WaveForm_Sawtooth:
        case eDAC_WaveForm_Sine:
            break;
        case eDAC_WaveForm_WhiteNoise:
        case eDAC_WaveForm_PinkNoise:
            k_work_cancel(&stWorker_NoiseBlockRefill);
            k_work_flush(&stWorker_NoiseBlockRefill, &stSync_NoiseBlockRefill);
            break;
        default:
            FHALT("Invalid Waveform Type");
            return;                  
    }    
}

static void vStart_CTimer( void )
{
    if(!bTrigSrc_StartCTimer(DACHWTrigCTIMER_t.eTrigSource, eTrigConsumer_DAC0_WaveGen))
    {
        FHALT("Failed to start DAC exclusive CTIMER trigger source");
        return;
    }
}

static void vStop_CTimer( void )
{
    if(!bTrigSrc_StopCTimer(DACHWTrigCTIMER_t.eTrigSource, eTrigConsumer_DAC0_WaveGen))
    {
        FHALT("Failed to stop DAC exclusive CTIMER trigger source");
        return;
    }
}

void vDeInit_CTimer_Configuration(void)
{
    if(DACHWTrigCTIMER_t.eTrigSource >= eNUMBER_OF_CTIMER_TRIG_SRCs)
        return;

    vTrigSrc_ReleaseCTimer(DACHWTrigCTIMER_t.eTrigSource, eTrigConsumer_DAC0_WaveGen);
    DACHWTrigCTIMER_t.eTrigSource = eNUMBER_OF_CTIMER_TRIG_SRCs;
    DACHWTrigCTIMER_t.eInputMuxConnection = 0U;
}

void vAssign_CTimer_ToConfig(eTrigSrc_CTimer_t eTrigCTimer, int *piret)
{
    if((eTrigCTimer >= eNUMBER_OF_CTIMER_TRIG_SRCs) ||
       (caTimerInputMuxMap[eTrigCTimer] == 0U))
    {
        FHALT("Invalid Timer Module/Channel Selection for DAC HW Trigger. Defined TimerConfig: %d", eTrigCTimer);
        *piret = -1;
        return;
    }

    DACHWTrigCTIMER_t.eTrigSource = eTrigCTimer;
    DACHWTrigCTIMER_t.eInputMuxConnection = caTimerInputMuxMap[eTrigCTimer];
    *piret = 0;
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
        case eDAC_WaveForm_PinkNoise:
        case eDAC_WaveForm_WhiteNoise:
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

    if(uiNumSamples > DAC_MAX_WAVEFORM_SAMPLE_COUNT)
    {
        FHALT("Sawtooth waveform sample count[%d] exceeds buffer size[%d]",
              uiNumSamples,
              DAC_MAX_WAVEFORM_SAMPLE_COUNT);
        *piret = -1;
        return;
    }
    if(uiNumSamples > stTDACOutputCodeCtrl_t.dacout_perWave_t.stTWaveConfig.uiSampleBufferLength)
    {
        FHALT("Sawtooth waveform sample count[%d] exceeds allocated buffer length[%d]",
              uiNumSamples,
              stTDACOutputCodeCtrl_t.dacout_perWave_t.stTWaveConfig.uiSampleBufferLength);
        *piret = -1;
        return;
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

    memset(puiBuffer, 0, (size_t)uiNumSamples * sizeof(uint32_t));

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

    switch(pstOutputCodeCtrl->eWaveType)
    {
        case eDAC_WaveForm_Triangle:
        case eDAC_WaveForm_Sawtooth:
        case eDAC_WaveForm_Sine:
            return puiGetBuffer_Waveform(eBuffer, pstOutputCodeCtrl);
        case eDAC_WaveForm_WhiteNoise:
        case eDAC_WaveForm_PinkNoise:
            return puiGet_Active_TCDBuffer();
        default:
            FHALT("Not Supported Waveform Type @Type : %d", pstOutputCodeCtrl->eWaveType);
            return NULL;            
    }
}

static eT_TCDBuff_t eGet_Active_TCDBufferId( void )
{
    eNoiseBufState_t eState;

    for(uint8_t i = eTCDBuff_0; i < DMA_TCD_RING_BUFF_COUNT; i++)
    {
        eState = atomic_load_explicit(&stNoiseConfigCtrl.staTCDBuff[i].eBuffState, memory_order_acquire);
        if(eState == eNoiseBuf_Active)
            return i;
    }
    return eNUMBER_OF_BUFFERs;    
}

eT_TCDBuff_t eGet_Ready_TCDBufferId(void)
{
    eNoiseBufState_t eState;

    for(uint8_t i = eTCDBuff_0; i < DMA_TCD_RING_BUFF_COUNT; i++)
    {
        eState = atomic_load_explicit(&stNoiseConfigCtrl.staTCDBuff[i].eBuffState, memory_order_acquire);
        if(eState == eNoiseBuf_Ready)
            return i; 
    }

    return eNUMBER_OF_BUFFERs;    
}

uint32_t *puiGet_TCDBuffer(eT_TCDBuff_t eBuffId)
{
    if(eBuffId >= DMA_TCD_RING_BUFF_COUNT)
    {
        return NULL;
    }

    return stNoiseConfigCtrl.staTCDBuff[eBuffId].uiBuffer;
}

bool bTry_Claim_TCDBuffer_ForDMA(eT_TCDBuff_t eBuffId)
{
    if(eBuffId >= DMA_TCD_RING_BUFF_COUNT)
        return false;

    k_spinlock_key_t key = k_spin_lock(&stLock_TCDBufferUpdate);

    eNoiseBufState_t eState = atomic_load_explicit(&stNoiseConfigCtrl.staTCDBuff[eBuffId].eBuffState, memory_order_acquire);
    if(eState != eNoiseBuf_Ready)
    {
        k_spin_unlock(&stLock_TCDBufferUpdate, key);
        return false;
    }

    atomic_store_explicit(&stNoiseConfigCtrl.staTCDBuff[eBuffId].eBuffState, eNoiseBuf_Queued, memory_order_release);
    k_spin_unlock(&stLock_TCDBufferUpdate, key);
    return true;
}

void vMark_TCDBuffer_Queued(eT_TCDBuff_t eBuffId)
{
    if(eBuffId >= DMA_TCD_RING_BUFF_COUNT)
        return;

    vSet_TCDBuffer_State(eBuffId, eNoiseBuf_Queued);
}

static bool bTry_Claim_TCDBuffer_ForCPUFill(eT_TCDBuff_t eBuffId)
{
    if(eBuffId >= DMA_TCD_RING_BUFF_COUNT)
        return false;

    return bTry_Claim_TCDBuffer(eBuffId, eNoiseBuf_Filling);
}

static bool bTry_Claim_TCDBuffer(eT_TCDBuff_t eBuffId, eNoiseBufState_t eNewState)
{
    if(eBuffId >= DMA_TCD_RING_BUFF_COUNT)
        return false;
    
    k_spinlock_key_t key = k_spin_lock(&stLock_TCDBufferUpdate);

    eNoiseBufState_t eState = atomic_load_explicit(&stNoiseConfigCtrl.staTCDBuff[eBuffId].eBuffState, memory_order_acquire);
    if(eState != eNoiseBuf_Free && eState != eNoiseBuf_Ready)
    {
        k_spin_unlock(&stLock_TCDBufferUpdate, key);
        return false;
    }

    atomic_store_explicit(&stNoiseConfigCtrl.staTCDBuff[eBuffId].eBuffState, eNewState, memory_order_release);
    k_spin_unlock(&stLock_TCDBufferUpdate, key);
    return true;    
}

static eT_TCDBuff_t eGet_Free_TCDBufferId( void )
{
    eNoiseBufState_t eState;

    for(uint8_t i = eTCDBuff_0; i < DMA_TCD_RING_BUFF_COUNT; i++)
    {
        eState = atomic_load_explicit(&stNoiseConfigCtrl.staTCDBuff[i].eBuffState, memory_order_acquire);
        if(eState == eNoiseBuf_Free)
            return i; 
    }
    return eNUMBER_OF_BUFFERs;    
}

static inline eNoiseBufState_t eGet_TCDBuffState( eT_TCDBuff_t eBuffId )
{
    eNoiseBufState_t eState = atomic_load_explicit(&stNoiseConfigCtrl.staTCDBuff[eBuffId].eBuffState, memory_order_acquire);
    return eState;
}

static uint32_t * puiGet_Active_TCDBuffer( void )
{
    eT_TCDBuff_t eBuffer = eGet_Active_TCDBufferId();
    if(eBuffer == eNUMBER_OF_BUFFERs)
        return NULL;
    
    return stNoiseConfigCtrl.staTCDBuff[eBuffer].uiBuffer;
}

static void vSet_Active_TCDBuffer(eT_TCDBuff_t eBufferId)
{
    if(eBufferId >= DMA_TCD_RING_BUFF_COUNT)
    {
        FHALT("Invalid TCD Buffer for Noise Generator");
        return;
    }

    k_spinlock_key_t key = k_spin_lock(&stLock_TCDBufferUpdate);
    stTDACOutputCodeCtrl_t.eCurrentTCDBuffer = eBufferId;
    atomic_store_explicit(&stNoiseConfigCtrl.staTCDBuff[eBufferId].eBuffState, eNoiseBuf_Active, memory_order_release); 
    k_spin_unlock(&stLock_TCDBufferUpdate, key);
}

static void vSet_TCDBuffer_Free(eT_TCDBuff_t eBufferId)
{
    if(eBufferId >= DMA_TCD_RING_BUFF_COUNT)
    {
        FHALT("Invalid TCD Buffer for Noise Generator");
        return;
    }

    k_spinlock_key_t key = k_spin_lock(&stLock_TCDBufferUpdate);
    atomic_store_explicit(&stNoiseConfigCtrl.staTCDBuff[eBufferId].eBuffState, eNoiseBuf_Free, memory_order_release); 
    k_spin_unlock(&stLock_TCDBufferUpdate, key);
}

static void vSet_TCDBuffer_State(eT_TCDBuff_t eBufferId, eNoiseBufState_t eState)
{
    if(eBufferId >= DMA_TCD_RING_BUFF_COUNT)
    {
        FHALT("Invalid TCD Buffer for Noise Generator");
        return;
    }

    k_spinlock_key_t key = k_spin_lock(&stLock_TCDBufferUpdate);
    atomic_store_explicit(&stNoiseConfigCtrl.staTCDBuff[eBufferId].eBuffState, eState, memory_order_release);
    k_spin_unlock(&stLock_TCDBufferUpdate, key);
}

static void vInit_TCDBuffers( void )
{
    k_spinlock_key_t key = k_spin_lock(&stLock_TCDBufferUpdate); 
    for(uint8_t i = eTCDBuff_0; i < DMA_TCD_RING_BUFF_COUNT; i++)
    {
        stNoiseConfigCtrl.staTCDBuff[i].eBuffId = i;
        stNoiseConfigCtrl.staTCDBuff[i].eBuffState = eNoiseBuf_Free;
        memset(stNoiseConfigCtrl.staTCDBuff[i].uiBuffer, 0, sizeof(stNoiseConfigCtrl.staTCDBuff[i].uiBuffer));
    }
    k_spin_unlock(&stLock_TCDBufferUpdate, key);    
}

uint32_t * puiGetBuffer_Waveform(eDAC_Buffer_t eBuffer, sT_DAC_OutputCode_Ctrl_t *pstOutputCodeCtrl)
{
    if(pstOutputCodeCtrl == NULL)
    {
        FHALT("Invalid pointer to DAC output control");
        return NULL;
    }

    if(pstOutputCodeCtrl->dacout_perWave_t.stTWaveConfig.uiSampleBufferLength < pstOutputCodeCtrl->uiNumberofSamples_Period)
    {
        FHALT("DAC waveform buffer length[%d] is smaller than sample count[%d]",
              pstOutputCodeCtrl->dacout_perWave_t.stTWaveConfig.uiSampleBufferLength,
              pstOutputCodeCtrl->uiNumberofSamples_Period);
        return NULL;
    }

    switch(eBuffer)
    {
        case eDAC_Buffer_A:
            return pstOutputCodeCtrl->dacout_perWave_t.stTWaveConfig.puiBuffer_A;
        case eDAC_Buffer_B:
            return pstOutputCodeCtrl->dacout_perWave_t.stTWaveConfig.puiBuffer_B;
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

void vSet_ActiveBuffer(eDAC_Buffer_t eBuffer, eT_TCDBuff_t eTCDBuff, sT_DAC_OutputCode_Ctrl_t *pstOutputCodeCtrl)
{
    if(pstOutputCodeCtrl == NULL)
    {
        FHALT("Invalid pointer to DAC output code control structure.");
        return;
    }

    switch(pstOutputCodeCtrl->eWaveType)
    {
        case eDAC_WaveForm_Triangle:
        case eDAC_WaveForm_Sawtooth:
        case eDAC_WaveForm_Sine:
            vSet_ActiveBuffer_Waveform(eBuffer, pstOutputCodeCtrl);
            break;
        case eDAC_WaveForm_WhiteNoise:
        case eDAC_WaveForm_PinkNoise:
            vSet_Active_TCDBuffer(eTCDBuff);
            break;
        default:
            FHALT("Not Supported Waveform Type @Type : %d", pstOutputCodeCtrl->eWaveType);
            break;        
    }

}

void vSet_ActiveBuffer_Waveform(eDAC_Buffer_t eBuffer, sT_DAC_OutputCode_Ctrl_t *pstOutputCodeCtrl)
{
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

    switch(pstSrc->eWaveType)
    {
        case eDAC_WaveForm_Triangle:
        case eDAC_WaveForm_Sawtooth:
        case eDAC_WaveForm_Sine:
            pstDest->eCurrentBuffer = pstSrc->eCurrentBuffer;
            break;
        case eDAC_WaveForm_PinkNoise:
        case eDAC_WaveForm_WhiteNoise:
            pstDest->eCurrentTCDBuffer = pstSrc->eCurrentTCDBuffer;
            pstDest->uiSampleRate_S_s = pstSrc->dacout_perWave_t.stTNoiseConfig.uiSampleRate_S_s;
            pstDest->uiBlockRepeatTime_us = pstSrc->dacout_perWave_t.stTNoiseConfig.uiBlockRepeatTime_us;
            break;
        default:
            FHALT("Not Supported Waveform Type");
            return;
    }

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
    pstDest->uiMaxOutput_mV = pstSrc->uiMaxOutput_mV;
    pstDest->uiMinOutput_mv = pstSrc->uiMinOutput_mv;
    pstDest->uiMaxCode = pstSrc->uiMaxCode;
    pstDest->uiMinCode = pstSrc->uiMinCode;
    pstDest->uiNumberofSamples_Period = pstSrc->uiNumberofSamples_Period;
    pstDest->fSettlingTime_us = pstSrc->fSettlingTime_us;
    pstDest->stTDACHWConfig = pstSrc->stTDACHWConfig;

    switch(pstDest->eWaveType)
    {
        case eDAC_WaveForm_Triangle:
        case eDAC_WaveForm_Sawtooth:
        case eDAC_WaveForm_Sine:
            pstDest->eCurrentBuffer = pstSrc->eCurrentBuffer;
            break;
        case eDAC_WaveForm_PinkNoise:
        case eDAC_WaveForm_WhiteNoise:
            pstDest->eCurrentTCDBuffer = pstSrc->eCurrentTCDBuffer;
            pstDest->dacout_perWave_t.stTNoiseConfig.uiBlockRepeatTime_us = pstSrc->uiBlockRepeatTime_us;
            pstDest->dacout_perWave_t.stTNoiseConfig.uiSampleRate_S_s = pstSrc->uiSampleRate_S_s;
            break;
        default:
            FHALT("Not Supported Waveform Type");
            break;        
    }
}

void vCompute_DAC_OutputTiming(sT_DAC_Config_t *pstConfig, sT_DAC_OutputCode_Metadata_t *pstDACOutCodeCtrl, int *piret)
{
    float ffreq_hz = 0.0f, fMaxSampleRate = 0.0f;
    uint32_t uiMaxSampleRate = 0;

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
    #define eWaveformType     (pstConfig->stOutputConfig.eWaveFormType)
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

    switch(eWaveformType)
    {
        case eDAC_WaveForm_Sawtooth:
        case eDAC_WaveForm_Sine:
            ffreq_hz = 1.0f / (pstDACOutCodeCtrl->fSettlingTime_us * 1e-6f);
            pstDACOutCodeCtrl->uiTriggerFrequency_Hz = (uint32_t)ffreq_hz;
            pstDACOutCodeCtrl->uiNumberofSamples_Period = (uint16_t)(pstDACOutCodeCtrl->uiTriggerFrequency_Hz / 
                                                            pstConfig->stOutputConfig.uOutputConfig.stWaveFormOutput.uiFrequencyHz);
            if(pstDACOutCodeCtrl->uiNumberofSamples_Period < DAC_MIN_WAVEFORM_SAMPLES_PER_PERIOD)
            {
                FHALT("Calculated number of samples per period for %s is less than the minimum required.", 
                    (eWaveformType == eDAC_WaveForm_Sawtooth)? "sawtooth waveform": "sine waveform");
                *piret = -1;
                return;
            }
            if(pstDACOutCodeCtrl->uiNumberofSamples_Period > DAC_MAX_WAVEFORM_SAMPLE_COUNT)
            {
                FHALT("Calculated number of samples per period[%d] exceeds waveform buffer size[%d]",
                    pstDACOutCodeCtrl->uiNumberofSamples_Period,
                    DAC_MAX_WAVEFORM_SAMPLE_COUNT);
                *piret = -1;
                return;
            }
            break;
        case eDAC_WaveForm_PinkNoise:
        case eDAC_WaveForm_WhiteNoise:
            fMaxSampleRate = 1.0f / (pstDACOutCodeCtrl->fSettlingTime_us * 1e-6f);
            uiMaxSampleRate = (uint32_t)fMaxSampleRate;
            pstDACOutCodeCtrl->uiSampleRate_S_s = (uint32_t)(2.5f * (float)pstConfig->stOutputConfig.uOutputConfig.stWaveFormOutput.uiFrequencyHz);
            pstDACOutCodeCtrl->uiSampleRate_S_s = min(pstDACOutCodeCtrl->uiSampleRate_S_s, uiMaxSampleRate);
            pstDACOutCodeCtrl->uiTriggerFrequency_Hz = pstDACOutCodeCtrl->uiSampleRate_S_s;
            pstDACOutCodeCtrl->uiNumberofSamples_Period = DMA_NOISEGEN_SAMPLE_COUNT;
            if(pstDACOutCodeCtrl->uiNumberofSamples_Period < DAC_MIN_WAVEFORM_SAMPLES_PER_PERIOD)
            {
                FHALT("Block is too small for Noise");
                *piret = -1;
                return;
            }
            pstDACOutCodeCtrl->uiBlockRepeatTime_us = (uint32_t)(((uint64_t)pstDACOutCodeCtrl->uiNumberofSamples_Period * 1000000ULL) /
                                                       pstDACOutCodeCtrl->uiSampleRate_S_s);           
            break;
        default:
            FHALT("Not Implemented for Waveform : %d", eWaveformType);
            *piret = -1;
            return;
    }

    *piret = 0;
}

void vDAC_EnableWaveGen(void)
{
    if(stTDACConfig_t.bIsConfigured)
    {
        DAC_Enable(DAC0, true);
        return;
    }

    if(!bIsDACDisabled())
    {
        FHALT("DAC is not properly configured. Cannot enable DAC.");
        return;
    }

    if(stTDACConfig_t.stOutputConfig.eWaveFormType >= eNUMBER_OF_DAC_WAVEFORMs)
    {
        FHALT("Stored DAC configuration is invalid. Reconfigure DAC before enabling.");
        return;
    }

    sT_DAC_Config_t stDACConfig = stTDACConfig_t;
    stDACConfig.bIsConfigured = false;
    vDAC_Init(&stDACConfig);
}

void vDAC_DisableWaveGen( eDAC_DefaultOutLevel_t eDefaultLevel, uint32_t uiCustomVal_mV )
{
    bool bKeepOutputDriven = false;

    if(!stTDACConfig_t.bIsConfigured)
    {
        DAC_Enable(DAC0, false);
        return;
    }

    switch(eGetDACOperationMode())
    {
        case eDAC_InternalMode_WaveGen_CTimer:
            vSet_DACStop_Flag();
            vDisable_DACConfig_with_CTimer();
            vSet_DefaultDACOutput_WithWaveGen(eDefaultLevel, uiCustomVal_mV);
            bKeepOutputDriven = true;
            break;
        case eDAC_InternalMode_Direct:
            DAC_SetData(DAC0, 0U);
            bKeepOutputDriven = true;
            break;
        case eDAC_InternalMode_Unsupported:
        default:
            break;
    }

    if(!bKeepOutputDriven)
    {
        DAC_Enable(DAC0, false);
    }

    stTDACConfig_t.bIsConfigured = false;
    vClear_DACStop_Flag();
    vClear_DAC_Pause();
    vSet_DAC_Disable();
    vSetDACOperationMode(eDAC_InternalMode_Unsupported);
}

void vSet_DefaultDACOutput_WithWaveGen( eDAC_DefaultOutLevel_t eDefaultLevel, uint32_t uiCustomVal_mV )
{
    int iret = 0;

    switch(eDefaultLevel)
    {
        case eDAC_DefaultOut_Low:
            vForce_DAC_FIFO_Output(0U);
            break;
        case eDAC_DefaultOut_High:
            vForce_DAC_FIFO_Output(stTDACOutputCodeCtrl_t.uiMaxCode);
            break;
        case eDAC_DefaultOut_Custom:
        {
            uint32_t uiDACCode = uiCalculateDACCode(uiCustomVal_mV, &iret);
            if(iret != 0)
            {
                FHALT("Invalid custom DAC default output level.");
                return;
            }
            vForce_DAC_FIFO_Output(uiDACCode);
            break;
        }
        default:
            FHALT("Invalid default output level selection for stopping wavegen.");
            break;
    }    
}

void vDisable_DACConfig_with_CTimer( void )
{
    eDAC_WaveFormType_t eWaveform = eGetWaveFormType();

    switch(eWaveform)
    {
        case eDAC_WaveForm_Triangle:
        case eDAC_WaveForm_Sawtooth:
        case eDAC_WaveForm_Sine:
            vDeInit_CTimer_Configuration();
            vDisable_DAC_DMA_Circular();
            vFree_MemoryFor_SampleBuffers(&stTDACOutputCodeCtrl_t);
            vClear_DMAUpdate_Pending();
            vClear_DMAUpdate_Status();
            memset(&stTBuffSwapData, 0, sizeof(stTBuffSwapData));
            break;
        case eDAC_WaveForm_PinkNoise:
        case eDAC_WaveForm_WhiteNoise:
            vDeInit_CTimer_Configuration();
            vDisable_DAC_DMA_Circular();
            vCancel_TCDWorker(eGetWaveFormType());
            vReset_TCDBuffer_Management();//Reset DMA Buffer Management
            for(uint8_t i = eTCDBuff_0; i < DMA_TCD_RING_BUFF_COUNT; i++)
            {
                atomic_store_explicit(&stNoiseConfigCtrl.staTCDBuff[i].eBuffState, eNoiseBuf_Free, memory_order_release);
                memset(stNoiseConfigCtrl.staTCDBuff[i].uiBuffer, 0, sizeof(stNoiseConfigCtrl.staTCDBuff[i].uiBuffer)); 
            }
            vClear_DMAUpdate_Pending();
            vClear_DMAUpdate_Status();
            break;
        default:
            FHALT("Not Supported Waveform Type");
            return;  
    }
}

void vStop_WaveGen(eDAC_DefaultOutLevel_t eDefaultLevel, uint32_t uiCustomVal_mV)
{
    if(bIsDACDisabled())
    {
        FHALT("DAC already disabled");
        return;
    }
    if(bIsDACStopped())
    {
        FHALT("Wavegen is already stopped.");
        return;
    }
    
    switch(eGetDACOperationMode())
    {
        case eDAC_InternalMode_WaveGen_CTimer:
            vStop_WaveGenerator();
            break;
        default:
            FHALT("Not Implemented yet.");
            return;
    }

    vSet_DefaultDACOutput_WithWaveGen(eDefaultLevel, uiCustomVal_mV);
}

void vStop_WaveGenerator( void )
{
    eDAC_WaveFormType_t eWaveType = eGetWaveFormType();

    switch(eWaveType)
    {
        case eDAC_WaveForm_Triangle:
        case eDAC_WaveForm_Sawtooth:
        case eDAC_WaveForm_Sine:
            vStop_WaveFormGenerator();
            break;
        case eDAC_WaveForm_PinkNoise:
        case eDAC_WaveForm_WhiteNoise:
            vStop_NoiseGenerator();
            break;
        default:
            FHALT("Not Supported Waveform Type");
            return;        
    }
}

void vStop_NoiseGenerator( void )
{
    vSet_DACStop_Flag();
    vCancel_TCDWorker(eGetWaveFormType());

    vStop_CTimer();//Stop the timer operation
    vStop_DAC_To_DMA_Request();//Stop DMA
    DAC_ClearStatusFlags(DAC0,
                        kDAC_FIFOOverflowFlag | kDAC_FIFOUnderflowFlag);

    vReset_TCDBuffer_Management();//Reset DMA Buffer Management
    
    for(uint8_t i = eTCDBuff_0; i < DMA_TCD_RING_BUFF_COUNT; i++)
    {
        atomic_store_explicit(&stNoiseConfigCtrl.staTCDBuff[i].eBuffState, eNoiseBuf_Free, memory_order_release);
        memset(stNoiseConfigCtrl.staTCDBuff[i].uiBuffer, 0, sizeof(stNoiseConfigCtrl.staTCDBuff[i].uiBuffer)); 
    }

    vClear_DAC_Pause();
    vClear_DMAUpdate_Pending();
    vClear_DMAUpdate_Status();    
}


void vStop_WaveFormGenerator( void )
{        
    vStop_CTimer();//Stop the timer operation
    vDisable_DAC_DMA_Circular();//Stop DAC DMA request and force restart to rebuild DMA
    DAC_ClearStatusFlags(DAC0,
                        kDAC_FIFOOverflowFlag | kDAC_FIFOUnderflowFlag);

    vClear_DAC_Pause();
    vSet_DACStop_Flag();
    vClear_DMAUpdate_Pending();
    vClear_DMAUpdate_Status();
    memset(&stTBuffSwapData, 0, sizeof(stTBuffSwapData));
}

static void vForce_DAC_FIFO_Output(uint32_t uiDACCode)
{
    uint32_t uiGCR = DAC0->GCR;
    uint32_t uiDirectModeGCR = uiGCR & ~(LPDAC_GCR_FIFOEN_MASK | LPDAC_GCR_TRGSEL_MASK);

    DAC_Enable(DAC0, false);

    DAC_SetReset(DAC0, kDAC_ResetLogic);
    DAC_ClearReset(DAC0, kDAC_ResetLogic);

    DAC0->GCR = uiDirectModeGCR;
    DAC_Enable(DAC0, true);
    DAC_SetData(DAC0, uiDACCode);
    k_busy_wait(20U);
}

void vReStart_WaveGen( void )
{
    if(bIsDACDisabled())
    {
        FHALT("DAC is disabled. You need to re-configure the DAC Module.");
        return;        
    }
    if(!bIsDACStopped())
    {
        FHALT("DAC is not stopped to perform a restart!!!");
        return;
    }
    
    switch(eGetDACOperationMode())
    {
        case eDAC_InternalMode_WaveGen_CTimer:
            vRestart_At_CTimerConfig();
            break;
        default:
            FHALT("Not Implemented yet.");
            return;
    }
    
    vClear_DACStop_Flag();
    vClear_DAC_Pause();
    vClear_DAC_Disable();
}

void vRestart_At_CTimerConfig( void )
{
    int iret = 0;

    eDAC_WaveFormType_t eWaveType = eGetWaveFormType();
    sT_DAC_OutputCode_Metadata_t stRestartMetadata;

    if(eWaveType == eDAC_WaveForm_WhiteNoise || eWaveType == eDAC_WaveForm_PinkNoise)
    {
        vLoad_DACOutputCtrlMetadata(&stRestartMetadata, &stTDACOutputCodeCtrl_t);
        vFill_TCD_Buffers(&iret, &stRestartMetadata);
        if(iret != 0)
        {
            FHALT("Failed to refill DAC noise buffers for waveform restart.");
            return;
        }

        vSet_ActiveBuffer(eDAC_Buffer_None, eTCDBuff_0, &stTDACOutputCodeCtrl_t);
    }

    uint32_t *puiBuffer = puiGetDACBuffer(stTDACOutputCodeCtrl_t.eCurrentBuffer, &stTDACOutputCodeCtrl_t);
    DACError_Callback_t pvErrorCallback =
        stTDACConfig_t.stOutputConfig.uOutputConfig.stWaveFormOutput.pvErrorCallback;

    if(puiBuffer == NULL)
    {
        FHALT("Failed to get active DAC buffer for waveform restart.");
        return;
    }

    DAC_SetReset(DAC0, kDAC_ResetFIFO);
    DAC_ClearReset(DAC0, kDAC_ResetFIFO);
    DAC_ClearStatusFlags(DAC0,
                            kDAC_FIFOOverflowFlag | kDAC_FIFOUnderflowFlag);
    DAC0->FCR = LPDAC_FCR_WML(4U);
    DAC0->GCR = (DAC0->GCR | LPDAC_GCR_FIFOEN_MASK) & ~LPDAC_GCR_TRGSEL_MASK;

    vInit_TCDRefill_Worker(eWaveType);
    if(!bSetup_DAC_DMA_Circular(puiBuffer,
                                stTDACOutputCodeCtrl_t.uiNumberofSamples_Period,
                                eWaveType,
                                stTDACOutputCodeCtrl_t.eCurrentTCDBuffer,
                                (uintptr_t)DAC0,
                                pvErrorCallback,
                                vNotify_DACParameterUpdate_Callback))
    {
        vCancel_TCDWorker(eWaveType);
        FHALT("Failed to rebuild DAC DMA for waveform restart.");
        return;
    }
    vStart_CTimer();
}

void vPause_WaveGen( void )
{
    if(bIsDACDisabled())
    {
        FHALT("DAC is disabled. You need to re-configure the DAC Module.");
        return;        
    }
    if(bIsDACStopped())
    {
        FHALT("DAC is already Stopped. If you want to start the wavegen, then perform a restart!!!");
        return;
    }
    if(bIsDACPaused())
    {
        FHALT("Wavegen is already paused.");
        return;
    }

    switch(eGetDACOperationMode())
    {
        case eDAC_InternalMode_WaveGen_CTimer:
            vStop_CTimer();            
            break;
        default:
            FHALT("Not Implemented yet.");
            return;
    }

    vSet_DAC_Pause();
}

void vResume_WaveGen( void )
{
    if(bIsDACDisabled())
    {
        FHALT("DAC is disabled. You need to re-configure the DAC Module.");
        return;        
    }
    if(bIsDACStopped())
    {
        FHALT("DAC is already Stopped. If you want to start the wavegen, then perform a restart!!!");
        return;
    }
    if(!bIsDACPaused())
    {
        FHALT("Wavegen is not paused.");
        return;
    }

    switch(eGetDACOperationMode())
    {
        case eDAC_InternalMode_WaveGen_CTimer:
            vStart_CTimer();            
            break;
        default:
            FHALT("Not Implemented yet.");
            return;
    }

    vClear_DAC_Pause();
}

#endif

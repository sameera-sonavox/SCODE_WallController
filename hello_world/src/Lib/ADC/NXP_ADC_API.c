#include <stdatomic.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/dma.h>
#include <math.h>
#include "fsl_lpadc.h"
#include "fsl_common.h"
#include "fsl_inputmux.h"

#include "NXP_ADC_API.h"
#include "NXP_ADC_Types.h"
#include "NXP_ADC_CHMap.h"
#include "../GenericMacro.h"
#include "../CPULoad/NXP_CPU_LoadMon.h"
#include "NXP_ADC_ProjDef.h"

static bool bIsADCThreadInit = false;
_Atomic bool bADCStatisticsOverflow = false;
static _Atomic uint32_t uiaADCStatisticsGeneration[eNUMBER_OF_ADC_MODULEs] = {0};

typedef struct
{    
    struct dma_config stADCDMAConfig_t;
    struct dma_block_config stADCDMABlockConfig_t;
    _Atomic bool bIsDMAError;
    const uint32_t *puiDMABuffer;
    uint32_t uiDMALen;
} sT_DMANotifyCtrl;

typedef struct
{
    eADC_NotificationType_t eNotificationType;
    union
    {        
        uint32_t uiInterMask;
        sT_DMANotifyCtrl stTDMANotifyCtrl; 
    } NotifyCtrl;    
} sT_ADCHWNotifyCtrl_t;

typedef struct
{
    eADC_Module_t eModule;
    eADC_Channel_t eChannel;
    uint16_t uiADCValue;
    uint32_t uiGeneration;
} sT_ADC_StatisticsSample_t;

typedef struct
{
    eADC_TrigSlot_t eSlotId;
    bool bIsEnabled;
    _Atomic bool bIsPaused;
    eADC_TrigSrcType_t eaTrigSlotType;
    struct sT_ADC_CommandConfig_t *pstCMDHead;
} sT_TrigSlotCtrl_t;

typedef struct
{
    bool bIsADCInitialized;
    bool baCommandConfigStat[eNUMBER_OF_ADC_COMMANDs];
    _Atomic bool *pbOverflowFlag;
    uint32_t uiGlobalIntrMask;
    sT_TrigSlotCtrl_t staTrigSlotCtrl[eNUMBER_OF_ADC_TRIG_SLOTs];
    ADC_Type *pstADCBase;
    lpadc_config_t stADCConfig;
    lpadc_conv_command_config_t stCmdConfig[eNUMBER_OF_ADC_TRIG_SLOTs];
    ADC_TrigCompCallback_t pfADCTrgiCallback;
    void *pvCallbackUserdata;
    sT_ADCHWNotifyCtrl_t stTADCHWNotifyCtrl;
}stADC_HWmodConfig_t;

static stADC_HWmodConfig_t staADC_HWConfig[eNUMBER_OF_ADC_MODULEs] = {0};

K_THREAD_STACK_DEFINE(thread_ADC_Statistics, ADC_STATISTIC_THREAD_STACK_SIZE);
static struct k_thread kADC_Statistic_Thread_t;
static k_tid_t kADC_ThreadId;
static void vCompute_ADC_Statistics_ForThread( void *p1, void *p2, void *p3 );
static void vCompute_AVG_Value(sT_ADC_ChannelStats_t *pstStatistics, sT_TrigSlotCtrl_t *pstTrigSlot,
                               sT_ADC_StatisticsSample_t *pstTADCSample);
static void vCompute_RMS_Value(sT_ADC_ChannelStats_t *pstStatistics, sT_TrigSlotCtrl_t *pstTrigSlot,
                               sT_ADC_StatisticsSample_t *pstTADCSample);
static void vCompute_Max_Value(sT_ADC_ChannelStats_t *pstStatistics, sT_TrigSlotCtrl_t *pstTrigSlot, sT_ADC_StatisticsSample_t *pstTADCSample);
static void vCompute_Min_Value(sT_ADC_ChannelStats_t *pstStatistics, sT_TrigSlotCtrl_t *pstTrigSlot, sT_ADC_StatisticsSample_t *pstTADCSample);

K_MSGQ_DEFINE(kADCMeasDataQueue, sizeof(sT_ADC_StatisticsSample_t), ADC_MSGQ_LENGTH, 4);
K_MUTEX_DEFINE(kADCStatisticsMutex);

#define bIsADCInitialized(index)            (staADC_HWConfig[index].bIsADCInitialized)
#define vSet_ADCThread_InitFlag()           (bIsADCThreadInit = true)
#define vResetSet_ADCThread_InitFlag()      (bIsADCThreadInit = false)

static const struct device *const pstaADCDMADev[eNUMBER_OF_ADC_MODULEs] = {
    [eADC_ADC0] = DEVICE_DT_GET(ADC0_DMA_CTLR_NODE),
    [eADC_ADC1] = DEVICE_DT_GET(ADC1_DMA_CTLR_NODE),
};
static const uint32_t uiaADCDMAChannel[eNUMBER_OF_ADC_MODULEs] = {
    [eADC_ADC0] = ADC0_DMA_CHANNEL,
    [eADC_ADC1] = ADC1_DMA_CHANNEL,
};
static const uint32_t uiaADCDMASlot[eNUMBER_OF_ADC_MODULEs] = {
    [eADC_ADC0] = ADC0_DMA_SLOT,
    [eADC_ADC1] = ADC1_DMA_SLOT,
};

static void vValidate_ADCConfig(sT_ADC_ModuleConfig_t *pstADCModuleConfig);
static bool bValidate_ADCGetRequest(eADC_Module_t eModule, eADC_Channel_t eChannel, uint16_t *puiValue, eADC_ValueType_t eValType);
static bool bValidate_TrigSourceCompCallbackFn(sT_ADC_ModuleConfig_t *pstADCModuleConfig);
static bool bADC_Init(ADC_Type *pstADCBase, stADC_HWmodConfig_t *pstHWConfig,sT_ADC_ModuleConfig_t *pstADCModuleConfig);

static bool bADC_InitCommandConfig(ADC_Type *pstADCBase, 
                                   stADC_HWmodConfig_t *pstHWConfig, 
                                   sT_ADC_ModuleConfig_t *pstADCModuleConfig);

static bool bConfig_ADCCommand(lpadc_conv_command_config_t *pstlpadc_CmdConfig, sT_ADC_CMDData_t *pstCmdData, sT_ADC_ModuleConfig_t *pstADCModuleConfig);                                   
static bool bMap_ADCChannel(eADC_Module_t eModule, eADC_Channel_t eChannel, uint32_t *puiMappedChannel);
static bool bAssign_ADCResolution(eADC_ResolutionType_t eResolution, lpadc_conversion_resolution_mode_t *peLPADCResolution);
static bool bAssign_LoopBehavior(eADC_Module_t eADCModule, lpadc_conv_command_config_t *pstlpadc_CmdConfig, sT_ADC_CMDData_t *pstCmdData);
static bool bAssign_HWAverageMode(lpadc_conv_command_config_t *pstlpadc_CmdConfig, sT_ADC_CMDData_t *pstCmdData);
static bool bAssign_SampleTime(lpadc_conv_command_config_t *pstlpadc_CmdConfig, sT_ADC_CMDData_t *pstCmdData);
static bool bHW_TrigSrc_Setup(ADC_Type *pstADCBase, eADC_Module_t eADCmodule, sT_ADC_TrigConfig_t *pstTrigConfig);
static void vSet_TrigSlot_TrigType(eADC_Module_t eADCModule, sT_ADC_TrigConfig_t *pstTrigConfig);
static eADC_TrigSrcType_t eGet_TrigSlot_TrigType(eADC_Module_t eADCModule, eADC_TrigSlot_t eTrigSlot);
static bool bIs_TrigSlotEnabled(eADC_Module_t eADCModule, eADC_TrigSlot_t eTrigSlot);
static void vInit_ADC_Thread( void );
static void vDeInit_ADC_Thread( void );
static void vRelease_CMDMemory_AtFailure(sT_ADC_ModuleConfig_t *pstADCModuleConfig);
static void vRelease_CommandMemoryBuffers(sT_TrigSlotCtrl_t *pstaTrigSlotCtrl);

static bool bADC_ResultReadBackConfig(ADC_Type *pstADCBase, eADC_Module_t eADCModule, sT_ADC_ModuleConfig_t *pstADCModuleConfig);
static bool bSet_TrigCompletionInterrupts(ADC_Type *pstADCBase, eADC_Module_t eADCModule, sT_ADC_ModuleConfig_t *pstADCModuleConfig);
static bool bConfig_ForInterrupt(ADC_Type *pstADCBase, eADC_Module_t eADCModule, const sT_ADCNotify_Interrupt_t stTInterruptCtrl);
static void vEnable_ADC_TrigCompletionInterrupts(ADC_Type *pstADCBase, eADC_Module_t eADCModule);
static void vEnable_ADC_IRQ(eADC_Module_t eADCModule);
static void vADC_ISRHandler(eADC_Module_t eADCModule);
static inline void vUpdate_ADC_Value(eADC_Module_t eADCModule, lpadc_conv_result_t stResult);
static void vADC0_ISR(const void *pvArg);
static void vADC1_ISR(const void *pvArg);
static void vDisable_ADC_Interrupts(ADC_Type *pstADCBase, eADC_Module_t eADCModule);
static inline void vDrain_ADC_FIFO(ADC_Type *pstADCBase, lpadc_conv_result_t *pstResult);
static inline void vNotify_ADCOverflow(eADC_Module_t eADCModule, bool bres);

static bool bConfig_ForDMA(ADC_Type *pstADCBase, eADC_Module_t eADCModule, const sT_ADCNotify_DMA_t stTDMACtrl);
static void vADC0_DMA_Callback(const struct device *dev, void *pUserData, uint32_t uiChannel, int iStatus);
static void vADC1_DMA_Callback(const struct device *dev, void *pUserData, uint32_t uiChannel, int iStatus);

static ADC_Type *pstGetADCBase(eADC_Module_t eADCModule);

static inline stADC_HWmodConfig_t *pstGetADCModule(eADC_Module_t eADCModule);
static inline bool bSet_CommandForUse(eADC_Module_t eADCModule, eADC_Command_t eCmdId);
static inline bool bIs_CommandInUse(eADC_Module_t eADCModule, eADC_Command_t eCmdId);
static inline void vSet_Msgq_FullFlag( void );//(bADCStatisticsOverflow = true)
static inline void vClear_Msgq_FullFlag( void );//(bADCStatisticsOverflow = false)
static inline bool bIs_Msgq_Full( void );//(bADCStatisticsOverflow == true)

static bool bPause_ADCModule(eADC_Module_t eModule);
static void vResume_ADCModule(eADC_Module_t eModule);
static void vResume_All_ADCModules( void );
static bool bWait_ADCModuleIdle(eADC_Module_t eModule);
static inline void vResume_PausedTrigSlot(sT_TrigSlotCtrl_t *pstTrigSlotCtrl);
static inline void vPasue_ActiveTrigSlot(sT_TrigSlotCtrl_t *pstTrigSlotCtrl);
static inline bool bIsTigSlot_Paused(sT_TrigSlotCtrl_t *pstTrigSlotCtrl);
static inline sT_TrigSlotCtrl_t *pstGetTrigSlotInfo(eADC_Module_t eAdcModule, eADC_TrigSlot_t eSlotId);

const sT_ADC_CommandConfig_t *pstGetCommandData(eADC_Module_t eADCModule, eADC_Channel_t eChannel)
{
    if(eADCModule < 0 || eADCModule >= eNUMBER_OF_ADC_MODULEs)
    {
        FHALT("Invalid ADC Module : %d", eADCModule);
        return NULL;
    }
    if(eChannel < 0 || eChannel >= eNUMBER_OF_ADC_CHANNELs)
    {
        FHALT("Invalid ADC Channel : %d", eChannel);
        return NULL;        
    }

    stADC_HWmodConfig_t *pstADCHW = &staADC_HWConfig[eADCModule];
    sT_ADC_ChannelMap_t *pstChData = pstGetADCChannelData(eADCModule, eChannel);
    sT_TrigSlotCtrl_t *pstTrig = &pstADCHW->staTrigSlotCtrl[pstChData->stOwner.eTrigSlot];
    
    const sT_ADC_CommandConfig_t *pstCmdInfo = pstGetCommandConfig(pstChData->stOwner.eCommandId, pstTrig->pstCMDHead);
    return pstCmdInfo;
}

bool bGet_ADCValue(eADC_Module_t eModule, eADC_Channel_t eChannel, uint16_t *puiValue, eADC_ValueType_t eValType)
{
    if(!bValidate_ADCGetRequest(eModule, eChannel, puiValue, eValType))
        return false;

    stADC_HWmodConfig_t *pstADCModule = pstGetADCModule(eModule);
    if(pstADCModule == NULL)
    {
        return false;
    }

    sT_ADC_ChannelMap_t *pstChData = pstGetADCChannelData(eModule, eChannel);
    if(pstChData == NULL)
    {
        return false;
    }
    sT_ADC_ChMinMax_t *pstMin = &pstChData->stStats.stTMinVal;
    sT_ADC_ChMinMax_t *pstMax = &pstChData->stStats.stTMaxVal;

    switch (eValType)
    {
        case eADC_Val:
            *puiValue = atomic_load_explicit(&pstChData->stValue.uiADCVal, memory_order_acquire);
            break;
        case eADC_Max:
            if(bIs_Msgq_Full())
                return false;
            *puiValue = atomic_load_explicit(&pstMax->uiADCVal, memory_order_acquire);
            break;
        case eADC_Min:
            if(bIs_Msgq_Full())
                return false;
            *puiValue = atomic_load_explicit(&pstMin->uiADCVal, memory_order_acquire);
            break;
        case eADC_Avg:
            if(bIs_Msgq_Full())
                return false;
            *puiValue = atomic_load_explicit(&pstChData->stStats.stTAvgVal.uiADCVal, memory_order_acquire);
            break;
        case eADC_RMS:
            if(bIs_Msgq_Full())
                return false;
            *puiValue = atomic_load_explicit(&pstChData->stStats.stTRMSVal.uiADCVal, memory_order_acquire);
            break;      
        default:
            *puiValue = 0;
            return false;
    }

    return true;
}

static bool bValidate_ADCGetRequest(eADC_Module_t eModule, eADC_Channel_t eChannel, uint16_t *puiValue, eADC_ValueType_t eValType)
{
    if(eValType < 0 || eValType >= eNUMBER_OF_ADC_VAL_TYPEs)
    {
        FHALT("Invalid Measurement Type @Type: %d", eValType);
        return false;
    }
    if(puiValue == NULL)
    {
        FHALT("Invalid Pointer Reference");
        return false;
    }
    if(eModule < 0 || eModule >= eNUMBER_OF_ADC_MODULEs)
    {
        FHALT("Invalid Index for ADC Module: %d", eModule);
        return false;        
    }
    if(eChannel < 0 || eChannel >= eNUMBER_OF_ADC_CHANNELs)
    {
        FHALT("Invalid Index for ADC Channel: %d", eChannel);
        return false;        
    }
    if(!bIsADCInitialized(eModule))
    {
        FHALT("ADC Module[%d] not initialized", eModule);
        return false;        
    }
    if(!bIsADC_ChannelUsed(eModule, eChannel))
    {
        FHALT("Requested Ch[%d] in ADC Module[%d] not used", eChannel, eModule);
        return false;        
    }

    return true;
}

bool bSet_ADCSW_Trig(eADC_Module_t eADCModule, eADC_TrigSlot_t eTrigSlot)
{
    if(eADCModule >= eNUMBER_OF_ADC_MODULEs || eTrigSlot >= eNUMBER_OF_ADC_TRIG_SLOTs)
    {
        FHALT("Invalid Parameters for SW Trigger(ADC: %d, TrigSlot: %d)", eADCModule, eTrigSlot);
        return false;        
    }

    if(!bIsADCInitialized(eADCModule))
    {
        FHALT("ADC[%d] is not initialized", eADCModule);
        return false;         
    }

    eADC_TrigSrcType_t eTrigType = eGet_TrigSlot_TrigType(eADCModule, eTrigSlot);
    if(eTrigType != eTrigSrc_Software)
    {
        FHALT("ADC[%d] HW Trig enabled for TrigSlot[%d]", eADCModule, eTrigSlot);
        return false;
    }
    if(!bIs_TrigSlotEnabled(eADCModule, eTrigSlot))
    {
        FHALT("ADC[%d] TrigSlot[%d] is disabled", eADCModule, eTrigSlot);
        return false;
    }

    ADC_Type *pstADCBase = pstGetADCBase(eADCModule);
    pstADCBase->SWTRIG = (uint32_t)(1U << eTrigSlot);
    return true;
}

void vInit_ADC(sT_ADC_ModuleConfig_t *pstADCModuleConfig)
{
    if(pstADCModuleConfig == NULL)
    {
        FHALT("ADC Module Config pointer is NULL. Cannot initialize ADC module.\n");
        return;
    }

    pstADCModuleConfig->bIsConfigOk = true; // Set this flag based on actual configuration validation
    vValidate_ADCConfig(pstADCModuleConfig);
    if(!pstADCModuleConfig->bIsConfigOk)
    {
        vRelease_CMDMemory_AtFailure(pstADCModuleConfig);
        return;
    }

    if(bIsADCInitialized(pstADCModuleConfig->eADCModule))
    {
        FHALT("ADC[%d] is already initialized and the Lib will DeInitialize Current Configurations now", pstADCModuleConfig->eADCModule);
        vDeInit_ADC(pstADCModuleConfig->eADCModule);
    }    

    stADC_HWmodConfig_t *pstHWConfig = pstGetADCModule(pstADCModuleConfig->eADCModule);
    if(pstHWConfig == NULL)
    {
        FHALT("ADC Hardware Config pointer is NULL. Cannot initialize ADC module.\n");
        pstADCModuleConfig->bIsConfigOk = false;
        vRelease_CMDMemory_AtFailure(pstADCModuleConfig);
        return;
    }

    pstHWConfig->pstADCBase = pstGetADCBase(pstADCModuleConfig->eADCModule);
    if(pstHWConfig->pstADCBase == NULL)
    {
        FHALT("Failed to get ADC base address. Cannot initialize ADC module.\n");
        pstADCModuleConfig->bIsConfigOk = false;
        vRelease_CMDMemory_AtFailure(pstADCModuleConfig);
        return;
    }
    ADC_Type *pstADCBase = pstHWConfig->pstADCBase;

    if(!bADC_Init(pstADCBase, pstHWConfig, pstADCModuleConfig))
    {
        FHALT("ADC Module initialization failed.\n");
        pstADCModuleConfig->bIsConfigOk = false;
        vRelease_CMDMemory_AtFailure(pstADCModuleConfig);
        return;
    }

    if(!bADC_InitCommandConfig(pstADCBase, pstHWConfig, pstADCModuleConfig))
    {
        FHALT("ADC Command configuration failed.\n");
        pstADCModuleConfig->bIsConfigOk = false;
        pstHWConfig->bIsADCInitialized = false;
        vDeInit_ADC(pstADCModuleConfig->eADCModule);
        vRelease_CMDMemory_AtFailure(pstADCModuleConfig);
        return;
    }

    if(!bADC_ResultReadBackConfig(pstADCBase, pstADCModuleConfig->eADCModule, pstADCModuleConfig))
    {
        FHALT("ADC Command configuration failed.\n");
        pstADCModuleConfig->bIsConfigOk = false;
        pstHWConfig->bIsADCInitialized = false;
        vDeInit_ADC(pstADCModuleConfig->eADCModule);
        vRelease_CMDMemory_AtFailure(pstADCModuleConfig);
        return;        
    }
    
    pstADCModuleConfig->bIsConfigOk = true;
    pstHWConfig->bIsADCInitialized = true;
    vRelease_CMDMemory_AtFailure(pstADCModuleConfig);
}

static void vRelease_CMDMemory_AtFailure(sT_ADC_ModuleConfig_t *pstADCModuleConfig)
{
    if(pstADCModuleConfig == NULL)
        return;

    for(uint8_t i = eTrig_Slot_0; i < eNUMBER_OF_ADC_TRIG_SLOTs; i++)
    {
        vRelease_CMDBuffers(
            &pstADCModuleConfig->staTrigConfig[i].pstTHeadCmdConfig);
    }
}

static bool bADC_ResultReadBackConfig(ADC_Type *pstADCBase, eADC_Module_t eADCModule, sT_ADC_ModuleConfig_t *pstADCModuleConfig)
{    
    if(pstADCBase == NULL || pstADCModuleConfig == NULL)
    {
        FHALT("Null Reference Pointer");
        return false;
    }

    if(!bSet_TrigCompletionInterrupts(pstADCBase, eADCModule, pstADCModuleConfig))
    {
        return false;
    }

    sT_ADCNotify_Ctrl_t *pstTNotifyCtrlConfig = &pstADCModuleConfig->stTNotifyCtrl;
    switch(pstTNotifyCtrlConfig->eNotificationType)
    {
        case eNotification_Polling:
            return true;
        case eNotification_Interrupt:
            return bConfig_ForInterrupt(pstADCBase, eADCModule, pstTNotifyCtrlConfig->ADCNotify_t.stTInterruptCtrl);
        case eNotification_DMA:
            return bConfig_ForDMA(pstADCBase, eADCModule, pstTNotifyCtrlConfig->ADCNotify_t.stTDMACtrl);
        default:
            FHALT("Invalid Result Read Control Type @Type: %d", pstTNotifyCtrlConfig->eNotificationType);
            return false;
    }
}

static bool bSet_TrigCompletionInterrupts(ADC_Type *pstADCBase, eADC_Module_t eADCModule, sT_ADC_ModuleConfig_t *pstADCModuleConfig)
{
    stADC_HWmodConfig_t *pstADCModule = pstGetADCModule(eADCModule);
    sT_ADC_TrigConfig_t *pstTrgiConfig = pstADCModuleConfig->staTrigConfig;
    uint32_t uiInterMask = 0;

    for(uint8_t i = eTrig_Slot_0; i < eNUMBER_OF_ADC_TRIG_SLOTs; i++)
    {
        if(!pstTrgiConfig[i].bIsTrigSlotEnabled || !pstTrgiConfig[i].bEnTrigCompletionNotifyReq)
            continue;

        switch (i)
        {
            case eTrig_Slot_0:
                uiInterMask |= kLPADC_Trigger0CompletionInterruptEnable;                
                break;
            case eTrig_Slot_1:
                uiInterMask |= kLPADC_Trigger1CompletionInterruptEnable;                 
                break;
            case eTrig_Slot_2:
                uiInterMask |= kLPADC_Trigger2CompletionInterruptEnable;                 
                break;
            case eTrig_Slot_3:
                uiInterMask |= kLPADC_Trigger3CompletionInterruptEnable;                
                break;            
            default:
                break;
        }
    }

    if(uiInterMask == 0)
        return true;

    uiInterMask |= kLPADC_TriggerExceptionInterruptEnable;
    pstADCModule->uiGlobalIntrMask = uiInterMask;
    return true;
}

static void vEnable_ADC_TrigCompletionInterrupts(ADC_Type *pstADCBase, eADC_Module_t eADCModule)
{
    stADC_HWmodConfig_t *pstHWConfig = pstGetADCModule(eADCModule);
    if(pstHWConfig->uiGlobalIntrMask == 0)
        return;

    LPADC_EnableInterrupts(pstADCBase, 
                           pstHWConfig->uiGlobalIntrMask);

    vEnable_ADC_IRQ(eADCModule);
}

static void vEnable_ADC_IRQ(eADC_Module_t eADCModule)
{
    switch(eADCModule)
    {
        case eADC_ADC0:
            IRQ_CONNECT(ADC0_IRQn, ADC_IRQ_PRIORITY, vADC0_ISR, NULL, 0);
            irq_enable(ADC0_IRQn);
            break;
        case eADC_ADC1:
            IRQ_CONNECT(ADC1_IRQn, ADC_IRQ_PRIORITY, vADC1_ISR, NULL, 0);
            irq_enable(ADC1_IRQn);
            break;
        default:
            FHALT("Invalid ADC Module[%d]", eADCModule);
            break;

    }    
}

static bool bConfig_ForDMA(ADC_Type *pstADCBase, eADC_Module_t eADCModule, const sT_ADCNotify_DMA_t stTDMACtrl)
{
    int iRet = 0;

    stADC_HWmodConfig_t *pstHWConfig = pstGetADCModule(eADCModule);
    if(pstHWConfig == NULL)
    {
        FHALT("ADC Hardware Config pointer is NULL");
        return false;
    }
    if((pstADCBase == NULL) || (stTDMACtrl.uiaResultBuffer == NULL) || (stTDMACtrl.uiLen == 0U))
    {
        FHALT("Invalid ADC DMA configuration");
        return false;
    }
    if(eADCModule >= eNUMBER_OF_ADC_MODULEs)
    {
        FHALT("Invalid ADC module for DMA: %d", eADCModule);
        return false;
    }
    if(!device_is_ready(pstaADCDMADev[eADCModule]))
    {
        FHALT("ADC DMA device is not ready");
        return false;
    }

    sT_DMANotifyCtrl *pstDMACtrl = &pstHWConfig->stTADCHWNotifyCtrl.NotifyCtrl.stTDMANotifyCtrl;
    sT_DMANotifyCtrl stTTemDMACtrl = {0};
    struct dma_block_config *pstDMABloock = &stTTemDMACtrl.stADCDMABlockConfig_t;
    struct dma_config *pstDMA = &stTTemDMACtrl.stADCDMAConfig_t;
    
    switch(pstHWConfig->stTADCHWNotifyCtrl.eNotificationType)
    {
        case eNotification_Interrupt:
            FHALT("ADC[%d] is already configured for Interrupt based notifications", eADCModule);
            vDisable_ADC_Interrupts(pstADCBase, eADCModule);
            break;
        case eNotification_DMA:
        default:
            break;
    }
    
    memset(pstDMABloock, 0, sizeof(struct dma_block_config));
    pstDMABloock->source_address = (uint32_t)&pstADCBase->RESFIFO;
    pstDMABloock->dest_address = (uint32_t)stTDMACtrl.uiaResultBuffer;
    pstDMABloock->block_size = (uint32_t)stTDMACtrl.uiLen * sizeof(uint32_t);
    pstDMABloock->source_addr_adj = DMA_ADDR_ADJ_NO_CHANGE;
    pstDMABloock->dest_addr_adj = DMA_ADDR_ADJ_INCREMENT;
    pstDMABloock->next_block = NULL;

    memset(pstDMA, 0, sizeof(struct dma_config));
    pstDMA->dma_slot = uiaADCDMASlot[eADCModule];
    pstDMA->channel_direction = PERIPHERAL_TO_MEMORY;
    pstDMA->source_data_size = sizeof(uint32_t);
    pstDMA->dest_data_size = sizeof(uint32_t);
    pstDMA->source_burst_length = sizeof(uint32_t);
    pstDMA->dest_burst_length = sizeof(uint32_t);
    pstDMA->block_count = 1U;
    pstDMA->head_block = pstDMABloock;
    pstDMA->dma_callback = (eADCModule == eADC_ADC0)?vADC0_DMA_Callback : vADC1_DMA_Callback;
    pstDMA->user_data = pstHWConfig;
    pstDMA->complete_callback_en = 1U;
    pstDMA->error_callback_dis = 0U;
    pstDMA->cyclic = 1U;

    stTTemDMACtrl.puiDMABuffer = stTDMACtrl.uiaResultBuffer;
    stTTemDMACtrl.uiDMALen = stTDMACtrl.uiLen;
    stTTemDMACtrl.bIsDMAError = false;

    vEnable_ADC_TrigCompletionInterrupts(pstADCBase, eADCModule);

    (void)dma_stop(pstaADCDMADev[eADCModule], uiaADCDMAChannel[eADCModule]);
    iRet = dma_config(pstaADCDMADev[eADCModule], uiaADCDMAChannel[eADCModule], pstDMA);
    if(iRet != 0)
    {
        FHALT("ADC DMA configuration failed: %d", iRet);
        return false;
    }

    iRet = dma_start(pstaADCDMADev[eADCModule], uiaADCDMAChannel[eADCModule]);
    if(iRet != 0)
    {
        FHALT("ADC DMA start failed: %d", iRet);
        return false;
    }

    memcpy(pstDMACtrl, &stTTemDMACtrl, sizeof(sT_DMANotifyCtrl));
    pstDMACtrl->stADCDMAConfig_t.head_block = &pstDMACtrl->stADCDMABlockConfig_t;
    pstHWConfig->stTADCHWNotifyCtrl.eNotificationType = eNotification_DMA;
    LPADC_EnableFIFOWatermarkDMA(pstADCBase, true);

    return true;
}

static void vADC0_DMA_Callback(const struct device *dev, void *pUserData, uint32_t uiChannel, int iStatus)
{

}

static void vADC1_DMA_Callback(const struct device *dev, void *pUserData, uint32_t uiChannel, int iStatus)
{

}

static bool bConfig_ForInterrupt(ADC_Type *pstADCBase, eADC_Module_t eADCModule, const sT_ADCNotify_Interrupt_t stTInterruptCtrl)
{
    stADC_HWmodConfig_t *pstHWConfig = pstGetADCModule(eADCModule);

    if(pstHWConfig == NULL)
    {
        FHALT("ADC Hardware Config pointer is NULL");
        return false;
    }

    pstHWConfig->stTADCHWNotifyCtrl.eNotificationType = eNotification_Interrupt;
    pstHWConfig->stTADCHWNotifyCtrl.NotifyCtrl.uiInterMask |=   (uint32_t)(pstHWConfig->uiGlobalIntrMask | 
                                                                (uint32_t)(kLPADC_FIFO0WatermarkInterruptEnable | kLPADC_ResultFIFO0OverflowInterruptEnable));
    pstHWConfig->uiGlobalIntrMask = pstHWConfig->stTADCHWNotifyCtrl.NotifyCtrl.uiInterMask;
       
    LPADC_EnableInterrupts(pstADCBase, 
                           pstHWConfig->stTADCHWNotifyCtrl.NotifyCtrl.uiInterMask);
    
    vInit_ADC_Thread();
    vEnable_ADC_IRQ(eADCModule);
    return true;
}

static void vInit_ADC_Thread( void )
{
    if(bIsADCThreadInit)
        return;

    kADC_ThreadId = k_thread_create(&kADC_Statistic_Thread_t, thread_ADC_Statistics,
                                    K_THREAD_STACK_SIZEOF(thread_ADC_Statistics), 
                                    vCompute_ADC_Statistics_ForThread,
                                    NULL, NULL, NULL, ADC_STATISTIC_THREAD_PRIORITY, 0, K_NO_WAIT);
    k_msgq_purge(&kADCMeasDataQueue);
    vSet_ADCThread_InitFlag();    
}

void vDeInit_ADC_Thread( void )
{
    if(!bIsADCThreadInit)
        return;

    uint8_t uiCount = 0;
    for(uint8_t i = 0; i < eNUMBER_OF_ADC_MODULEs; i++)
    { 
        if(!staADC_HWConfig[i].bIsADCInitialized)
            uiCount++;
    }
    if(uiCount < eNUMBER_OF_ADC_MODULEs)
        return;

    k_thread_abort(kADC_ThreadId);
    k_msgq_purge(&kADCMeasDataQueue);
    vResetSet_ADCThread_InitFlag();
}

static void vCompute_ADC_Statistics_ForThread( void *p1, void *p2, void *p3 )
{
    sT_ADC_StatisticsSample_t stTADCSample = {0};

    while(1)
    {
        if(k_msgq_get(&kADCMeasDataQueue, &stTADCSample, K_FOREVER) != 0)
            continue;
        
        sT_ADC_ChannelMap_t *pstChData = pstGetADCChannelData(stTADCSample.eModule, stTADCSample.eChannel);
        if(pstChData == NULL)
            continue;

        k_mutex_lock(&kADCStatisticsMutex, K_FOREVER);

        if(stTADCSample.uiGeneration != atomic_load_explicit(&uiaADCStatisticsGeneration[stTADCSample.eModule],
                                                              memory_order_acquire))
        {
            k_mutex_unlock(&kADCStatisticsMutex);
            continue;
        }

        sT_TrigSlotCtrl_t *pstTrigSlot = pstGetTrigSlotInfo(stTADCSample.eModule, pstChData->stOwner.eTrigSlot);
        sT_ADC_ChannelStats_t *pstStatistics = &pstChData->stStats;

        vCompute_AVG_Value(pstStatistics, pstTrigSlot, &stTADCSample);
        vCompute_RMS_Value(pstStatistics, pstTrigSlot, &stTADCSample);
        vCompute_Max_Value(pstStatistics, pstTrigSlot, &stTADCSample);
        vCompute_Min_Value(pstStatistics, pstTrigSlot, &stTADCSample);

        k_mutex_unlock(&kADCStatisticsMutex);
    }
}

static void vCompute_RMS_Value(sT_ADC_ChannelStats_t *pstStatistics, sT_TrigSlotCtrl_t *pstTrigSlot,
                               sT_ADC_StatisticsSample_t *pstTADCSample)
{
    sT_ADC_ChAvgRMS_t *pstRMS = &pstStatistics->stTRMSVal;

    pstRMS->uiADCVal_Sum += (uint64_t)((uint64_t)pstTADCSample->uiADCValue * pstTADCSample->uiADCValue);
    pstRMS->uiSampleCount++;
    uint16_t uiMaxSampleCount = atomic_load_explicit(&pstRMS->uiMaxSampleCount, memory_order_acquire);
    
    if(pstRMS->uiSampleCount >= uiMaxSampleCount && !bIsTigSlot_Paused(pstTrigSlot))
    {
        pstRMS->uiSampleCount = (pstRMS->uiSampleCount > 0U) ? pstRMS->uiSampleCount : 1U;
        uint16_t uiADCRMSVal = sqrt((double)(pstRMS->uiADCVal_Sum / pstRMS->uiSampleCount));
        atomic_store_explicit(&pstRMS->uiADCVal, uiADCRMSVal, memory_order_release);
        
        pstRMS->uiSampleCount = 0U;
        pstRMS->uiADCVal_Sum = 0U;
    }
    if(bIsTigSlot_Paused(pstTrigSlot))
    {
        pstRMS->uiSampleCount = 0U;
        pstRMS->uiADCVal_Sum = 0U;
    } 

}

static void vCompute_AVG_Value(sT_ADC_ChannelStats_t *pstStatistics, sT_TrigSlotCtrl_t *pstTrigSlot,
                               sT_ADC_StatisticsSample_t *pstTADCSample)
{
    sT_ADC_ChAvgRMS_t *pstAvg = &pstStatistics->stTAvgVal;

    pstAvg->uiADCVal_Sum += pstTADCSample->uiADCValue;
    pstAvg->uiSampleCount++;

    uint16_t uiMaxSampleCount = atomic_load_explicit(&pstAvg->uiMaxSampleCount, memory_order_acquire);
    if(pstAvg->uiSampleCount >= uiMaxSampleCount && !bIsTigSlot_Paused(pstTrigSlot))
    {
        pstAvg->uiSampleCount = (pstAvg->uiSampleCount > 0U) ? pstAvg->uiSampleCount : 1U;
        uint16_t uiADCAvgVal = (uint16_t)(pstAvg->uiADCVal_Sum / pstAvg->uiSampleCount);
        atomic_store_explicit(&pstAvg->uiADCVal, uiADCAvgVal, memory_order_release);
        
        pstAvg->uiSampleCount = 0U;
        pstAvg->uiADCVal_Sum = 0U;
    }
    if(bIsTigSlot_Paused(pstTrigSlot))
    {
        pstAvg->uiSampleCount = 0U;
        pstAvg->uiADCVal_Sum = 0U;
    }    
}


static void vCompute_Min_Value(sT_ADC_ChannelStats_t *pstStatistics, sT_TrigSlotCtrl_t *pstTrigSlot, sT_ADC_StatisticsSample_t *pstTADCSample)
{
    if(pstStatistics == NULL || pstTADCSample == NULL || pstTrigSlot == NULL)
    {
        FHALT("Null reference pointer");
        return;
    }
    if(bIsTigSlot_Paused(pstTrigSlot))
        return;

    sT_ADC_ChMinMax_t *pstMin = &pstStatistics->stTMinVal;
    uint64_t uiCurrentTime_ms = k_uptime_get();
    uint32_t uiReleaseTime_ms = 0U;

    if(pstTADCSample->uiADCValue < pstMin->uiADCVal)
    {
        pstMin->uiADCVal = pstTADCSample->uiADCValue;
        pstMin->uiLastTriggerTime_ms = k_uptime_get();
        return;
    }

    uiReleaseTime_ms = atomic_load_explicit(&pstMin->uiReleaseDelay_ms, memory_order_acquire);

    if(uiReleaseTime_ms == 0U)
        uiReleaseTime_ms = ADC_STATS_DEAFULT_MIN_RELEASE_TIME_ms;

    if(!bIsTimeOut(pstMin->uiLastTriggerTime_ms, uiCurrentTime_ms, uiReleaseTime_ms))
        return;
        
    pstMin->uiADCVal = pstTADCSample->uiADCValue;
    pstMin->uiLastTriggerTime_ms = k_uptime_get();    
}

static void vCompute_Max_Value(sT_ADC_ChannelStats_t *pstStatistics, sT_TrigSlotCtrl_t *pstTrigSlot, sT_ADC_StatisticsSample_t *pstTADCSample)
{
    if(pstStatistics == NULL || pstTADCSample == NULL || pstTrigSlot == NULL)
    {
        FHALT("Null reference pointer");
        return;
    }
    if(bIsTigSlot_Paused(pstTrigSlot))
        return;

    sT_ADC_ChMinMax_t *pstMax = &pstStatistics->stTMaxVal;
    uint64_t uiCurrentTime_ms = k_uptime_get();
    uint32_t uiReleaseTime_ms = 0U;

    if(pstTADCSample->uiADCValue > pstMax->uiADCVal)
    {
        pstMax->uiADCVal = pstTADCSample->uiADCValue;
        pstMax->uiLastTriggerTime_ms = k_uptime_get();
        return;

    }

    uiReleaseTime_ms = atomic_load_explicit(&pstMax->uiReleaseDelay_ms, memory_order_acquire);

    if(uiReleaseTime_ms == 0U)
        uiReleaseTime_ms = ADC_STATS_DEAFULT_MAX_RELEASE_TIME_ms;

    if(!bIsTimeOut(pstMax->uiLastTriggerTime_ms, uiCurrentTime_ms, uiReleaseTime_ms))
        return;
        
    pstMax->uiADCVal = pstTADCSample->uiADCValue;
    pstMax->uiLastTriggerTime_ms = k_uptime_get();
}

static void vDisable_ADC_Interrupts(ADC_Type *pstADCBase, eADC_Module_t eADCModule)
{
    sT_ADCHWNotifyCtrl_t *pstHWCtrl = &staADC_HWConfig[eADCModule].stTADCHWNotifyCtrl;

    LPADC_DisableInterrupts(pstADCBase, pstHWCtrl->NotifyCtrl.uiInterMask);

    switch(eADCModule)
    {
        case eADC_ADC0:
            irq_disable(ADC0_IRQn);
            break;
        case eADC_ADC1:
            irq_disable(ADC1_IRQn);
            break;
        default:
            FHALT("Invalid ADC Module : %d", eADCModule);
            break;       
    }
    
}

static void vADC_ISRHandler(eADC_Module_t eADCModule)
{
    lpadc_conv_result_t stResult;
    stADC_HWmodConfig_t *pstHWConfig = pstGetADCModule(eADCModule);
    ADC_Type *pstADCBase = pstGetADCBase(eADCModule);

    uint32_t uiResStatus = LPADC_GetStatusFlags(pstADCBase);
    uint32_t uiTrigStatus = LPADC_GetTriggerStatusFlags(pstADCBase);
    uint32_t uiTrigFlag = 0;

    if(uiResStatus & kLPADC_ResultFIFO0ReadyFlag)
    {
        while(LPADC_GetConvResult(pstADCBase, &stResult))
        {
            vUpdate_ADC_Value(eADCModule, stResult);
        }
    }

    if(uiResStatus & kLPADC_ResultFIFOOverflowFlag)
    {
        vDrain_ADC_FIFO(pstADCBase, &stResult);
        LPADC_ClearStatusFlags(pstADCBase, kLPADC_ResultFIFOOverflowFlag);
        vNotify_ADCOverflow(eADCModule, true);
    }

    if(uiResStatus & kLPADC_TriggerCompletionFlag)
    {
        uiTrigFlag = (eADC_TrigSlot_t)(((uint32_t)(uiTrigStatus >> 16) & (uint32_t)0x0000000F));
        if(pstHWConfig->pfADCTrgiCallback != NULL)
        {
            pstHWConfig->pfADCTrgiCallback(eADCModule, uiTrigFlag, NULL);
        }
        LPADC_ClearTriggerStatusFlags(pstADCBase, (uiTrigStatus & ADC_TRIG_COMPLETE_MASK));
        LPADC_ClearStatusFlags(pstADCBase, kLPADC_TriggerCompletionFlag);
    }

    if(uiResStatus & kLPADC_TriggerExceptionFlag)
    {
        if(pstHWConfig->pfADCTrgiCallback != NULL)
        {
            pstHWConfig->pfADCTrgiCallback(eADCModule, (uiTrigStatus & ADC_TRIG_EXCEPTION_MASK), NULL);
        }
        LPADC_ClearTriggerStatusFlags(pstADCBase, (uiTrigStatus & ADC_TRIG_EXCEPTION_MASK));
        LPADC_ClearStatusFlags(pstADCBase, kLPADC_TriggerExceptionFlag);
    }

}

static inline void vUpdate_ADC_Value(eADC_Module_t eADCModule, lpadc_conv_result_t stResult)
{
    sT_ADC_ChannelMap_t *pstChData = pstGetChInfo_ByCmdId_TrigSlot(eADCModule, 
                                                                   (eADC_TrigSlot_t)stResult.triggerIdSource, 
                                                                   (eADC_Command_t)stResult.commandIdSource, 
                                                                   stResult.loopCountIndex);
    if(pstChData == NULL)
    {
        FHALT("Invalid Channel -> Channel Map returns NULL");
        return;
    }

    atomic_store_explicit(&pstChData->stValue.uiADCVal, stResult.convValue, memory_order_release);

    sT_ADC_StatisticsSample_t stTADCSample = {
        .eModule = eADCModule,
        .eChannel = pstChData->stOwner.eChannel,
        .uiADCValue = stResult.convValue,
        .uiGeneration = atomic_load_explicit(&uiaADCStatisticsGeneration[eADCModule], memory_order_acquire)
    };

    if(k_msgq_put(&kADCMeasDataQueue, &stTADCSample, K_NO_WAIT) != 0)
    {
        vSet_Msgq_FullFlag();
    }
}

static inline void vSet_Msgq_FullFlag( void )
{
    atomic_store_explicit(&bADCStatisticsOverflow, true, memory_order_release);
}
static inline void vClear_Msgq_FullFlag( void )
{
    atomic_store_explicit(&bADCStatisticsOverflow, false, memory_order_release);    
}
static inline bool bIs_Msgq_Full( void )
{
    bool isFull = atomic_load_explicit(&bADCStatisticsOverflow, memory_order_acquire);
    return isFull;
}

void vClear_ADCStatisticsOverflow( void )
{
    for(uint8_t i = eADC_ADC0; i < eNUMBER_OF_ADC_MODULEs; i++)
    {
        if(!bIsADCInitialized(i))
            continue;

        if(!bPause_ADCModule(i))
        {
            vResume_All_ADCModules();
            return;
        }
    }

    for(uint8_t i = eADC_ADC0; i < eNUMBER_OF_ADC_MODULEs; i++)
    {
        if(!bIsADCInitialized(i))
            continue;
        if(!bWait_ADCModuleIdle(i))
        {
            vResume_All_ADCModules();
            return;
        }
    }
    k_mutex_lock(&kADCStatisticsMutex, K_FOREVER);

    for(uint8_t i = eADC_ADC0; i < eNUMBER_OF_ADC_MODULEs; i++)
    {
        atomic_fetch_add_explicit(&uiaADCStatisticsGeneration[i], 1U, memory_order_release);
    }
    k_msgq_purge(&kADCMeasDataQueue);

    for(uint8_t i = eADC_ADC0; i < eNUMBER_OF_ADC_MODULEs; i++)
    {
        for(uint8_t j = eADC_Ch_0; j < eNUMBER_OF_ADC_CHANNELs; j++)
        {
            if(!bIsADC_ChannelUsed(i, j))
                continue;
            sT_ADC_ChannelMap_t *pstChData = pstGetADCChannelData(i, j);

            pstChData->stStats.stTAvgVal.uiADCVal_Sum = 0U;
            pstChData->stStats.stTAvgVal.uiSampleCount = 0U;
            atomic_store_explicit(&pstChData->stStats.stTAvgVal.uiADCVal, 0U, memory_order_release);

            pstChData->stStats.stTRMSVal.uiADCVal_Sum = 0U;
            pstChData->stStats.stTRMSVal.uiSampleCount = 0U;
            atomic_store_explicit(&pstChData->stStats.stTRMSVal.uiADCVal, 0U, memory_order_release);

            atomic_store_explicit(&pstChData->stStats.stTMaxVal.uiADCVal, 0, memory_order_release);            
            pstChData->stStats.stTMaxVal.uiLastTriggerTime_ms = 0U;

            atomic_store_explicit(&pstChData->stStats.stTMinVal.uiADCVal, UINT16_MAX, memory_order_release);
            pstChData->stStats.stTMinVal.uiLastTriggerTime_ms = 0U;
        }
    }

    vClear_Msgq_FullFlag();
    k_mutex_unlock(&kADCStatisticsMutex);

    vResume_All_ADCModules();
}

static bool bPause_ADCModule(eADC_Module_t eModule)
{
    if(eModule < 0 || eModule >= eNUMBER_OF_ADC_MODULEs)
    {
        FHALT("Invalid ADC Module : %d", eModule);
        return false;
    }
    if(!bIsADCInitialized(eModule))
    {
        FHALT("Invalid ADC Module[%d] not initialized", eModule);
        return false;        
    }

    stADC_HWmodConfig_t *pstADCModule = pstGetADCModule(eModule);
    ADC_Type *pstADCBase = pstADCModule->pstADCBase;

    for(uint8_t j = 0; j < eNUMBER_OF_ADC_TRIG_SLOTs; j++)
    {
        sT_TrigSlotCtrl_t *pstTrigSlotCtrl = &pstADCModule->staTrigSlotCtrl[j];
        if(!pstTrigSlotCtrl->bIsEnabled)
            continue;

        switch(pstTrigSlotCtrl->eaTrigSlotType)
        {
            case eTrigSrc_Software:
                vPasue_ActiveTrigSlot(pstTrigSlotCtrl);
                break;
            case eTrigSrc_Hardware:
                pstADCBase->TCTRL[pstTrigSlotCtrl->eSlotId] &= ~ADC_TCTRL_HTEN_MASK;
                vPasue_ActiveTrigSlot(pstTrigSlotCtrl);
                break;
            default:
                break;
        }
    }

    return true;
}

static bool bWait_ADCModuleIdle(eADC_Module_t eModule)
{
    ADC_Type *pstBase = pstGetADCBase(eModule);
    int64_t iStartTime = k_uptime_get();

    while((LPADC_GetStatusFlags(pstBase) & kLPADC_ActiveFlag) != 0U)
    {
        if((k_uptime_get() - iStartTime) >= ADC_IDLE_TIMEOUT_MS)
        {
            return false;
        }
    }

    return true;
}

static void vResume_ADCModule(eADC_Module_t eModule)
{
    if((eModule < 0) || (eModule >= eNUMBER_OF_ADC_MODULEs) || !bIsADCInitialized(eModule))
        return;

    stADC_HWmodConfig_t *pstADCModule = pstGetADCModule(eModule);
    ADC_Type *pstADCBase = pstADCModule->pstADCBase;

    for(uint8_t i = 0; i < eNUMBER_OF_ADC_TRIG_SLOTs; i++)
    {
        sT_TrigSlotCtrl_t *pstTrigSlotCtrl = &pstADCModule->staTrigSlotCtrl[i];
        if(!bIsTigSlot_Paused(pstTrigSlotCtrl))
            continue;

        switch(pstTrigSlotCtrl->eaTrigSlotType)
        {
            case eTrigSrc_Software:
                vResume_PausedTrigSlot(pstTrigSlotCtrl);
                break;
            case eTrigSrc_Hardware:
                pstADCBase->TCTRL[pstTrigSlotCtrl->eSlotId] |= ADC_TCTRL_HTEN_MASK;
                vResume_PausedTrigSlot(pstTrigSlotCtrl);
                break;
            default:
                break;
        }
    }
}

static void vResume_All_ADCModules( void )
{
    for(uint8_t i = eADC_ADC0; i < eNUMBER_OF_ADC_MODULEs; i++)
    {
        vResume_ADCModule(i);
    }
}

static inline bool bIsTigSlot_Paused(sT_TrigSlotCtrl_t *pstTrigSlotCtrl)
{
    if(pstTrigSlotCtrl == NULL)
        return false;
    bool paused = atomic_load_explicit(&pstTrigSlotCtrl->bIsPaused, memory_order_acquire);
    return (!pstTrigSlotCtrl->bIsEnabled && paused);
}

static inline void vResume_PausedTrigSlot(sT_TrigSlotCtrl_t *pstTrigSlotCtrl)
{
    if(pstTrigSlotCtrl == NULL)
        return;
    bool paused = atomic_load_explicit(&pstTrigSlotCtrl->bIsPaused, memory_order_acquire);

    if(paused)
        pstTrigSlotCtrl->bIsEnabled = true;
    else
        pstTrigSlotCtrl->bIsEnabled = false;
    atomic_store_explicit(&pstTrigSlotCtrl->bIsPaused, false, memory_order_release);  
}

static inline void vPasue_ActiveTrigSlot(sT_TrigSlotCtrl_t *pstTrigSlotCtrl)
{
    if(pstTrigSlotCtrl == NULL)
        return;
    if(pstTrigSlotCtrl->bIsEnabled)
        atomic_store_explicit(&pstTrigSlotCtrl->bIsPaused, true, memory_order_release);
    else
        atomic_store_explicit(&pstTrigSlotCtrl->bIsPaused, false, memory_order_release);

    pstTrigSlotCtrl->bIsEnabled = false;
}

static inline sT_TrigSlotCtrl_t *pstGetTrigSlotInfo(eADC_Module_t eAdcModule, eADC_TrigSlot_t eSlotId)
{
    stADC_HWmodConfig_t *pstADCConfig = pstGetADCModule(eAdcModule);
    if(pstADCConfig == NULL)
        return NULL;
    return &pstADCConfig->staTrigSlotCtrl[eSlotId];
}

bool bIs_ADCStatisticsOverflowed( void )
{
    return bIs_Msgq_Full();
}

static inline void vDrain_ADC_FIFO(ADC_Type *pstADCBase, lpadc_conv_result_t *pstResult)
{
    while(LPADC_GetConvResult(pstADCBase, pstResult))
    {

    }    
}

static inline void vNotify_ADCOverflow(eADC_Module_t eADCModule, bool bres)
{
    if(staADC_HWConfig[eADCModule].pbOverflowFlag == NULL)
        return;
    atomic_store_explicit(staADC_HWConfig[eADCModule].pbOverflowFlag, bres, memory_order_release);
}

static void vADC0_ISR(const void *pvArg)
{
    ARG_UNUSED(pvArg);
    vADC_ISRHandler(eADC_ADC0);
}

static void vADC1_ISR(const void *pvArg)
{
    ARG_UNUSED(pvArg);
    vADC_ISRHandler(eADC_ADC1);
}

static bool bADC_InitCommandConfig(ADC_Type *pstADCBase, 
                                   stADC_HWmodConfig_t *pstHWConfig, 
                                   sT_ADC_ModuleConfig_t *pstADCModuleConfig)
{
    uint8_t uiTrigSlotIndex = 0;
    if(pstADCModuleConfig == NULL)
    {
        FHALT("ADC Module Config pointer is NULL. Cannot initialize ADC command configuration.\n");
        return false;
    }

    sT_ADC_TrigConfig_t *pstTrigConfig = pstADCModuleConfig->staTrigConfig;
    if(pstTrigConfig == NULL)
    {
        FHALT("ADC Trigger Config pointer is NULL. Cannot initialize ADC command configuration.\n");
        return false;
    }
    
    memset(&pstHWConfig->baCommandConfigStat, false, sizeof(pstHWConfig->baCommandConfigStat));
    for(uiTrigSlotIndex = eTrig_Slot_0; uiTrigSlotIndex < eNUMBER_OF_ADC_TRIG_SLOTs; uiTrigSlotIndex++)
    {
        if(!pstTrigConfig[uiTrigSlotIndex].bIsTrigSlotEnabled)
            continue;

        if(pstTrigConfig[uiTrigSlotIndex].eTrigSrcType <= eTrigSrc_None ||
           pstTrigConfig[uiTrigSlotIndex].eTrigSrcType >= eNUMBER_OF_TRIGGER_TYPEs)
        {
            FHALT("Invalid trigger source type for trigger slot[%d]: %d",
                uiTrigSlotIndex,
                pstTrigConfig[uiTrigSlotIndex].eTrigSrcType);
            return false;
        }        
        if(pstTrigConfig[uiTrigSlotIndex].ePrioLevel < 0 || pstTrigConfig[uiTrigSlotIndex].ePrioLevel >= eNUMBER_OF_PRIORITY_LEVELs)
        {
            FHALT("Invalid Priority Level Defined. Level : %d", pstTrigConfig[uiTrigSlotIndex].ePrioLevel);
            return false;
        }
        if(pstTrigConfig[uiTrigSlotIndex].uiTrigDelay >= ADC_MAX_TRIG_DELAY_ADC_CLK_CYCLEs)
        {
            FHALT("Invalid Trigger Delay Defined. Delay : %d", pstTrigConfig[uiTrigSlotIndex].uiTrigDelay);
            return false;            
        }
        
        sT_ADC_CommandConfig_t *pstCmdNode = pstTrigConfig[uiTrigSlotIndex].pstTHeadCmdConfig;
        if(pstCmdNode == NULL)
        {
            FHALT("ADC Command Config pointer is NULL for trigger slot %d. Cannot initialize ADC command configuration.\n", uiTrigSlotIndex);
            return false;
        }

        while(pstCmdNode != NULL)
        {
            sT_ADC_CMDData_t *pstCmdData = &pstCmdNode->stTCMDData;
            if(pstCmdData == NULL)
            {
                FHALT("Null Pointer reference");
                return false;
            }
            if(pstCmdData->eCommandId == eADC_CMD_None || 
               pstCmdData->eCommandId >= eNUMBER_OF_ADC_COMMANDs)
            {
                FHALT("Command Id[%d] cannot be '0' or be greater than '%d'", pstCmdData->eCommandId, eNUMBER_OF_ADC_COMMANDs);
                return false;
            }

            lpadc_conv_command_config_t stCmdConfig;
            LPADC_GetDefaultConvCommandConfig(&stCmdConfig);

            if(!bConfig_ADCCommand(&stCmdConfig, pstCmdData, pstADCModuleConfig))
            {
                FHALT("Command[%d] configuration failed", pstCmdData->eCommandId);
                return false;
            }
            
            if(bIs_CommandInUse(pstADCModuleConfig->eADCModule, pstCmdData->eCommandId))
            {
                FHALT("Command[%d] already use with ADC Module[%d]", pstCmdData->eCommandId, pstADCModuleConfig->eADCModule);
                return false;                
            }
            
            stCmdConfig.chainedNextCommandNumber = (pstCmdNode->pstNextCommandConfig != NULL)? 
                                                    (uint32_t)pstCmdNode->pstNextCommandConfig->stTCMDData.eCommandId : 
                                                    (uint32_t)eADC_CMD_None;
            LPADC_SetConvCommandConfig(pstADCBase, pstCmdData->eCommandId, &stCmdConfig);
            if(!bSet_CommandForUse(pstADCModuleConfig->eADCModule, pstCmdData->eCommandId))
            {
                FHALT("Command[%d] already use with ADC Module[%d]", pstCmdData->eCommandId, pstADCModuleConfig->eADCModule);
                return false;                 
            }
            if(!bUpdateADCChannelCommandMap(pstADCModuleConfig->eADCModule,
                                            pstCmdData->eChannel,
                                            pstCmdData->uiLoopCount,
                                            pstCmdData->bIsLoopWithChIncrementEnabled,
                                            (eADC_TrigSlot_t)uiTrigSlotIndex,
                                            pstCmdData->eCommandId, 
                                            pstCmdData->uiADCMax_ReleaseTime_ms,
                                            pstCmdData->uiADCMin_ReleaseTime_ms,
                                            pstCmdData->uiSWAvgSampleCount))
            {
                FHALT("ADC channel command map update failed for ADC[%d], Command[%d]",
                      pstADCModuleConfig->eADCModule,
                      pstCmdData->eCommandId);
                return false;
            }
            pstCmdNode = pstCmdNode->pstNextCommandConfig;
        }
        
        pstCmdNode = pstTrigConfig[uiTrigSlotIndex].pstTHeadCmdConfig;

        if(pstTrigConfig[uiTrigSlotIndex].eTrigSlot != uiTrigSlotIndex)
        {
            FHALT("Invalid TrigSlot Defined at Slot: %d", pstTrigConfig[uiTrigSlotIndex].eTrigSlot);
            return false;
        }
        lpadc_conv_trigger_config_t stTrigConfig;        
        LPADC_GetDefaultConvTriggerConfig(&stTrigConfig);

        stTrigConfig.enableHardwareTrigger = (pstTrigConfig[uiTrigSlotIndex].eTrigSrcType == eTrigSrc_Hardware)? true: false;
        stTrigConfig.targetCommandId = pstCmdNode->stTCMDData.eCommandId;
        stTrigConfig.priority = (uint32_t)pstTrigConfig[uiTrigSlotIndex].ePrioLevel;
        stTrigConfig.delayPower = pstTrigConfig[uiTrigSlotIndex].uiTrigDelay;        
        LPADC_SetConvTriggerConfig(pstADCBase, uiTrigSlotIndex, &stTrigConfig);

        if(stTrigConfig.enableHardwareTrigger)
        {
            if(!bHW_TrigSrc_Setup(pstADCBase, pstADCModuleConfig->eADCModule, &pstTrigConfig[uiTrigSlotIndex]))
            {
                FHALT("Trig Source setup failed for the trig slot[%d]", uiTrigSlotIndex);
                return false;
            }
        }
        
        vSet_TrigSlot_TrigType(pstADCModuleConfig->eADCModule, &pstTrigConfig[uiTrigSlotIndex]);
    }
    pstHWConfig->pfADCTrgiCallback = pstADCModuleConfig->pvTrigCompltCallbackFn;
    return true;
}

static void vSet_TrigSlot_TrigType(eADC_Module_t eADCModule, sT_ADC_TrigConfig_t *pstTrigConfig)
{
    stADC_HWmodConfig_t *pstADCModule = pstGetADCModule(eADCModule);
    if((pstADCModule == NULL) || pstTrigConfig == NULL)
        return;

    sT_TrigSlotCtrl_t *pstTrigSlotCtrl = &pstADCModule->staTrigSlotCtrl[pstTrigConfig->eTrigSlot];
    pstTrigSlotCtrl->eSlotId = pstTrigConfig->eTrigSlot;
    pstTrigSlotCtrl->bIsEnabled = true;
    pstTrigSlotCtrl->eaTrigSlotType = pstTrigConfig->eTrigSrcType;
    pstTrigSlotCtrl->pstCMDHead = pstTrigConfig->pstTHeadCmdConfig;
    pstTrigConfig->pstTHeadCmdConfig = NULL;
}

static eADC_TrigSrcType_t eGet_TrigSlot_TrigType(eADC_Module_t eADCModule, eADC_TrigSlot_t eTrigSlot)
{
    if((eADCModule >= eNUMBER_OF_ADC_MODULEs) ||
       (eTrigSlot >= eNUMBER_OF_ADC_TRIG_SLOTs) ||
       !bIsADCInitialized(eADCModule))
    {
        FHALT("ADC[%d] module is not initialized", eADCModule);
        return eNUMBER_OF_TRIGGER_TYPEs;
    }

    stADC_HWmodConfig_t *pstADCModule = pstGetADCModule(eADCModule);
    return pstADCModule->staTrigSlotCtrl[eTrigSlot].eaTrigSlotType;
}

static bool bIs_TrigSlotEnabled(eADC_Module_t eADCModule, eADC_TrigSlot_t eTrigSlot)
{
    if((eADCModule >= eNUMBER_OF_ADC_MODULEs) ||
       (eTrigSlot >= eNUMBER_OF_ADC_TRIG_SLOTs) ||
       !bIsADCInitialized(eADCModule))
    {
        return false;
    }

    return staADC_HWConfig[eADCModule].staTrigSlotCtrl[eTrigSlot].bIsEnabled;
}

static inline bool bSet_CommandForUse(eADC_Module_t eADCModule, eADC_Command_t eCmdId)
{
    if(bIs_CommandInUse(eADCModule, eCmdId))
        return false;
    staADC_HWConfig[eADCModule].baCommandConfigStat[eCmdId] = true;
    return true;
}

static inline bool bIs_CommandInUse(eADC_Module_t eADCModule, eADC_Command_t eCmdId)
{
    bool *bpstCmdArr = staADC_HWConfig[eADCModule].baCommandConfigStat;
    return bpstCmdArr[eCmdId];
}

static bool bHW_TrigSrc_Setup(ADC_Type *pstADCBase, eADC_Module_t eADCmodule, sT_ADC_TrigConfig_t *pstTrigConfig)
{
    if(pstTrigConfig == NULL || pstADCBase == NULL)
    {
        FHALT("Null Pointer Reference");
        return false;
    }

    inputmux_connection_t eMuxConnection = eGetInputMuxConnection(eADCmodule, pstTrigConfig->eTrigSrc);
    if(eMuxConnection == 0U)
    {
        FHALT("Invalid Input Mux Connection(%d)", eMuxConnection);
        return false;
    }

    INPUTMUX_Init(INPUTMUX0);
    INPUTMUX_AttachSignal(INPUTMUX0, (uint16_t)pstTrigConfig->eTrigSlot, eMuxConnection);

    return true;
}

static bool bConfig_ADCCommand(lpadc_conv_command_config_t *pstlpadc_CmdConfig, sT_ADC_CMDData_t *pstCmdData, sT_ADC_ModuleConfig_t *pstADCModuleConfig)
{
    if(!bMap_ADCChannel(pstADCModuleConfig->eADCModule, pstCmdData->eChannel, &pstlpadc_CmdConfig->channelNumber))
    {
        FHALT("Failed to map ADC channel for ADC module [%d].\n", pstADCModuleConfig->eADCModule);
        return false;
    }
    if(!bAssign_ADCResolution(pstCmdData->eResolution, &pstlpadc_CmdConfig->conversionResolutionMode))
    {
        FHALT("Failed to assign ADC resolution for ADC module [%d].\n", pstADCModuleConfig->eADCModule);
        return false;
    }        
    if(!bAssign_LoopBehavior(pstADCModuleConfig->eADCModule, pstlpadc_CmdConfig, pstCmdData))
    {
        FHALT("Invalid Configuration for Loop Count and behavior");
        return false;
    }
    pstlpadc_CmdConfig->enableWaitTrigger = pstCmdData->bIsNewTrig_Req_For_NextConv;

    if(!bAssign_HWAverageMode(pstlpadc_CmdConfig, pstCmdData))
    {
        FHALT("Invalid Configuration for HW Averaging");
        return false;                
    }

    pstlpadc_CmdConfig->hardwareCompareMode = kLPADC_HardwareCompareDisabled;
    pstlpadc_CmdConfig->hardwareCompareValueHigh = 0;
    pstlpadc_CmdConfig->hardwareCompareValueLow = 0;
    pstlpadc_CmdConfig->sampleChannelMode = kLPADC_SampleChannelSingleEndSideA;
    
    if(!bAssign_SampleTime(pstlpadc_CmdConfig, pstCmdData))
    {
        FHALT("Invalid Configuration for Sample Time");
        return false;                
    }
    return true;
}

static bool bAssign_SampleTime(lpadc_conv_command_config_t *pstlpadc_CmdConfig, sT_ADC_CMDData_t *pstCmdData)
{
    if(pstlpadc_CmdConfig == NULL || pstCmdData == NULL)
    {
        FHALT("Invalid Pointer Reference");
        return false;
    }
    
    if(pstCmdData->eSampleTime < 0 || pstCmdData->eSampleTime >= eNUMBER_OF_ADC_SAMPLE_TIMEs)
    {
        FHALT("Invalid Sample Time. Configuration : %d(Max: %d)", pstCmdData->eSampleTime, eNUMBER_OF_ADC_SAMPLE_TIMEs);
        return false;
    }

    switch(pstCmdData->eSampleTime)
    {
        case eADC_SampleTime_3_ADCKCycles://No averaging
            pstlpadc_CmdConfig->sampleTimeMode = kLPADC_SampleTimeADCK3;
            break;
        case eADC_SampleTime_5_ADCKCycles:
            pstlpadc_CmdConfig->sampleTimeMode = kLPADC_SampleTimeADCK5;
            break;
        case eADC_SampleTime_7_ADCKCycles:
            pstlpadc_CmdConfig->sampleTimeMode = kLPADC_SampleTimeADCK7;
            break;
        case eADC_SampleTime_11_ADCKCycles:
            pstlpadc_CmdConfig->sampleTimeMode = kLPADC_SampleTimeADCK11;
            break;
        case eADC_SampleTime_19_ADCKCycles:
            pstlpadc_CmdConfig->sampleTimeMode = kLPADC_SampleTimeADCK19;
            break;
        case eADC_SampleTime_35_ADCKCycles:
            pstlpadc_CmdConfig->sampleTimeMode = kLPADC_SampleTimeADCK35;
            break;
        case eADC_SampleTime_67_ADCKCycles:
            pstlpadc_CmdConfig->sampleTimeMode = kLPADC_SampleTimeADCK67;
            break;
        case eADC_SampleTime_131_ADCKCycles:
            pstlpadc_CmdConfig->sampleTimeMode = kLPADC_SampleTimeADCK131;
            break;
        default:
            FHALT("Invalid AVG Type");
            return false;
    }
    return true;    
}

static bool bAssign_HWAverageMode(lpadc_conv_command_config_t *pstlpadc_CmdConfig, sT_ADC_CMDData_t *pstCmdData)
{
    if(pstlpadc_CmdConfig == NULL || pstCmdData == NULL)
    {
        FHALT("Invalid Pointer Reference");
        return false;
    }
    
    if(pstCmdData->eHWAvgSampleCount < 0 || pstCmdData->eHWAvgSampleCount >= eNUMBER_OF_ADC_AVG_CONVCOUNTs)
    {
        FHALT("Invalid HW Avg. Configuration : %d(Max: %d)", pstCmdData->eHWAvgSampleCount, eNUMBER_OF_ADC_AVG_CONVCOUNTs);
        return false;
    }

    switch(pstCmdData->eHWAvgSampleCount)
    {
        case eADC_AVG_ConvCount_0://No averaging
            pstlpadc_CmdConfig->hardwareAverageMode = kLPADC_HardwareAverageCount1;
            break;
        case eADC_AVG_ConvCount_2:
            pstlpadc_CmdConfig->hardwareAverageMode = kLPADC_HardwareAverageCount2;
            break;
        case eADC_AVG_ConvCount_4:
            pstlpadc_CmdConfig->hardwareAverageMode = kLPADC_HardwareAverageCount4;
            break;
        case eADC_AVG_ConvCount_8:
            pstlpadc_CmdConfig->hardwareAverageMode = kLPADC_HardwareAverageCount8;
            break;
        case eADC_AVG_ConvCount_16:
            pstlpadc_CmdConfig->hardwareAverageMode = kLPADC_HardwareAverageCount16;
            break;
        case eADC_AVG_ConvCount_32:
            pstlpadc_CmdConfig->hardwareAverageMode = kLPADC_HardwareAverageCount32;
            break;
        case eADC_AVG_ConvCount_64:
            pstlpadc_CmdConfig->hardwareAverageMode = kLPADC_HardwareAverageCount64;
            break;
        case eADC_AVG_ConvCount_128:
            pstlpadc_CmdConfig->hardwareAverageMode = kLPADC_HardwareAverageCount128;
            break;
        case eADC_AVG_ConvCount_256:
            pstlpadc_CmdConfig->hardwareAverageMode = kLPADC_HardwareAverageCount256;
            break;
        case eADC_AVG_ConvCount_512:
            pstlpadc_CmdConfig->hardwareAverageMode = kLPADC_HardwareAverageCount512;
            break;
        case eADC_AVG_ConvCount_1024:
            pstlpadc_CmdConfig->hardwareAverageMode = kLPADC_HardwareAverageCount1024;
            break;
        default:
            FHALT("Invalid AVG Type");
            return false;
    }
    return true;
}

static bool bAssign_LoopBehavior(eADC_Module_t eADCModule, lpadc_conv_command_config_t *pstlpadc_CmdConfig, sT_ADC_CMDData_t *pstCmdData)
{
    if(pstlpadc_CmdConfig == NULL || pstCmdData == NULL)
    {
        FHALT("Invalid Pointer Reference");
        return false;
    }
    if(pstCmdData->uiLoopCount > ADC_MAX_LOOP_COUNT)
    {
        FHALT("Invalid LoopCount : %d (Max: %d)", pstCmdData->uiLoopCount, ADC_MAX_LOOP_COUNT);
        return false;        
    }

    if(!pstCmdData->bIsLoopWithChIncrementEnabled)
    {
        pstlpadc_CmdConfig->enableAutoChannelIncrement = false;
        pstlpadc_CmdConfig->loopCount = pstCmdData->uiLoopCount;//If uiLoopCount>0, then the ADC will run conversion on the defined channel
        return true;                                            //(uiLoopCount + 1) times
    }
    if(pstCmdData->uiLoopCount == 0)
    {
        FHALT("Invalid loop count (%d) with Auto Loop enabled. It cannot be zero", pstCmdData->uiLoopCount);
        return false;        
    }

    if((pstCmdData->eChannel + pstCmdData->uiLoopCount) >= eNUMBER_OF_ADC_CHANNELs)
    {
        FHALT("ADC will perform conversion beyond available ADC channels with this loop count (%d)[Ch: %d, Max: %d]",
                pstCmdData->uiLoopCount, pstCmdData->eChannel, eNUMBER_OF_ADC_CHANNELs);
        return false;
    }

    if(!bValidate_CMD_ChChainingWithLoop(eADCModule, pstCmdData->eChannel, pstCmdData->uiLoopCount))
    {
        FHALT("ADC[%d] : CH discontinuity detected with Ch[%d] with LoopCount(%d). A 'Reserved' or 'Disabled' Channel in the middle",
            eADCModule, pstCmdData->eChannel, pstCmdData->uiLoopCount);
        return false;
    }

    pstlpadc_CmdConfig->enableAutoChannelIncrement = true;
    pstlpadc_CmdConfig->loopCount = pstCmdData->uiLoopCount;
    return true;
}

static bool bAssign_ADCResolution(eADC_ResolutionType_t eResolution, lpadc_conversion_resolution_mode_t *peLPADCResolution)
{
    if(peLPADCResolution == NULL)
    {
        FHALT("LPADC Resolution pointer is NULL. Cannot assign ADC resolution.\n");
        return false;
    }

    switch(eResolution)
    {
        case eADC_Resolution_12Bit:
            *peLPADCResolution = kLPADC_ConversionResolutionStandard;
            break;
        case eADC_Resolution_16Bit:
            *peLPADCResolution = kLPADC_ConversionResolutionHigh;
            break;
        default:
            FHALT("Invalid ADC resolution specified. Cannot assign ADC resolution.\n");
            return false;
    }
    return true;
}

static bool bMap_ADCChannel(eADC_Module_t eModule, eADC_Channel_t eChannel, uint32_t *puiMappedChannel)
{
    // Implement the mapping logic based on your specific requirements
    // This is a placeholder implementation and should be replaced with actual mapping logic
    if(eChannel >= eNUMBER_OF_ADC_CHANNELs || eChannel < 0)
    {
        FHALT("Invalid ADC channel specified for mapping.\n");
        return false;
    }
    if(eModule >= eNUMBER_OF_ADC_MODULEs || eModule < 0)
    {
        FHALT("Invalid ADC module specified for mapping.\n");
        return false;
    }
    
    const sT_ADC_ChannelMap_t *pstChannelMap = pstGetADCChannelMapROnly(eModule, eChannel);
    if(pstChannelMap == NULL || !pstChannelMap->stInfo.bIsAvailable)
    {
        FHALT("ADC channel mapping not available for module [%d], channel [%d].\n", eModule, eChannel);
        return false;
    }
    
    *puiMappedChannel = (uint32_t)pstChannelMap->stInfo.uiLPADCChannelNumber; // Example mapping, replace with actual logic
    return true;
}

static bool bADC_Init(ADC_Type *pstADCBase, stADC_HWmodConfig_t *pstHWConfig, sT_ADC_ModuleConfig_t *pstADCModuleConfig)
{
    if(pstADCModuleConfig == NULL)
    {
        FHALT("ADC Module Config pointer is NULL. Cannot initialize ADC module.\n");
        return false;
    }
        
    lpadc_config_t *pstADCConfig = &pstHWConfig->stADCConfig;
    LPADC_GetDefaultConfig(&pstHWConfig->stADCConfig);

    switch(pstADCModuleConfig->eRefSrc)
    {
        case eADC_VrefSrc_VDD_ANA:
            pstADCConfig->referenceVoltageSource = kLPADC_ReferenceVoltageAlt1;
            break;
        case eADC_VrefSrc_Ext:
            pstADCConfig->referenceVoltageSource = kLPADC_ReferenceVoltageAlt2;
            break;
        case eADC_VrefSrc_Int:
            pstADCConfig->referenceVoltageSource = kLPADC_ReferenceVoltageAlt3;
            break;
        default:
            FHALT("Invalid ADC reference voltage source specified in configuration.\n");
            return false;
    }
    
    if(pstADCModuleConfig->uiWaterMarkLevel >= ADC_MAX_WATERMARK_LEVEL)
    {
        FHALT("Invalid ADC watermark level specified in configuration.\n");
        return false;
    }
    pstADCConfig->FIFOWatermark = pstADCModuleConfig->uiWaterMarkLevel;

    LPADC_Init(pstADCBase, pstADCConfig);
    LPADC_DoAutoCalibration(pstADCBase);    
    return true;

}

static void vValidate_ADCConfig(sT_ADC_ModuleConfig_t *pstADCModuleConfig)
{
    if(pstADCModuleConfig == NULL)
    {
        FHALT("ADC Module Config pointer is NULL. Cannot validate ADC module configuration.\n");
        return;
    }

    if(pstADCModuleConfig->eADCModule >= eNUMBER_OF_ADC_MODULEs || pstADCModuleConfig->eADCModule < eADC_ADC0)
    {
        FHALT("Invalid ADC module specified in configuration.\n");
        pstADCModuleConfig->bIsConfigOk = false;
        return;
    }

    if(!bValidate_TrigSourceCompCallbackFn(pstADCModuleConfig))
    {
        pstADCModuleConfig->bIsConfigOk = false;
        return;
    }
}

static bool bValidate_TrigSourceCompCallbackFn(sT_ADC_ModuleConfig_t *pstADCModuleConfig)
{
    bool bNotificationRequested = false;

    for(uint8_t i = eTrig_Slot_0; i < eNUMBER_OF_ADC_TRIG_SLOTs; i++)
    {
        sT_ADC_TrigConfig_t *pstSlot = &pstADCModuleConfig->staTrigConfig[i];

        if(!pstSlot->bIsTrigSlotEnabled &&
            pstSlot->pstTHeadCmdConfig != NULL)
        {
            FHALT("Disabled trigger slot[%d] contains command configurations", i);
            return false;
        }
        if(!pstSlot->bIsTrigSlotEnabled)
            continue;

        if(pstSlot->bEnTrigCompletionNotifyReq)
            bNotificationRequested = true;
    }

    if(bNotificationRequested && pstADCModuleConfig->pvTrigCompltCallbackFn == NULL)
    {
        FHALT("Trigger completion notification requested without callback");
        return false;
    }

    if(!bNotificationRequested && pstADCModuleConfig->pvTrigCompltCallbackFn != NULL)
    {
        FHALT("Trigger callback defined without completion notification");
        return false;
    }

    return true;
}

void vDeInit_ADC(eADC_Module_t eADCModule)
{
    if(eADCModule >= eNUMBER_OF_ADC_MODULEs)
    {
        FHALT("Invalid ADC module specified. Cannot de-initialize ADC module.\n");
        return;
    }

    stADC_HWmodConfig_t *pstADCConfig = pstGetADCModule(eADCModule);
    if(pstADCConfig == NULL)
    {
        FHALT("ADC Hardware Config pointer is NULL. Cannot de-initialize ADC module.\n");
        return;
    }

    ADC_Type *pstADCBase = pstADCConfig->pstADCBase;
    if(pstADCBase == NULL)
    {
        pstADCBase = pstGetADCBase(eADCModule);
    }

    if(pstADCBase != NULL)
    {
        LPADC_EnableFIFOWatermarkDMA(pstADCBase, false);
        LPADC_DisableInterrupts(pstADCBase,
                                (uint32_t)pstADCConfig->uiGlobalIntrMask);
        LPADC_ClearStatusFlags(pstADCBase, kLPADC_ResultFIFO0OverflowFlag);

        switch(eADCModule)
        {
            case eADC_ADC0:
                irq_disable(ADC0_IRQn);
                break;
            case eADC_ADC1:
                irq_disable(ADC1_IRQn);
                break;
            default:
                break;
        }
    }

    if(pstADCConfig->stTADCHWNotifyCtrl.eNotificationType == eNotification_DMA)
    {
        if(device_is_ready(pstaADCDMADev[eADCModule]))
        {
            (void)dma_stop(pstaADCDMADev[eADCModule], uiaADCDMAChannel[eADCModule]);
        }
    }

    if(pstADCBase != NULL)
    {
        LPADC_Deinit(pstADCBase);
    }

    vRelease_CommandMemoryBuffers(pstADCConfig->staTrigSlotCtrl);
    
    k_mutex_lock(&kADCStatisticsMutex, K_FOREVER);
    atomic_fetch_add_explicit(&uiaADCStatisticsGeneration[eADCModule], 1U, memory_order_release);
    vRelease_ADCChannelConfig(eADCModule);
    k_mutex_unlock(&kADCStatisticsMutex);

    memset(pstADCConfig, 0, sizeof(*pstADCConfig));
    vDeInit_ADC_Thread();
}

static void vRelease_CommandMemoryBuffers(sT_TrigSlotCtrl_t *pstaTrigSlotCtrl)
{
    if(pstaTrigSlotCtrl == NULL)
        return;

    for(uint8_t i = eTrig_Slot_0; i < eNUMBER_OF_ADC_TRIG_SLOTs; i++)
    {
        vRelease_CMDBuffers(&pstaTrigSlotCtrl[i].pstCMDHead);
    }
}

static ADC_Type *pstGetADCBase(eADC_Module_t eADCModule)
{
    switch(eADCModule)
    {
        case eADC_ADC0:
            return ADC0;
        case eADC_ADC1:
            return ADC1;
        default:
            FHALT("Invalid ADC module specified. Cannot get ADC base address.\n");
            return NULL;
    }
}

static inline stADC_HWmodConfig_t *pstGetADCModule(eADC_Module_t eADCModule)
{
    if(eADCModule >= eNUMBER_OF_ADC_MODULEs)
    {
        FHALT("Invalid ADC module specified. Cannot get ADC module configuration.\n");
        return NULL;
    }
    return &staADC_HWConfig[eADCModule];
}

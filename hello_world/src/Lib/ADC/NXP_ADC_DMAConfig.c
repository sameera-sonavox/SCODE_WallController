#include <stdatomic.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/dma.h>
#include <zephyr/sys/util.h>
#include "fsl_common.h"
#include "NXP_ADC_DMAConfig.h"
#include "NXP_ADC_API.h"
#include "NXP_ADC_ProjDef.h"
#include "../GenericMacro.h"

BUILD_ASSERT(ADC_DMA_BLOCK_COUNT > 0U, "ADC_DMA_BLOCK_COUNT must be greater than zero");
BUILD_ASSERT(ADC_DMA_BLOCK_COUNT <= CONFIG_DMA_TCD_QUEUE_SIZE,
             "ADC_DMA_BLOCK_COUNT must be less than or equal to CONFIG_DMA_TCD_QUEUE_SIZE");

_Atomic bool bIsDMAThreadInitialized = false;

typedef struct
{    
    struct dma_config stADCDMAConfig_t;
    struct dma_block_config staADCDMABlockConfig_t[ADC_DMA_BLOCK_COUNT];
    bool bIsDMAConfigured;
    _Atomic uint8_t uiDMAErrorCount;
    _Atomic bool bMsgQ_Full;
    _Atomic bool bIsDMAError;
    _Atomic bool bIsDMAHWError;
} sT_ADCDMACtrl;

typedef struct
{
    eADC_Module_t eModuleId;
    uint32_t *puiDMABuffer[ADC_DMA_BLOCK_COUNT];
    uint32_t uiTransferSize;
    uint32_t uiResultsForBurst;
    _Atomic uint8_t uiCompletedBufferIndex;
} sT_ADC_DMAContext_t;

typedef struct
{
    eADC_Module_t eModuleId;
    uint32_t uiResultCount;
    uint32_t uiaDMAData[ADC_MAX_WATERMARK_LEVEL];
} sT_DMAMessage_t;


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

sT_ADC_DMAContext_t staDMAContext[eNUMBER_OF_ADC_MODULEs] = {0};
sT_ADCDMACtrl staDMAControl[eNUMBER_OF_ADC_MODULEs] = {0};

K_THREAD_STACK_DEFINE(thread_DMA_ADC, ADC_DMA_THREAD_STACK_SIZE);
static struct k_thread kADC_DMA_Thread_t;
static k_tid_t kADC_DMA_ThreadId;

K_MSGQ_DEFINE(kMsgQ_DMAResult_Q, sizeof(sT_DMAMessage_t), ADC_DMA_MSG_QUEUE_SIZE, 4);

static void vADC_DMA_Callback(const struct device *dev, void *pUserData, uint32_t uiChannel, int iStatus);
static inline bool bDecode_DMA_ADCResult(uint32_t puiDMAData, lpadc_conv_result_t *pstConvResult);
static void vDeInit_DMAConfiguration(eADC_Module_t eADCModule);
static void vFree_MemoryBuffers(eADC_Module_t eADCModule, uint8_t uiIndex);
static void vClear_DMAErrorStates(eADC_Module_t eModule);

static inline bool bIs_DMAConfigured_ForADCModule(eADC_Module_t eADCModule);
static inline void vSet_NextDMABuffer(uint8_t uiIndex, sT_ADC_DMAContext_t *pstDMAContext);
static inline bool bIs_DMAError(eADC_Module_t eADCModule);
static inline bool bIs_DMAHWError(eADC_Module_t eADCModule);
static inline void vSet_DMA_Error(eADC_Module_t eADCModule, bool bIsHWError);
static inline void vClear_DMA_ErrorFlag(eADC_Module_t eADCModule);
static inline void vCheck_DMA_Error( void );
static inline void vUpdate_DMAError_State( eADC_Module_t eModule );

static inline void vSet_MsgQ_FullFlag(eADC_Module_t eADCModule);
static inline bool bIs_MsgQ_Full(eADC_Module_t eADCModule);
static inline void vClear_MsgQ_FullFLag(eADC_Module_t eADCModule);

static void vInit_DMA_Thread( void );
static void vDeInit_DMA_Thread( void );
static void vCompute_DMA_MsgQ_Handler( void *p1, void *p2, void *p3 );

static inline bool bIsDMA_Thread_Initialized( void );
static inline void vSet_DMAThreadInit_Flag( void );
static inline void vClear_DMAThreadInit_Flag( void );

bool bADC_API_DMAInit(eADC_Module_t eADCModule)
{
    ADC_Type *pstADCBase = pstGetHWADCModule(eADCModule);
    if(pstADCBase == NULL)
        return false;

    sT_ADCToDMA_HW_Map_t stTADC_DMA_HWConfig = stGetSWADCModule(eADCModule);
    if(!stTADC_DMA_HWConfig.bIsADCInitialized)
    {
        FHALT("ADC Module[%d] not initialized", eADCModule);
        return false;
    }

    if(!device_is_ready(pstaADCDMADev[eADCModule]))
    {
        FHALT("ADC DMA device is not ready");
        return false;
    }
    
    sT_ADC_DMAContext_t *pstDMAContext = &staDMAContext[eADCModule];
    sT_ADCDMACtrl *pstDMACtrl = &staDMAControl[eADCModule];
    if(pstDMACtrl->bIsDMAConfigured)
    {
        FHALT("DMA for ADC Module[%d] is already Configured. API will deinitialize the stack and re-configure.", eADCModule);
        vDeInit_DMAConfiguration(eADCModule);
    }
    pstDMAContext->eModuleId = eADCModule;

    struct dma_block_config *pstDMABlock = pstDMACtrl->staADCDMABlockConfig_t;
    struct dma_config *pstDMA = &pstDMACtrl->stADCDMAConfig_t;
    
    uint32_t uiResultsPerBurst = stTADC_DMA_HWConfig.uiWaterMarkLevel + 1U;
    uint32_t uiBurstLengthBytes = uiResultsPerBurst * sizeof(uint32_t);
    
    for(uint8_t i = 0; i < ADC_DMA_BLOCK_COUNT; i++)
    {
        memset(&pstDMABlock[i], 0, sizeof(struct dma_block_config));
        pstDMAContext->puiDMABuffer[i] = malloc(uiBurstLengthBytes);
        if(pstDMAContext->puiDMABuffer[i] == NULL)
        {
            vFree_MemoryBuffers(eADCModule, i);
            FHALT("Required memory could not be allocated to the DMA memory buffer for the ADC");
            return false;
        }

        pstDMABlock[i].source_address = (uint32_t)&pstADCBase->RESFIFO;
        pstDMABlock[i].dest_address = (uint32_t)pstDMAContext->puiDMABuffer[i];
        pstDMABlock[i].block_size = uiBurstLengthBytes;
        pstDMABlock[i].source_addr_adj = DMA_ADDR_ADJ_NO_CHANGE;
        pstDMABlock[i].dest_addr_adj = DMA_ADDR_ADJ_INCREMENT;
        pstDMABlock[i].dest_scatter_en = 1U;

        pstDMAContext->uiTransferSize = uiBurstLengthBytes;
        pstDMAContext->uiResultsForBurst = uiResultsPerBurst;
        if(i < (ADC_DMA_BLOCK_COUNT - 1))
            pstDMABlock[i].next_block = &pstDMABlock[i+1];
        else
            pstDMABlock[i].next_block = &pstDMABlock[0];
    }

    memset(pstDMA, 0, sizeof(struct dma_config));
    pstDMA->dma_slot = uiaADCDMASlot[eADCModule];
    pstDMA->channel_direction = PERIPHERAL_TO_MEMORY;
    pstDMA->source_data_size = sizeof(uint32_t);
    pstDMA->dest_data_size = sizeof(uint32_t);

    pstDMA->source_burst_length = uiResultsPerBurst;
    pstDMA->dest_burst_length = uiResultsPerBurst;
    pstDMA->head_block = &pstDMABlock[0];
    pstDMA->dma_callback = vADC_DMA_Callback;
    pstDMA->user_data = pstDMAContext;
    pstDMA->complete_callback_en = 1U;
    pstDMA->error_callback_dis = 0U;
    pstDMA->cyclic = 1U;
    pstDMA->block_count = ADC_DMA_BLOCK_COUNT;
    pstDMACtrl->bIsDMAConfigured = true;

    vRequest_ADC_To_DisableInterrupts(eADCModule);

    (void)dma_stop(pstaADCDMADev[eADCModule], uiaADCDMAChannel[eADCModule]);
    int iRet = dma_config(pstaADCDMADev[eADCModule], uiaADCDMAChannel[eADCModule], pstDMA);
    if(iRet != 0)
    {
        FHALT("ADC DMA configuration failed: %d", iRet);
        vDeInit_DMAConfiguration(eADCModule);
        return false;
    }

    iRet = dma_start(pstaADCDMADev[eADCModule], uiaADCDMAChannel[eADCModule]);
    if(iRet != 0)
    {
        FHALT("ADC DMA start failed: %d", iRet);
        vDeInit_DMAConfiguration(eADCModule);
        return false;
    }

    vClear_DMAErrorStates(eADCModule);
    vInit_DMA_Thread();
    LPADC_EnableFIFOWatermarkDMA(pstADCBase, true);
    printf("DMA Configured\n\r");
    return true;
}

static void vClear_DMAErrorStates(eADC_Module_t eModule)
{
    atomic_store_explicit(&staDMAControl[eModule].uiDMAErrorCount, 0U, memory_order_release);
    vClear_DMA_ErrorFlag(eModule);
    vClear_MsgQ_FullFLag(eModule);
}

static void vCompute_DMA_MsgQ_Handler( void *p1, void *p2, void *p3 )
{
    sT_DMAMessage_t stTDMAMsg = {0};

    while(1)
    { 
        if(k_msgq_get(&kMsgQ_DMAResult_Q, &stTDMAMsg, K_MSEC(10)) != 0)
        {
            vCheck_DMA_Error();
            continue;
        }        
        if(bIs_MsgQ_Full(stTDMAMsg.eModuleId))
        {
            vClear_MsgQ_FullFLag(stTDMAMsg.eModuleId);
        }

        if(stTDMAMsg.eModuleId >= eNUMBER_OF_ADC_MODULEs)
        {
            continue;
        }

        for(uint32_t i = 0U; i < stTDMAMsg.uiResultCount; i++)
        {
            lpadc_conv_result_t stResult = {0};
            if(!bDecode_DMA_ADCResult(stTDMAMsg.uiaDMAData[i], &stResult))
            {
                vSet_DMA_Error(stTDMAMsg.eModuleId, false);
                break;
            }

            vUpdate_ADCResult_FromDMA(stTDMAMsg.eModuleId, stResult);
        }
    }
}

static inline void vCheck_DMA_Error( void )
{
    for(uint8_t i = 0; i < eNUMBER_OF_ADC_MODULEs; i++)
    {
        if(!bIs_DMAError(i))
            continue;
        vUpdate_DMAError_State(i);
    }
}

static inline void vUpdate_DMAError_State( eADC_Module_t eModule )
{
    if(bIs_DMAHWError(eModule))
    {
        vClear_DMA_ErrorFlag(eModule);
        vNotify_ADC_DMAError(eModule);
        vDeInit_DMAConfiguration(eModule);
        return;
    }

    uint8_t uicount = atomic_fetch_add_explicit(&staDMAControl[eModule].uiDMAErrorCount, 1U, memory_order_acq_rel) + 1U;
    vClear_DMA_ErrorFlag(eModule);

    if(uicount < ADC_DMA_MAX_ERROR_COUNT)
        return;

    vNotify_ADC_DMAError(eModule);
    vDeInit_DMAConfiguration(eModule);
}


static void vInit_DMA_Thread( void )
{
    if(bIsDMA_Thread_Initialized())
        return;

    k_msgq_purge(&kMsgQ_DMAResult_Q);
    kADC_DMA_ThreadId = k_thread_create(&kADC_DMA_Thread_t, thread_DMA_ADC,
                                    K_THREAD_STACK_SIZEOF(thread_DMA_ADC), 
                                    vCompute_DMA_MsgQ_Handler,
                                    NULL, NULL, NULL, ADC_DMA_THREAD_PRIORITY, 0, K_NO_WAIT);
    vSet_DMAThreadInit_Flag();    
}

static void vDeInit_DMA_Thread( void )
{
    if(!bIsDMA_Thread_Initialized())
        return;

    uint8_t uiCount = 0;
    for(uint8_t i = 0; i < eNUMBER_OF_ADC_MODULEs; i++)
    {
        if(bIs_DMAConfigured_ForADCModule(i))
            uiCount++;
    }
    if(uiCount != 0)
        return;

    k_thread_abort(kADC_DMA_ThreadId);
    k_msgq_purge(&kMsgQ_DMAResult_Q);
    vClear_DMAThreadInit_Flag();
}

static inline bool bIsDMA_Thread_Initialized( void )
{
    bool bisInit = atomic_load_explicit(&bIsDMAThreadInitialized, memory_order_acquire);
    return bisInit;
}

static inline void vClear_DMAThreadInit_Flag( void )
{
    if(!bIsDMA_Thread_Initialized())
        return;
    atomic_store_explicit(&bIsDMAThreadInitialized, false, memory_order_release);
}

static inline void vSet_DMAThreadInit_Flag( void )
{
    uint8_t i = 0;
    for(i = 0; i < eNUMBER_OF_ADC_MODULEs; i++)
    {
        if(staDMAControl[i].bIsDMAConfigured)
            break;
    }
    if(i == eNUMBER_OF_ADC_MODULEs)
    {
        vDeInit_DMA_Thread();
        return;
    }
    atomic_store_explicit(&bIsDMAThreadInitialized, true, memory_order_release);
}

static void vADC_DMA_Callback(const struct device *dev, void *pUserData, uint32_t uiChannel, int iStatus)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(uiChannel);

    if(pUserData == NULL)
        return;

    sT_ADC_DMAContext_t *pstDMAContext = (sT_ADC_DMAContext_t *)pUserData;
    eADC_Module_t eModuleId = pstDMAContext->eModuleId;
    if(iStatus < 0)
    {
        vSet_DMA_Error(eModuleId, true);
        return;
    }

    uint8_t uiIndex = atomic_load_explicit(&pstDMAContext->uiCompletedBufferIndex, memory_order_acquire);
    if(bIs_MsgQ_Full(eModuleId))
    {
        vSet_NextDMABuffer(uiIndex, pstDMAContext);
        return;
    }

    sT_DMAMessage_t stTMsg = {
        .eModuleId = eModuleId,
        .uiResultCount = pstDMAContext->uiResultsForBurst
    };

    uint32_t *puiBuffer = pstDMAContext->puiDMABuffer[uiIndex];
    if(puiBuffer == NULL)
    {
        vSet_DMA_Error(eModuleId, false);
        vSet_NextDMABuffer(uiIndex, pstDMAContext);
        return;
    }

    for(uint32_t i = 0U; i < pstDMAContext->uiResultsForBurst; i++)
    {
        stTMsg.uiaDMAData[i] = puiBuffer[i];
    }

    if(k_msgq_put(&kMsgQ_DMAResult_Q, &stTMsg, K_NO_WAIT) != 0)
    {
        vSet_MsgQ_FullFlag(pstDMAContext->eModuleId);
        vSet_NextDMABuffer(uiIndex, pstDMAContext);
        return;
    }

    atomic_store_explicit(&staDMAControl[eModuleId].uiDMAErrorCount, 0U, memory_order_release);
    vSet_NextDMABuffer(uiIndex, pstDMAContext);
}

static inline bool bIs_DMAError(eADC_Module_t eADCModule)
{
    bool bisError = atomic_load_explicit(&staDMAControl[eADCModule].bIsDMAError, memory_order_acquire);
    return bisError;
}

static inline bool bIs_DMAHWError(eADC_Module_t eADCModule)
{
    bool bIsHWError = atomic_load_explicit(&staDMAControl[eADCModule].bIsDMAHWError, memory_order_acquire);
    return bIsHWError;
}

static inline void vSet_DMA_Error(eADC_Module_t eADCModule, bool bIsHWError)
{
    if(!bIs_DMAConfigured_ForADCModule(eADCModule))
        return;
    atomic_store_explicit(&staDMAControl[eADCModule].bIsDMAHWError, bIsHWError, memory_order_release);
    atomic_store_explicit(&staDMAControl[eADCModule].bIsDMAError, true, memory_order_release);
}

static inline void vClear_DMA_ErrorFlag(eADC_Module_t eADCModule)
{
    if(!bIs_DMAConfigured_ForADCModule(eADCModule))
        return;
    atomic_store_explicit(&staDMAControl[eADCModule].bIsDMAHWError, false, memory_order_release);
    atomic_store_explicit(&staDMAControl[eADCModule].bIsDMAError, false, memory_order_release);    
}

static inline void vSet_NextDMABuffer(uint8_t uiIndex, sT_ADC_DMAContext_t *pstDMAContext)
{
    uint8_t uiNext = (uiIndex + 1) % ADC_DMA_BLOCK_COUNT;
    atomic_store_explicit(&pstDMAContext->uiCompletedBufferIndex, uiNext, memory_order_release);
}

static inline void vSet_MsgQ_FullFlag(eADC_Module_t eADCModule)
{
    if(eADCModule >= eNUMBER_OF_ADC_MODULEs)
        return;
    if(!bIs_DMAConfigured_ForADCModule(eADCModule))
        return;
    atomic_store_explicit(&staDMAControl[eADCModule].bMsgQ_Full, true, memory_order_release);
}

static inline void vClear_MsgQ_FullFLag(eADC_Module_t eADCModule)
{
    if(eADCModule >= eNUMBER_OF_ADC_MODULEs)
        return;
    if(!bIs_DMAConfigured_ForADCModule(eADCModule))
        return;
    atomic_store_explicit(&staDMAControl[eADCModule].bMsgQ_Full, false, memory_order_release);
}

static inline bool bIs_MsgQ_Full(eADC_Module_t eADCModule)
{
    if(eADCModule >= eNUMBER_OF_ADC_MODULEs)
        return false;
    if(!bIs_DMAConfigured_ForADCModule(eADCModule))
        return false;
    
    bool bisfull = atomic_load_explicit(&staDMAControl[eADCModule].bMsgQ_Full, memory_order_acquire);
    return bisfull;
}

static inline bool bIs_DMAConfigured_ForADCModule(eADC_Module_t eADCModule)
{
    if(eADCModule >= eNUMBER_OF_ADC_MODULEs)
        return false;
    return staDMAControl[eADCModule].bIsDMAConfigured;    
}

static void vFree_MemoryBuffers(eADC_Module_t eADCModule, uint8_t uiIndex)
{
    if(uiIndex == 0)
        return;

    sT_ADC_DMAContext_t *pstDMAContext = &staDMAContext[eADCModule];
    for(int8_t i = uiIndex - 1; i >= 0; i--)
    {
        free(pstDMAContext->puiDMABuffer[i]);
        pstDMAContext->puiDMABuffer[i] = NULL;
    }
}

static void vDeInit_DMAConfiguration(eADC_Module_t eADCModule)
{
    if(eADCModule >= eNUMBER_OF_ADC_MODULEs)
    {
        FHALT("Invalid ADC Module: %d", eADCModule);
        return;
    }

    ADC_Type *pstADCBase = pstGetHWADCModule(eADCModule);
    if(pstADCBase == NULL)
    {
        FHALT("Null Pointer reference for HW ADC module at Id : %d", eADCModule);
        return;
    }

    LPADC_EnableFIFOWatermarkDMA(pstADCBase, false);
    (void)dma_stop(pstaADCDMADev[eADCModule], uiaADCDMAChannel[eADCModule]);    

    sT_ADC_DMAContext_t *pstDMAContext = &staDMAContext[eADCModule];
    sT_ADCDMACtrl *pstDMACtrl = &staDMAControl[eADCModule];
    if(!pstDMACtrl->bIsDMAConfigured)
    {
        FHALT("DMA is not configured for this module : %d", eADCModule);
        return;
    }

    memset(&pstDMACtrl->staADCDMABlockConfig_t, 0, sizeof(struct dma_block_config) * ADC_DMA_BLOCK_COUNT);
    memset(&pstDMACtrl->stADCDMAConfig_t, 0, sizeof(struct dma_config));

    for(uint8_t i = 0; i < ADC_DMA_BLOCK_COUNT; i++)
    {
        free(pstDMAContext->puiDMABuffer[i]);
        pstDMAContext->puiDMABuffer[i] = NULL;

        pstDMAContext->uiCompletedBufferIndex = 0;
        pstDMAContext->uiResultsForBurst = 0;
        pstDMAContext->uiTransferSize = 0;
        pstDMAContext->eModuleId = eNUMBER_OF_ADC_MODULEs;
    }
    pstDMACtrl->bIsDMAConfigured = false;
    vDeInit_DMA_Thread();
}

static inline bool bDecode_DMA_ADCResult(uint32_t puiDMAData, lpadc_conv_result_t *pstConvResult)
{
    if(pstConvResult == NULL)
        return false;
    if((puiDMAData & ADC_RESFIFO_VALID_MASK) != ADC_RESFIFO_VALID_MASK)
        return false;
    
    pstConvResult->commandIdSource = (puiDMAData & ADC_RESFIFO_CMDSRC_MASK) >> ADC_RESFIFO_CMDSRC_SHIFT;
    pstConvResult->loopCountIndex = (puiDMAData & ADC_RESFIFO_LOOPCNT_MASK) >> ADC_RESFIFO_LOOPCNT_SHIFT;
    pstConvResult->triggerIdSource = (puiDMAData & ADC_RESFIFO_TSRC_MASK) >> ADC_RESFIFO_TSRC_SHIFT;
    pstConvResult->convValue = (uint16_t)(puiDMAData & ADC_RESFIFO_D_MASK);
    return true;
}


bool bRequest_To_StopDMA(eADC_Module_t eADCModule)
{
    if(device_is_ready(pstaADCDMADev[eADCModule]))
    {
        (void)dma_stop(pstaADCDMADev[eADCModule], uiaADCDMAChannel[eADCModule]);
        vDeInit_DMAConfiguration(eADCModule);
        return true;
    }
    else
        return false;
}

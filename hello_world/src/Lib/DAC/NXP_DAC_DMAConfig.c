#include "NXP_DAC_DMAConfig.h"

#include <stdatomic.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/dma.h>

#include "fsl_dac.h"

#include "../GenericMacro.h"
#include "DAC_ProjDef.h"
#include "NXP_DAC_API.h"

#if defined USE_DMA

/*
 * Noise TCD buffer ownership:
 * Free    : not used by DMA, CPU may claim for refill/update.
 * Filling : owned by CPU worker/update path.
 * Ready   : complete buffer, available for DMA queueing.
 * Queued  : submitted to DMA, CPU must not modify.
 * Active  : initial/running DMA buffer ownership, CPU must not modify.
 *
 * Only Free/Ready buffers may be claimed by CPU-side update/refill logic.
 * Only Ready buffers may be claimed for DMA queueing.
 * Stop/disable must stop DMA and flush the refill worker before clearing states.
 */

#define DAC_DMA_QUEUE_DEPTH             DMA_TCD_RING_BUFF_COUNT

typedef struct
{
    volatile eT_TCDBuff_t aeBuffId[DMA_TCD_RING_BUFF_COUNT];
    volatile uint8_t uiWr;
    volatile uint8_t uiRd;
    volatile uint8_t uiCount;
} sT_DMATCDBuffQueue_t;

static const struct device *const pstDACDMADev = DEVICE_DT_GET(DAC_DMA_CTLR_NODE);

typedef struct{
    LPDAC_Type *DACModule;
    eDAC_WaveFormType_t eWaveType;
    eT_TCDBuff_t eTCDQueueBuffId;
    struct dma_config stDACDMAConfig_t;
    struct dma_block_config stDACDMABlockConfig_t;
    DACError_Callback_t pVErrorCallbackFn;
    DACParam_UpdateComplete_Callback_t pvParamUpdateCallBackFn;
    _Atomic bool bIsDMAError;
    _Atomic bool bIsUpdateRequested;
    const uint32_t *puiDMABuffer;
    uint32_t uiDMALen;
    const uint32_t *puiDMAUpdateBuffer;
    uint32_t uiUpdateLen;
} sT_DACDMAConfig_t;

typedef struct{
    _Atomic bool bIsDMAUsed;
    struct k_work_delayable stDMAMonitor_Worker_t;
} sT_DMAControl_t;

sT_DACDMAConfig_t stTDACDMA_Ctrl = {
    .DACModule = NULL,
    .bIsDMAError = false,
    .bIsUpdateRequested = false,
};

sT_DMAControl_t stTDMACtrl = {
    .bIsDMAUsed = false,
};

static sT_DMATCDBuffQueue_t stTDMAQueuedBuffCtrl;

#define stDMABlockConfig          (stTDACDMA_Ctrl.stDACDMABlockConfig_t)
#define stDMAModuleConfig         (stTDACDMA_Ctrl.stDACDMAConfig_t)
#define stDMADACModule            (stTDACDMA_Ctrl.DACModule)


static void vInit_DMAError_Monitor( void );
static void vCancel_DMAError_Monitor( void );
static void vDAC_DMAMonitor(struct k_work *work);
static void vDAC_DMA_Callback(const struct device *dev, void *pUserData, uint32_t uiChannel, int iStatus);

static inline void vSet_DMA_FlagForUse( void );
static inline void vClear_DMA_FlagForUse( void );
static inline bool bIsDMAActive( void );
static inline void vSet_DMAError(eDAC_Error eError);
static inline bool bIsDMAUpdateRequested( void );
static inline void vClear_DMAUpdateRequest( void );
static inline void vSet_DMAUpdateRequest( void );
static bool bSwitch_DAC_DMA_Buffer( void );
static bool bQueue_DAC_DMA_Buffer(const uint32_t *puiDMABuffer, uint16_t uiLen, eT_TCDBuff_t eBuffId);
static bool IsAutomaticTCDFillRequired( void );
static bool bPush_QueuedTCDBufferId(eT_TCDBuff_t eBuffId);
static bool bPop_Completed_DMA_Buffer(eT_TCDBuff_t *peBuffId);

bool bValidate_DMAConfigurations(const uint32_t *puiDMABuffer, uint16_t uiLen);

bool bSetup_DAC_DMA_Circular( const uint32_t *puiDMABuffer,
                              uint16_t uiLen,
                              eDAC_WaveFormType_t eWaveType,
                              eT_TCDBuff_t eTCDBuffId, 
                              uintptr_t ptrDAC,
                              DACError_Callback_t pvErrorCallback,
                              DACParam_UpdateComplete_Callback_t pvUpdateCallback )
{
    int iRet;

    if(!bValidate_DMAConfigurations(puiDMABuffer, uiLen))
        return false;
    if(ptrDAC == 0U)
    {
        FHALT("Invalid DAC base address for DMA setup");
        return false;
    }

    stDMADACModule = ((LPDAC_Type *)ptrDAC);
    stTDACDMA_Ctrl.puiDMABuffer = puiDMABuffer;
    stTDACDMA_Ctrl.uiDMALen = uiLen;
    stTDACDMA_Ctrl.eWaveType = eWaveType;
    stTDACDMA_Ctrl.eTCDQueueBuffId = eTCDBuffId;
    stTDACDMA_Ctrl.pVErrorCallbackFn = pvErrorCallback;
    stTDACDMA_Ctrl.pvParamUpdateCallBackFn = pvUpdateCallback;
    vReset_TCDBuffer_Management();

    vClear_DMAUpdateRequest();

    (void)dma_stop(pstDACDMADev, DAC_DMA_CHANNEL);

    memset(&stDMABlockConfig, 0, sizeof(stDMABlockConfig));
    stDMABlockConfig.source_address = (uint32_t)puiDMABuffer;
    stDMABlockConfig.dest_address = (uint32_t)&stDMADACModule->DATA;
    stDMABlockConfig.block_size = (uint32_t)uiLen * DAC_DMA_WORD_BYTES;
    stDMABlockConfig.source_addr_adj = DMA_ADDR_ADJ_INCREMENT;
    stDMABlockConfig.dest_addr_adj = DMA_ADDR_ADJ_NO_CHANGE;
    stDMABlockConfig.source_gather_en = 1U;
    stDMABlockConfig.next_block = NULL;

    memset(&stDMAModuleConfig, 0, sizeof(stDMAModuleConfig));
    stDMAModuleConfig.dma_slot = DAC_DMA_SLOT_DAC0;
    stDMAModuleConfig.channel_direction = MEMORY_TO_PERIPHERAL;
    stDMAModuleConfig.source_data_size = DAC_DMA_WORD_BYTES;
    stDMAModuleConfig.dest_data_size = DAC_DMA_WORD_BYTES;
    stDMAModuleConfig.source_burst_length = DAC_DMA_WORD_BYTES;
    stDMAModuleConfig.dest_burst_length = DAC_DMA_WORD_BYTES;
    stDMAModuleConfig.block_count = 1U;
    stDMAModuleConfig.head_block = &stDMABlockConfig;
    stDMAModuleConfig.dma_callback = vDAC_DMA_Callback;
    stDMAModuleConfig.user_data = NULL;
    stDMAModuleConfig.complete_callback_en = 1U;
    stDMAModuleConfig.error_callback_dis = 0U;
    stDMAModuleConfig.cyclic = 1U;

    iRet = dma_config(pstDACDMADev, DAC_DMA_CHANNEL, &stDMAModuleConfig);
    if(iRet != 0)
    {
        FHALT("Zephyr DMA configuration failed: %d", iRet);
        stDMADACModule = NULL;
        return false;
    }

    if(IsAutomaticTCDFillRequired())
    {
        if(eTCDBuffId >= DMA_TCD_RING_BUFF_COUNT)
        {
            FHALT("Invalid initial TCD buffer ID.");
            stDMADACModule = NULL;
            return false;
        }

        vMark_TCDBuffer_Queued(eTCDBuffId);

        if(!bPush_QueuedTCDBufferId(eTCDBuffId))
        {
            FHALT("Failed to push initial queued TCD buffer ID.");
            stDMADACModule = NULL;
            return false;
        }

    }

    DAC_EnableDMA(stDMADACModule, kDAC_FIFOWatermarkDMAEnable, true);

    iRet = dma_start(pstDACDMADev, DAC_DMA_CHANNEL);
    if(iRet != 0)
    {
        DAC_EnableDMA(stDMADACModule, kDAC_FIFOWatermarkDMAEnable, false);
        FHALT("Zephyr DMA start failed: %d", iRet);
        stDMADACModule = NULL;
        return false;
    }

    vInit_DMAError_Monitor();
    return true;
}

bool bRequest_DMA_BufferSwap(const uint32_t *puiDMABuffer, uint16_t uiLen, uint8_t uiBuffIndex)
{
    ARG_UNUSED(uiBuffIndex);

    if(!bValidate_DMAConfigurations(puiDMABuffer, uiLen))
    {
        FHALT("DMA Buffer Validation Failed");
        return false;
    }
    if(stDMADACModule == NULL)
    {
        FHALT("DAC DMA module is not initialized.");
        return false;
    }

    stTDACDMA_Ctrl.puiDMAUpdateBuffer = puiDMABuffer;
    stTDACDMA_Ctrl.uiUpdateLen = uiLen;
    vSet_DMAUpdateRequest();

    if(!bSwitch_DAC_DMA_Buffer())
    {
        vClear_DMAUpdateRequest();
        return false;
    }

    return true;
}

static bool IsAutomaticTCDFillRequired( void )
{
    switch(stTDACDMA_Ctrl.eWaveType)
    {
        case eDAC_WaveForm_Sawtooth:
        case eDAC_WaveForm_Sine:
        case eDAC_WaveForm_Triangle:
            return false;
        case eDAC_WaveForm_PinkNoise:
        case eDAC_WaveForm_WhiteNoise:
            return true;
        default:
            FHALT("Not Supported Waveform Type");
            return false;
    }
}

static void vDAC_DMA_Callback(const struct device *dev, void *pUserData, uint32_t uiChannel, int iStatus)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(pUserData);
    ARG_UNUSED(uiChannel);

    eT_TCDBuff_t eCompletedBuffId, eNextBuffId;

    if(iStatus < 0)
    {
        vSet_DMAError(eDACErr_DMA_Error);
        return;
    }

    if(IsAutomaticTCDFillRequired())
    {
        if(bPop_Completed_DMA_Buffer(&eCompletedBuffId))
        {
            vNotify_DMANoiseBuffer_Completed(eCompletedBuffId);
        }

        eNextBuffId = eGet_Ready_TCDBufferId();
        if(eNextBuffId < DMA_TCD_RING_BUFF_COUNT)
        {
            if(!bTry_Claim_TCDBuffer_ForDMA(eNextBuffId))
            {
                return;
            }

            uint32_t *puiNextBuffer = puiGet_TCDBuffer(eNextBuffId);
            if(puiNextBuffer != NULL)
            {
                (void)bQueue_DAC_DMA_Buffer(puiNextBuffer,
                                            (uint16_t)stTDACDMA_Ctrl.uiDMALen,
                                            eNextBuffId);
            }
        }
        return;
    }
    
    if(bIsDMAUpdateRequested())
    {
        stTDACDMA_Ctrl.puiDMABuffer = stTDACDMA_Ctrl.puiDMAUpdateBuffer;
        stTDACDMA_Ctrl.uiDMALen = stTDACDMA_Ctrl.uiUpdateLen;
        vClear_DMAUpdateRequest();

        if(stTDACDMA_Ctrl.pvParamUpdateCallBackFn != NULL)
        {
            stTDACDMA_Ctrl.pvParamUpdateCallBackFn(true, NULL);
        }
    }

    if(stDMADACModule != NULL && stTDACDMA_Ctrl.puiDMABuffer != NULL && stTDACDMA_Ctrl.uiDMALen != 0U)
    {
        (void)bQueue_DAC_DMA_Buffer(stTDACDMA_Ctrl.puiDMABuffer, (uint16_t)stTDACDMA_Ctrl.uiDMALen, DMA_TCD_RING_BUFF_COUNT);
    }
}

void vReset_TCDBuffer_Management( void )
{
    memset(stTDMAQueuedBuffCtrl.aeBuffId, eNUMBER_OF_BUFFERs, sizeof(stTDMAQueuedBuffCtrl.aeBuffId));
    stTDMAQueuedBuffCtrl.uiCount = 0;
    stTDMAQueuedBuffCtrl.uiRd = 0;
    stTDMAQueuedBuffCtrl.uiWr = 0;
}

static bool bPush_QueuedTCDBufferId(eT_TCDBuff_t eBuffId)
{
    if(eBuffId >= DMA_TCD_RING_BUFF_COUNT)
    {
        return false;
    }

    if(stTDMAQueuedBuffCtrl.uiCount >= DAC_DMA_QUEUE_DEPTH)
    {
        return false;
    }

    stTDMAQueuedBuffCtrl.aeBuffId[stTDMAQueuedBuffCtrl.uiWr] = eBuffId;
    stTDMAQueuedBuffCtrl.uiWr = (stTDMAQueuedBuffCtrl.uiWr + 1U) % DAC_DMA_QUEUE_DEPTH;
    stTDMAQueuedBuffCtrl.uiCount++;

    return true;
}

static bool bPop_Completed_DMA_Buffer(eT_TCDBuff_t *peBuffId)
{
    if((peBuffId == NULL) || (stTDMAQueuedBuffCtrl.uiCount == 0U))
    {
        return false;
    }

    *peBuffId = stTDMAQueuedBuffCtrl.aeBuffId[stTDMAQueuedBuffCtrl.uiRd];
    stTDMAQueuedBuffCtrl.uiRd = (stTDMAQueuedBuffCtrl.uiRd + 1U) % DAC_DMA_QUEUE_DEPTH;
    stTDMAQueuedBuffCtrl.uiCount--;

    return true;
}

static inline bool bIsDMAUpdateRequested( void )
{
    bool bisrequested = atomic_load_explicit(&stTDACDMA_Ctrl.bIsUpdateRequested, memory_order_acquire);
    return bisrequested;
}

static inline void vClear_DMAUpdateRequest( void )
{
    atomic_store_explicit(&stTDACDMA_Ctrl.bIsUpdateRequested, false, memory_order_release);
}

static inline void vSet_DMAUpdateRequest( void )
{
    atomic_store_explicit(&stTDACDMA_Ctrl.bIsUpdateRequested, true, memory_order_release);
}

static bool bSwitch_DAC_DMA_Buffer( void )
{
    if(!bIsDMAUpdateRequested())
    {
        FHALT("DAC DMA buffer switch requested without pending update.");
        return false;
    }

    if(stDMADACModule == NULL || stTDACDMA_Ctrl.puiDMAUpdateBuffer == NULL || stTDACDMA_Ctrl.uiUpdateLen == 0U)
    {
        FHALT("Invalid DAC DMA buffer switch state.");
        return false;
    }

    /*
     * Zephyr MCUX eDMA cyclic mode owns the TCD queue. A new TCD slot is safe
     * only after the DMA callback runs, so the request path only marks the
     * pending buffer. The callback queues it at the next block boundary.
     */
    return true;
}

static bool bQueue_DAC_DMA_Buffer(const uint32_t *puiDMABuffer, uint16_t uiLen, eT_TCDBuff_t eBuffId)
{
    int iRet;
    uint32_t uiTransferBytes = (uint32_t)uiLen * DAC_DMA_WORD_BYTES;

    iRet = dma_reload(pstDACDMADev,
                      DAC_DMA_CHANNEL,
                      (uint32_t)puiDMABuffer,
                      (uint32_t)&stDMADACModule->DATA,
                      uiTransferBytes);
    if(iRet != 0)
    {
        FHALT("Zephyr DMA reload failed: %d", iRet);
        return false;
    }

    if(IsAutomaticTCDFillRequired())
    {
        if(!bPush_QueuedTCDBufferId(eBuffId))
        {
            FHALT("Failed to push queued TCD buffer ID.");
            return false;
        }

    }

    return true;
}

void vDisable_DAC_DMA_Circular( void )
{
    if(stDMADACModule == NULL)
    {
        return;
    }

    vCancel_DMAError_Monitor();
    DAC_EnableDMA(stDMADACModule, kDAC_FIFOWatermarkDMAEnable, false);
    (void)dma_stop(pstDACDMADev, DAC_DMA_CHANNEL);
    stDMADACModule = NULL;
}

void vStop_DAC_To_DMA_Request( void )
{
    if(stDMADACModule == NULL)
    {
        FHALT("DAC is not enabled to stop it!!!");
        return;
    }

    vCancel_DMAError_Monitor();
    DAC_EnableDMA(stDMADACModule, kDAC_FIFOWatermarkDMAEnable, false);
    (void)dma_stop(pstDACDMADev, DAC_DMA_CHANNEL);
}

void vSet_DMA_ReStart_Request( void )
{
    int iRet;

    if(stDMADACModule == NULL)
    {
        FHALT("DAC DMA module is not initialized.");
        return;
    }

    DAC_ClearStatusFlags(stDMADACModule,
                         kDAC_FIFOOverflowFlag | kDAC_FIFOUnderflowFlag);

    DAC_EnableDMA(stDMADACModule, kDAC_FIFOWatermarkDMAEnable, true);
    iRet = dma_start(pstDACDMADev, DAC_DMA_CHANNEL);
    if(iRet != 0)
    {
        FHALT("Zephyr DMA restart failed: %d", iRet);
    }
}

static inline void vSet_DMA_FlagForUse( void )
{
    atomic_store_explicit(&stTDMACtrl.bIsDMAUsed, true, memory_order_release);
}
static inline void vClear_DMA_FlagForUse( void )
{
    atomic_store_explicit(&stTDMACtrl.bIsDMAUsed, false, memory_order_release);
}
static inline bool bIsDMAActive( void )
{
    bool bIsactive = atomic_load_explicit(&stTDMACtrl.bIsDMAUsed, memory_order_acquire);
    return bIsactive;
}

static void vInit_DMAError_Monitor( void )
{
    if(bIsDMAActive())
    {
        k_work_schedule(&stTDMACtrl.stDMAMonitor_Worker_t, K_MSEC(10));
        return;
    }

    vSet_DMA_FlagForUse();
    stTDACDMA_Ctrl.bIsDMAError = false;
    k_work_init_delayable(&stTDMACtrl.stDMAMonitor_Worker_t, vDAC_DMAMonitor);
    k_work_schedule(&stTDMACtrl.stDMAMonitor_Worker_t, K_MSEC(10));
}

static void vCancel_DMAError_Monitor( void )
{
    vClear_DMA_FlagForUse();
    k_work_cancel_delayable(&stTDMACtrl.stDMAMonitor_Worker_t);
}

void vDAC_DMAMonitor(struct k_work *work)
{
    ARG_UNUSED(work);

    if(!bIsDMAActive())
    {
        return;
    }

    sT_DAC_DMA_Flags stTFlags = stGet_DAC_DMA_Status();
    uint32_t uiDACFlags = stTFlags.uiDACFlags;

    if((uiDACFlags & kDAC_FIFOUnderflowFlag) != 0U && stTDACDMA_Ctrl.pVErrorCallbackFn != NULL)
    {
        vSet_DMAError(eDACErr_FIFO_UnderFlow);
    }

    if((uiDACFlags & kDAC_FIFOOverflowFlag) != 0U && stTDACDMA_Ctrl.pVErrorCallbackFn != NULL)
    {
        vSet_DMAError(eDACErr_FIFO_OverFlow);
    }

    k_work_schedule(&stTDMACtrl.stDMAMonitor_Worker_t, K_MSEC(10));
}

static inline void vSet_DMAError(eDAC_Error eError)
{
    if(stTDACDMA_Ctrl.pVErrorCallbackFn != NULL)
    {
        stTDACDMA_Ctrl.pVErrorCallbackFn(eError, NULL);
    }
    atomic_store_explicit(&stTDACDMA_Ctrl.bIsDMAError, true, memory_order_release);
}

bool bValidate_DMAConfigurations(const uint32_t *puiDMABuffer, uint16_t uiLen)
{
    if(puiDMABuffer == NULL)
    {
        FHALT("Invalid Pointer to Data Buffer");
        return false;
    }
    if(uiLen == 0)
    {
        FHALT("Length of Data Buffer cannot be zero");
        return false;
    }

    if(!device_is_ready(pstDACDMADev))
    {
        FHALT("DMA device is not ready");
        return false;
    }

    return true;
}

sT_DAC_DMA_Flags stGet_DAC_DMA_Status( void )
{
    sT_DAC_DMA_Flags stTFlags = {0};
    struct dma_status stDMAStatus = {0};

    if(stDMADACModule == NULL)
        return stTFlags;

    if(dma_get_status(pstDACDMADev, DAC_DMA_CHANNEL, &stDMAStatus) == 0)
    {
        stTFlags.uiDMAFlags = stDMAStatus.busy ? 1U : 0U;
    }
    stTFlags.uiDACFlags = DAC_GetStatusFlags(stDMADACModule);

    return stTFlags;
}
#else
bool bValidate_DMAConfigurations(const uint32_t *puiDMABuffer, uint16_t uiLen)
{
    ARG_UNUSED(puiDMABuffer);
    ARG_UNUSED(uiLen);
    FHALT("DMA Is Not Enabled in DAC_ProjDef.h. Define 'USE_DMA'");
    return false;
}
#endif

#include "NXP_DAC_DMAConfig.h"

#include <stdatomic.h>
#include "fsl_edma.h"
#include "fsl_dac.h"
#include "../GenericMacro.h"
#include "DAC_ProjDef.h"

#if defined USE_DMA

static const struct device *const pstDACDMADev = DEVICE_DT_GET(DAC_DMA_CTLR_NODE);

typedef struct{
    LPDAC_Type *DACModule;
    struct dma_config stDACDMAConfig_t;
    struct dma_block_config stDACDMABlockConfig_t;
    edma_transfer_config_t stEDMATxConfig;
} sT_DACDMAConfig_t;

typedef struct{
    _Atomic bool bIsDMAUsed;
    struct k_work_delayable stDMAMonitor_Worker_t;
} sT_DMAControl_t;

sT_DACDMAConfig_t stTDACDMA_Ctrl = {
    .DACModule = NULL,
};

sT_DMAControl_t stTDMACtrl = {
    .bIsDMAUsed = false,
};

#define stDMABlockConfig          (stTDACDMA_Ctrl.stDACDMABlockConfig_t)
#define stDMAModuleConfig         (stTDACDMA_Ctrl.stDACDMAConfig_t)
#define stDMADACModule            (stTDACDMA_Ctrl.DACModule)
#define steDMATxConfig            (stTDACDMA_Ctrl.stEDMATxConfig)

static void vInit_DMAError_Monitor( void );
static void vCancel_DMAError_Monitor( void );
static void vDAC_DMAMonitor(struct k_work *work);

static inline void vSet_DMA_FlagForUse( void );
static inline void vClear_DMA_FlagForUse( void );
static inline bool bIsDMAActive( void );

bool bValidate_DMAConfigurations(const uint32_t *puiDMABuffer, uint16_t uiLen);

bool bSetup_DAC_DMA_Circular(const uint32_t *puiDMABuffer, uint16_t uiLen, uintptr_t ptrDAC)
{
    uint32_t uiTransferBytes = 0U;

    if(!bValidate_DMAConfigurations(puiDMABuffer, uiLen))
        return false;        
    if(ptrDAC == 0U)
    {
        FHALT("Invalid DAC base address for DMA setup");
        return false;
    }
    stDMADACModule = ((LPDAC_Type *)ptrDAC);
    uiTransferBytes = (uint32_t)uiLen * DAC_DMA_WORD_BYTES;

    EDMA_DisableChannelRequest(DMA0, DAC_DMA_CHANNEL);
    EDMA_SetChannelMux(DMA0, DAC_DMA_CHANNEL, DAC_DMA_SLOT_DAC0);
    EDMA_ClearChannelStatusFlags(DMA0,
                                 DAC_DMA_CHANNEL,
                                 kEDMA_DoneFlag | kEDMA_ErrorFlag | kEDMA_InterruptFlag);

    EDMA_PrepareTransfer(&steDMATxConfig,
                         (void *)puiDMABuffer,
                         DAC_DMA_WORD_BYTES,
                         (void *)&stDMADACModule->DATA,
                         DAC_DMA_WORD_BYTES,
                         DAC_DMA_WORD_BYTES,
                         uiTransferBytes,
                         kEDMA_MemoryToPeripheral);

    steDMATxConfig.srcMajorLoopOffset = -(int32_t)uiTransferBytes;
    steDMATxConfig.dstMajorLoopOffset = 0;
    steDMATxConfig.enabledInterruptMask = 0U;

    EDMA_SetTransferConfig(DMA0, DAC_DMA_CHANNEL, &steDMATxConfig, NULL);
    EDMA_EnableAutoStopRequest(DMA0, DAC_DMA_CHANNEL, false);

    DAC_EnableDMA(stDMADACModule, kDAC_FIFOWatermarkDMAEnable, true);
    EDMA_EnableChannelRequest(DMA0, DAC_DMA_CHANNEL);
    vInit_DMAError_Monitor();
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
    EDMA_DisableChannelRequest(DMA0, DAC_DMA_CHANNEL);
    stDMADACModule = NULL;
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
    uint32_t uiDMAFlags = stTFlags.uiDMAFlags;
    uint32_t uiDACFlags = stTFlags.uiDACFlags;

    if((uiDMAFlags & kEDMA_ErrorFlag) != 0U)
    {
        /* mark error, stop waveform, or notify upper layer */
    }

    if((uiDACFlags & kDAC_FIFOUnderflowFlag) != 0U)
    {
        /* FIFO starved: waveform integrity lost */
    }

    if((uiDACFlags & kDAC_FIFOOverflowFlag) != 0U)
    {
        /* DMA wrote when FIFO was full */
    }

    k_work_schedule(&stTDMACtrl.stDMAMonitor_Worker_t, K_MSEC(10));

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
    if(stDMADACModule == NULL)
        return stTFlags;
        
    stTFlags.uiDMAFlags = EDMA_GetChannelStatusFlags(DMA0, DAC_DMA_CHANNEL);
    stTFlags.uiDACFlags = DAC_GetStatusFlags(stDMADACModule);

    return stTFlags;
}
#else
bool bValidate_DMAConfigurations(const uint32_t *puiDMABuffer, uint16_t uiLen)
{
    FHALT("DMA Is Not Enabled in DAC_ProjDef.h. Define 'USE_DMA'");
    return false;
}
#endif

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
    DACError_Callback_t pVErrorCallbackFn;
    DACParam_UpdateComplete_Callback_t pvParamUpdateCallBackFn;
    _Atomic bool bIsDMAError;
    _Atomic bool bIsUpdateRequested;
    uint32_t *puiDMAUpdateBuffer;
    uint32_t uiUpdateLen;
} sT_DACDMAConfig_t;

typedef struct{
    _Atomic bool bIsDMAUsed;
    edma_handle_t stDMATransferHandle;
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
static inline void vSet_DMAError(eDAC_Error eError);
static inline bool bIsDMAUpdateRequested( void );
static inline void vClear_DMAUpdateRequest( void );
static inline void vSet_DMAUpdateRequest( void );

bool bValidate_DMAConfigurations(const uint32_t *puiDMABuffer, uint16_t uiLen);
static void vDAC_DMA_CallBack(edma_handle_t *psHandle, void *pUserData, bool bTransferDone, uint32_t uiTcds);
void vSwitch_DAC_DMA_Buffer( void );

bool bSetup_DAC_DMA_Circular(const uint32_t *puiDMABuffer, 
                            uint16_t uiLen, 
                            uintptr_t ptrDAC, 
                            DACError_Callback_t pvErrorCallback,
                            DACParam_UpdateComplete_Callback_t pvUpdateCallback)
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
    steDMATxConfig.enabledInterruptMask = kEDMA_MajorInterruptEnable;
    
    stTDACDMA_Ctrl.pVErrorCallbackFn = pvErrorCallback;
    stTDACDMA_Ctrl.pvParamUpdateCallBackFn = pvUpdateCallback;

    EDMA_SetTransferConfig(DMA0, DAC_DMA_CHANNEL, &steDMATxConfig, NULL);
    EDMA_EnableAutoStopRequest(DMA0, DAC_DMA_CHANNEL, false);

    EDMA_CreateHandle(&stTDMACtrl.stDMATransferHandle, DMA0, DAC_DMA_CHANNEL);
    EDMA_SetCallback(&stTDMACtrl.stDMATransferHandle, vDAC_DMA_CallBack, NULL);

    DAC_EnableDMA(stDMADACModule, kDAC_FIFOWatermarkDMAEnable, true);
    EDMA_EnableChannelRequest(DMA0, DAC_DMA_CHANNEL);
    vInit_DMAError_Monitor();
    return true;
}

void vDAC_DMA_CallBack(edma_handle_t *psHandle, void *pUserData, bool bTransferDone, uint32_t uiTcds)
{
    ARG_UNUSED(psHandle);
    ARG_UNUSED(pUserData);
    ARG_UNUSED(uiTcds);

    if(!bTransferDone)
        return;
    if(!bIsDMAUpdateRequested())
        return;

    vSwitch_DAC_DMA_Buffer();    
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

void vSwitch_DAC_DMA_Buffer( void )
{
    uint32_t uiTransferBytes = 0;

    uiTransferBytes = (uint32_t)stTDACDMA_Ctrl.uiUpdateLen * DAC_DMA_WORD_BYTES;

    EDMA_DisableChannelRequest(DMA0, DAC_DMA_CHANNEL);//Disable requests from DAC to DMA controller

    EDMA_ClearChannelStatusFlags(DMA0, DAC_DMA_CHANNEL, 
                                kEDMA_DoneFlag | kEDMA_ErrorFlag | kEDMA_InterruptFlag);
    
    EDMA_PrepareTransfer(&steDMATxConfig,
                         (void *)stTDACDMA_Ctrl.puiDMAUpdateBuffer,
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

    EDMA_EnableChannelRequest(DMA0, DAC_DMA_CHANNEL);
    vClear_DMAUpdateRequest();

    if(stTDACDMA_Ctrl.pvParamUpdateCallBackFn != NULL)
    {
        stTDACDMA_Ctrl.pvParamUpdateCallBackFn(true, NULL);
    }
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
    uint32_t uiDMAFlags = stTFlags.uiDMAFlags;
    uint32_t uiDACFlags = stTFlags.uiDACFlags;

    if((uiDMAFlags & kEDMA_ErrorFlag) != 0U && stTDACDMA_Ctrl.pVErrorCallbackFn != NULL)
    {
        vSet_DMAError(eDACErr_DMA_Error);
    }

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
    stTDACDMA_Ctrl.pVErrorCallbackFn(eError, NULL);
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

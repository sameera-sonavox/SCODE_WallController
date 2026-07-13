#include "../API_Usage_Definition.h"

#if defined(USE_SPI)

#ifdef DEBUG_SPI_SLAVE_TX
    #define debug_SlaveTx_Print             printf
#else
    #define debug_SlaveTx_Print(...)
#endif

#ifdef DEBUG_SPI_SLAVE_RX
    #define debug_SlaveRx_Print             printf
#else
    #define debug_SlaveRx_Print(...)
#endif 

#ifdef DEBUG_SPI_SLAVE_IRQ
    #define debug_SlaveIRQ_Print            printf
#else
    #define debug_SlaveIRQ_Print(...)
#endif

#include <stdbool.h>
#include <stdatomic.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pinctrl.h>
#include "fsl_lpspi.h"
#include "fsl_clock.h"
#include <zephyr/irq.h>

#include "NXP_SPI_API.h"
#include "NXP_SPI_ConfigValidation.h"
#include "NXP_SPI_LinkedList.h"
#include "../GenericMacro.h"

#if defined(USE_SPI0_SLAVE_HW_RDY_GPIO)
    #define SPI0_SLAVE_RDY_GPIO_NODE DT_ALIAS(spi0_slave_hw_rdy)
    #if DT_NODE_HAS_STATUS(SPI0_SLAVE_RDY_GPIO_NODE, okay)
        static const struct gpio_dt_spec stSPI0SlaveRdyGPIO = GPIO_DT_SPEC_GET(SPI0_SLAVE_RDY_GPIO_NODE, gpios);
        #define SPI0_SLAVE_HWRDY_PIN_AVAILABLE
    #endif
#endif

#if defined(USE_SPI1_SLAVE_HW_RDY_GPIO)
    #define SPI1_SLAVE_RDY_GPIO_NODE DT_ALIAS(spi1_slave_hw_rdy)
    #if DT_NODE_HAS_STATUS(SPI1_SLAVE_RDY_GPIO_NODE, okay)
        static const struct gpio_dt_spec stSPI1SlaveRdyGPIO = GPIO_DT_SPEC_GET(SPI1_SLAVE_RDY_GPIO_NODE, gpios);
        #define SPI1_SLAVE_HWRDY_PIN_AVAILABLE
    #endif
#endif

typedef enum
{
    eSPISlave_Busy,
    eSPISlave_Ready,
    eNUMBER_OF_SPISLAVE_STATEs
} eHWReady_State_t;

typedef struct
{
    eSPI_Slave_Id_t eActiveSlaveId;
    eSPI_DataLane_Width_t eSPI_BusWidth;

    lpspi_master_config_t stMasterConfig;
    lpspi_master_handle_t stMasterHandle;
    sT_SPISlave_Control_t *pstSPISlaveHead_Ctrl;
} sT_SPI_MasterCtrl;

typedef struct
{
    sT_SPIRxBuff_t *pstRxBuffHead;
    uint16_t uiBuffSize;
    uint8_t uiBuffDepth;
    _Atomic uint8_t uiBuffIndex;
    _Atomic eSPI_RxTarget_t eActiveRxTarget;
    uint8_t *puiDrainBuffer;
    SPI_PeripheralCallback_t pvPeripheral_UserCallBack;
} sT_SlaveCallback_Config_t;

typedef struct
{

} sT_MessageBus_Config_t;

typedef struct
{
    eSlaveData_PathConfig_t eDataPathType;
    union
    {
        sT_SlaveCallback_Config_t stTCallbackConfig;
        sT_MessageBus_Config_t stTMsgBusConfig;
    } data_pathConfig;    
} sT_UserNotify_Ctrl_t;

typedef struct
{
    bool bHWReady_Used;
    eSPI_HWRDY_PinState_t eHWRdy_PinState;
    const struct gpio_dt_spec *pstGPIOStruct;
} sT_HWReadyPin_Ctrl;

typedef struct
{
    _Atomic bool bIsTransferBusy;
    bool bIsHWMatchRequested;
    bool bIsTxOnlyNotification_Requested;
    _Atomic bool bIsMatchOccurred;
    _Atomic eTransfer_Type_t eTransferType;
    _Atomic bool bIsPeripheralTransferBusy;
    lpspi_slave_config_t stSlaveConfig;
    lpspi_slave_handle_t stSlaveHandle;
    sT_UserNotify_Ctrl_t stTUserNotifyCtrl;
    sT_SPI_RxOverflowCtrl_t stTDevRxOverflowControl;    
    sT_HWReadyPin_Ctrl stTHWRdyCtrl;
} sT_SPI_SlaveCtrl;

typedef struct
{
    eSPI_Mode_t eMode;
    union
    {
        sT_SPI_MasterCtrl stTMasterCtrl;
        sT_SPI_SlaveCtrl stTSlaveCtrl;
    }st_DevCtrlMode;

} sT_SPI_DeviceControl_t;

typedef struct
{
    eSPIModule_t eModuleId;
    LPSPI_Type *pstSPIDevice;
    uint32_t uiSPImodule_Clock_Hz;
    sT_SPI_DeviceControl_t stTSPIDevCtrl;

    bool bIsInitialized;
    _Atomic bool bIsTransferBusy;
    eSPI_NotificationType_t eNotificationType;
    struct k_work_delayable kw_SPITransferMonitor;
} sT_SPIModuleConfig_t;

sT_SPIModuleConfig_t staSPIModule[eNUMBER_OF_SPI_MODULEs] = {
    {.eModuleId = eSPI_0},
    {.eModuleId = eSPI_1}
};

static bool bConfig_SPI_MasterMode( sT_SPIConfig_t *pstSPIConfig );
static void vAssign_PinConfigurations(eSPIModule_t eModuleId);
static inline bool bIsModule_Initialized( eSPIModule_t eModule );
static inline eSPI_Mode_t eGetSPIMode( eSPIModule_t eModule );
static bool bDeinit_MasterMode( eSPIModule_t eModule );
static bool bDeinit_SlaveMode( eSPIModule_t eModule );
static void vDeInit_Slave_CallbackConfigs(sT_SPI_SlaveCtrl *pstSlaveCtrl);
static void vDeinit_Slave_InterruptConfigurations( sT_SPIModuleConfig_t *pstSPIModule, sT_SPI_SlaveCtrl *pstSlaveCtrl);

static bool bConfig_SPI_SlaveMode( sT_SPIConfig_t *pstSPIConfig );
static bool bSetup_HWMatchConfig(sT_SPIModuleConfig_t *pstSPIModule, sT_Peripheral_Config_t *pstPeripheralConfig);
static bool bAssign_UserDataPath_Configs(sT_SPI_SlaveCtrl *pstSlaveControl, sT_Peripheral_Config_t *pstPeripheralConfig);
static bool bConfig_CallbackConfigs(sT_SPI_SlaveCtrl *pstSlaveControl, sT_Peripheral_Config_t *pstPeripheralConfig);
static bool bPrepare_SlaveHW_ForNewTransfer(eSPIModule_t eModuleId);
static bool bArm_SlaveRx_ForCallback(sT_SPIModuleConfig_t *pstSPIModule,
                                     sT_SPI_SlaveCtrl *pstSlaveControl);

static bool bInit_SPISlave_HWRDY_Control(sT_SPIConfig_t *pstSPIConfig);
static inline void vSet_SPISlave_HWReadyState(eSPIModule_t eModuleId, eHWReady_State_t eState);

static bool bAllocate_MemoryForDrainBuffer(sT_SlaveCallback_Config_t *pstDevConfig);
static inline void vDeallocate_DrainBufferMemory(sT_SlaveCallback_Config_t *pstDevConfig);

static bool bSetup_OverflowPolicy(sT_SPI_RxOverflowCtrl_t *pstDevOverflowCtrl, eSPI_OverflowPolicy_t eOverflowPolicy);
static bool bApply_OverflowPolicy(sT_SPIModuleConfig_t *pstSPIModule, sT_SPI_SlaveCtrl *pstSlaveControl, sT_SPIRxBuff_t *pstRxBuffer);
static bool bApplyPolicy_DropNewest(sT_SPIModuleConfig_t *pstSPIModule,
                                    sT_SPI_SlaveCtrl *pstSlaveControl, 
                                    sT_SPI_RxOverflowCtrl_t *pstRxOverflowCtrl);

static inline bool bTryClaim_SlaveTransfer(sT_SPI_SlaveCtrl *pstSlaveControl);
static inline void vClear_SlaveTransferFlag(sT_SPI_SlaveCtrl *pstSlaveControl);
static inline bool bIsSlaveTransferBusy(sT_SPI_SlaveCtrl *pstSlaveControl);
static inline void vRecover_SlaveHW_AfterError(LPSPI_Type *base);

static void vConfig_TransferHandle(sT_SPIModuleConfig_t *pstSPIModule);
static void vConfigure_SPIInterrupt(sT_SPIModuleConfig_t *pstSPIModule);

static void vSPI_MasterCallback(LPSPI_Type *base,
                                lpspi_master_handle_t *handle,
                                status_t status,
                                void *userData);
static void vSPI_PeripheralModeCallback(LPSPI_Type *base,
                                        lpspi_slave_handle_t *handle,
                                        status_t status,
                                        void *userData);
static inline void vNotify_Via_Callback(eSPIModule_t eModuleId, 
                                        sT_SlaveCallback_Config_t *pstCallbackConfig, 
                                        lpspi_slave_handle_t *pshandle,
                                        eSPI_TransferResult_t eResult);
static void vNotify_Application_AtOverflow(sT_SPIModuleConfig_t *pstSPIModule, 
                                           sT_SPI_SlaveCtrl *pstSlaveControl,
                                           sT_SlaveCallback_Config_t *pstCallbackConfig,
                                           eSPI_TransferResult_t eResult);
static void vNotify_Application_AtNoError(sT_SPIModuleConfig_t *pstSPIModule,
                                          sT_SPI_SlaveCtrl *pstSlaveControl,
                                          sT_SlaveCallback_Config_t *pstCallbackConfig,
                                          eSPI_TransferResult_t eResult);
static void vNotify_SlaveTxComplete(sT_SPIModuleConfig_t *pstSPIModule, sT_SPI_SlaveCtrl *pstSlaveCtrl, 
                                    eSPI_TransferResult_t eResult);

static inline void vSet_SlaveTransferType(eSPIModule_t eModuleId, eTransfer_Type_t eTransferType);
static inline eTransfer_Type_t eGet_SlaveTransferType(eSPIModule_t eModuleId);
static inline void vSet_RxBuffId(sT_SlaveCallback_Config_t *pstCallbackConfig, uint8_t uiId);
static inline uint8_t uiGet_RxBuffId(sT_SlaveCallback_Config_t *pstCallbackConfig);
static inline eSPI_PeripheralEvent_Type_t eGetCurrentRxEventType(sT_SPI_RxOverflowCtrl_t *pstRxFlowCtrl);
static inline void vSet_RxEventType(sT_SPI_RxOverflowCtrl_t *pstRxFlowCtrl, eSPI_PeripheralEvent_Type_t eType);
static inline void vSet_RxBufferType(sT_SlaveCallback_Config_t *pstCallbackConfig, eSPI_RxTarget_t eNewTargetType);
static inline eSPI_RxTarget_t eGet_RxBufferType(sT_SlaveCallback_Config_t *pstCallbackConfig);

static void vEnable_SPI_IRQ(eSPIModule_t eModule);
static void vDisable_SPI_IRQ(eSPIModule_t eModule);
static void vLPSPI0_ISR(const void *arg);
static void vLPSPI1_ISR(const void *arg);
static void vConfigure_SPI_DMA(sT_SPIModuleConfig_t *pstSPIModule);
static void vSPI_FaultRecoveryHandler( struct k_work *work );
static inline void vHandle_SPIPeripheral_HWMatch(eSPIModule_t eModuleId);
static inline sT_SPIModuleConfig_t *pstGetSPIModule_From_KWork(struct k_work_delayable *work);
static inline void vSet_HWMatchFlag(sT_SPI_SlaveCtrl *pstPeriphreralConfig);
static inline bool bGet_HWMatchFlag(sT_SPI_SlaveCtrl *pstPeriphreralConfig);
static inline void vClear_HWMatchFlag(sT_SPI_SlaveCtrl *pstPeriphreralConfig);
static inline void vReConfigure_HWMatch_Interrupts(sT_SPIModuleConfig_t *pstSPIModule, sT_SPI_SlaveCtrl *pstPeriphreralConfig);

static inline bool bSPI_Master_Send(sT_SPIMasterTransfer_t *pstTTransfer, lpspi_transfer_t *pstLPSPITransfer);
static inline bool bSPI_Master_Receive(sT_SPIMasterTransfer_t *pstTTransfer, lpspi_transfer_t *pstLPSPITransfer);
static inline bool bSPI_Master_Transceive(sT_SPIMasterTransfer_t *pstTTransfer, lpspi_transfer_t *pstLPSPITransfer);
static uint32_t uiGetTransferPCsFlag(eSPI_PCS_t eHW_PCS_Ctrl);

static bool bSetUp_NewSlaveConfig(eSPIModule_t eModuleId, sT_SPI_MasterCtrl *pstMasterCtrl, eSPI_Slave_Id_t eNewSlaveId);
static bool bSPI_Assign_NewFrequency(eSPIModule_t eModuleId, 
                                     sT_SPI_MasterCtrl *pstMasterCtrl,
                                     sT_SPISlave_Config_t *pstCurConf, 
                                     sT_SPISlave_Config_t *pstNewConf);
static bool bSPI_Assign_SamplingEdge(eSPIModule_t eModuleId,
                                     sT_SPI_MasterCtrl *pstMasterCtrl, 
                                     sT_SPISlave_Config_t *pstCurConf, 
                                     sT_SPISlave_Config_t *pstNewConf);
static bool bSPI_Assign_CSPolarity(eSPIModule_t eModuleId,
                                   sT_SPI_MasterCtrl *pstMasterCtrl,
                                   sT_SPISlave_Config_t *pstNewConf);
static inline bool bSPI_Assign_CS_To_SCK_DelayTime(eSPIModule_t eModuleId,
                                     sT_SPI_MasterCtrl *pstMasterCtrl, 
                                     sT_SPISlave_Config_t *pstCurConf, 
                                     sT_SPISlave_Config_t *pstNewConf);
static inline bool bSPI_Assign_SCK_To_CS_DelayTime(eSPIModule_t eModuleId,
                                     sT_SPI_MasterCtrl *pstMasterCtrl, 
                                     sT_SPISlave_Config_t *pstCurConf, 
                                     sT_SPISlave_Config_t *pstNewConf);
static inline bool bSPI_Assign_Block_To_Block_DelayTime(eSPIModule_t eModuleId,
                                                        sT_SPI_MasterCtrl *pstMasterCtrl, 
                                                        sT_SPISlave_Config_t *pstCurConf, 
                                                        sT_SPISlave_Config_t *pstNewConf);

static inline void vClear_TransferBusyFlag(sT_SPIModuleConfig_t *pstSPIModule);
static inline bool bTryClaim_TransferBusyFlag(eSPIModule_t eModuleId);
static inline bool bTryComplete_TransferBusyFlag(sT_SPIModuleConfig_t *pstSPIModule);

static LPSPI_Type *pstGetSPIDevice(eSPIModule_t eModule);
static lpspi_clock_polarity_t eGetDefault_CPOL(eSPI_CPOL_CPHA_Type_t eSPIConf_CPOL);
static lpspi_clock_phase_t eGetDefault_CPHA(eSPI_CPOL_CPHA_Type_t eSPIConf_CPHA);
static lpspi_data_out_config_t eGetDataOutState_AtTxEnd(eSPI_DataOut_PinState_t eOutState);
static lpspi_shift_direction_t eGetEndianType(eSPI_ShiftDirection_t eShiftDirection);
static lpspi_pcs_polarity_config_t eGet_CS_PinActiveState(eSPI_CS_Polarity_t eCSPinState);
static lpspi_pcs_function_config_t eGet_PSCConfiguration(eSPI_DataLane_Width_t eDataLaneWidth);
static lpspi_pin_config_t eGet_SPIPinConfigurations(eSPI_PinCfg_For_Transfer_t ePinConfig);
static lpspi_which_pcs_t eGet_SPI_HW_CSPin(eSPI_PCS_t eHW_CSPin);
static void vAssign_SlaveDevices(sT_SPIModuleConfig_t *pstSPIModule, sT_SPIConfig_t *pstSPIConfig);

#pragma region Debugging Printf Definitions

#ifdef DEBUG_SPI_DEV_INIT
    #define SPI_INIT_Print              printf
#else
    #define SPI_INIT_Print(...)
#endif

#pragma endregion

#if defined(USE_SPI_0)
    #define LPSPI0_NODE     DT_NODELABEL(lpspi0)
    PINCTRL_DT_DEFINE(LPSPI0_NODE);        
#endif

#if defined(USE_SPI_1)
    #define LPSPI1_NODE     DT_NODELABEL(lpspi1)
    PINCTRL_DT_DEFINE(LPSPI1_NODE);        
#endif

void vInit_SPI( sT_SPIConfig_t *pstSPIConfig )
{
    bool bResult = false;

    if(pstSPIConfig == NULL)
    {
        FHALT("NULL Pointer Reference");
        return;
    }

    if(!bValidate_SPI_Config(pstSPIConfig))
    {
        pstSPIConfig->bIsOk = false;
        return;
    }

    switch(pstSPIConfig->stTSPIModeCtrl.eMode)
    {
        case eSPI_Mode_Controller:
            bResult = bConfig_SPI_MasterMode(pstSPIConfig);
            break;
        case eSPI_Mode_Peripheral:
            bResult = bConfig_SPI_SlaveMode(pstSPIConfig);
            break;
        default:
            bResult = false;
            break;
    }

    pstSPIConfig->bIsOk = bResult;

    sT_SPIModuleConfig_t *pstMoudle = &staSPIModule[pstSPIConfig->eModule];
    k_work_init_delayable(&pstMoudle->kw_SPITransferMonitor, vSPI_FaultRecoveryHandler);
    SPI_INIT_Print("SPI Module[%d] Init Result: %s\n", pstSPIConfig->eModule, (bResult) ? "Success" : "Failed");
}

#pragma region Slave Configurations

static bool bConfig_SPI_SlaveMode( sT_SPIConfig_t *pstSPIConfig )
{
    sT_SPIModuleConfig_t *pstSPIModule = &staSPIModule[pstSPIConfig->eModule];
    sT_SPI_SlaveCtrl *pstSlaveControl = &pstSPIModule->stTSPIDevCtrl.st_DevCtrlMode.stTSlaveCtrl;
    sT_Peripheral_Config_t *pstPeripheralConfig = &pstSPIConfig->stTSPIModeCtrl.spi_mode.stTConfig_Peripheral;
    sT_SPI_RxOverflowCtrl_t *pstOverflowCtrl = &pstSlaveControl->stTDevRxOverflowControl;

    pstSPIModule->bIsInitialized = false;
    vClear_TransferBusyFlag(pstSPIModule);
    pstSPIModule->eNotificationType = pstSPIConfig->eNotificationType;
    pstSPIModule->stTSPIDevCtrl.eMode = pstSPIConfig->stTSPIModeCtrl.eMode;
    pstSPIModule->uiSPImodule_Clock_Hz = CLOCK_GetLpspiClkFreq(pstSPIConfig->eModule);

    pstSPIModule->pstSPIDevice = pstGetSPIDevice(pstSPIConfig->eModule);

    lpspi_slave_config_t *pstSlave_HWConfig = &pstSlaveControl->stSlaveConfig;
    if(pstSlave_HWConfig == NULL)
    {
        FHALT("Null Pointer reference");
        return false;
    }
    LPSPI_SlaveGetDefaultConfig(pstSlave_HWConfig);

    pstSlaveControl->bIsTxOnlyNotification_Requested = pstPeripheralConfig->bRequest_TxNotifications;

    pstSlave_HWConfig->bitsPerFrame = pstPeripheralConfig->uiFrameSize;
    pstSlave_HWConfig->cpol = eGetDefault_CPOL(pstPeripheralConfig->eCPOLCPH_Ctrl);
    pstSlave_HWConfig->cpha = eGetDefault_CPHA(pstPeripheralConfig->eCPOLCPH_Ctrl);
    pstSlave_HWConfig->dataOutConfig = eGetDataOutState_AtTxEnd(pstSPIConfig->eDataOutPinState);
    pstSlave_HWConfig->direction = eGetEndianType(pstPeripheralConfig->eEndianFormat);
    pstSlave_HWConfig->pcsActiveHighOrLow = eGet_CS_PinActiveState(pstPeripheralConfig->eSlaveMode_CS_Ctrl);
    pstSlave_HWConfig->pinCfg = eGet_SPIPinConfigurations(pstSPIConfig->ePinConfig);
    pstSlave_HWConfig->whichPcs = eGet_SPI_HW_CSPin(pstPeripheralConfig->eCSPin);
    
    if(!bAssign_UserDataPath_Configs(pstSlaveControl, pstPeripheralConfig))
        return false;
    
    if(!bSetup_OverflowPolicy(pstOverflowCtrl, 
                              pstSPIConfig->stTSPIModeCtrl.spi_mode.stTConfig_Peripheral.stTRxControl.eOverflowPolicy))
        return false;
    if(!bInit_SPISlave_HWRDY_Control(pstSPIConfig))
        return false;

    vAssign_PinConfigurations(pstSPIConfig->eModule);

    LPSPI_SlaveInit(pstSPIModule->pstSPIDevice,
                    pstSlave_HWConfig);

    if(!bSetup_HWMatchConfig(pstSPIModule, pstPeripheralConfig))
        return false;

    vConfig_TransferHandle(pstSPIModule);

    if(!bPrepare_SlaveHW_ForNewTransfer(pstSPIConfig->eModule))
        return false;

    vSet_SlaveTransferType(pstSPIModule->eModuleId, eTransfer_Rx_Only);    

    pstSPIModule->bIsInitialized = true;
    printk("SPI Slave Initialized\n\r");   
    return true;
}

static bool bSetup_OverflowPolicy(sT_SPI_RxOverflowCtrl_t *pstDevOverflowCtrl, eSPI_OverflowPolicy_t eOverflowPolicy)
{
    if(pstDevOverflowCtrl == NULL)
    {
        FHALT("Null Pointer reference");
        return false;
    }

    pstDevOverflowCtrl->eOverflowPolicy = eOverflowPolicy;
    pstDevOverflowCtrl->uiDroppedFrameCount = 0U;
    pstDevOverflowCtrl->uiOverflowCount = 0U;
    pstDevOverflowCtrl->eEventType = eSPI_PeripheralEvent_RxReady;
    return true;
}

static bool bPrepare_SlaveHW_ForNewTransfer(eSPIModule_t eModuleId)
{
    if(eGetSPIMode(eModuleId) != eSPI_Mode_Peripheral)
    {
        FHALT("Invalid Call to Slave HW Preperation while module is in Master Mode @ModuleId: %d", eModuleId);
        return false;
    }

    sT_SPIModuleConfig_t *pstSPIModule = &staSPIModule[eModuleId];
    sT_SPI_SlaveCtrl *pstSlaveControl = &pstSPIModule->stTSPIDevCtrl.st_DevCtrlMode.stTSlaveCtrl;    
    
    switch(pstSlaveControl->stTUserNotifyCtrl.eDataPathType)
    {
        case eTransfer_Use_Callback:
            return bArm_SlaveRx_ForCallback(pstSPIModule, pstSlaveControl);
        case eTransfer_Use_MessageBus:
            FHALT("Not Implemented for Using Message Bus");
            return false;
        default:
            FHALT("Invalid Data Path Type: %d", pstSlaveControl->stTUserNotifyCtrl.eDataPathType);
            return false;
    }
}
                                    
static inline bool bTryClaim_SlaveTransfer(sT_SPI_SlaveCtrl *pstSlaveControl)
{
    if(pstSlaveControl == NULL)
    {
        FHALT("Null Pointer Reference");
        return false;
    }

    bool bRes = atomic_exchange_explicit(&pstSlaveControl->bIsPeripheralTransferBusy, true, memory_order_acq_rel);
    return !bRes;
}

static inline void vClear_SlaveTransferFlag(sT_SPI_SlaveCtrl *pstSlaveControl)
{
    if(pstSlaveControl == NULL)
    {
        FHALT("Null Pointer Reference");
        return;
    }
    atomic_store_explicit(&pstSlaveControl->bIsPeripheralTransferBusy, false, memory_order_release);
}

static inline bool bIsSlaveTransferBusy(sT_SPI_SlaveCtrl *pstSlaveControl)
{
    if(pstSlaveControl == NULL)
    {
        FHALT("Null Pointer Reference");
        return false;
    }
    bool bRes = atomic_load_explicit(&pstSlaveControl->bIsPeripheralTransferBusy, memory_order_acquire);
    return bRes;
}

static inline void vRecover_SlaveHW_AfterError(LPSPI_Type *base)
{
    if(base == NULL)
    {
        return;
    }

    LPSPI_FlushFifo(base, true, true);
    LPSPI_ClearStatusFlags(base,
                           kLPSPI_TransmitErrorFlag |
                           kLPSPI_ReceiveErrorFlag |
                           kLPSPI_TransferCompleteFlag |
                           kLPSPI_FrameCompleteFlag |
                           kLPSPI_WordCompleteFlag);
}

bool bSPI_PeripheralSendResponse(sT_SPIPreipheralResponse_t stTSlaveResponse)
{
    if(eGetSPIMode(stTSlaveResponse.eModuleId) != eSPI_Mode_Peripheral)
    {
        FHALT("Invalid Transfer Request when SPI not in Slave Mode @SPIMod: %d", stTSlaveResponse.eModuleId);
        return false;
    }
    if(stTSlaveResponse.puiTxData == NULL || stTSlaveResponse.uiLen == 0)
    {
        FHALT("Invalid Parameter settings for Slave Transmission");
        return false;
    }

    sT_SPIModuleConfig_t *pstSPIModule = &staSPIModule[stTSlaveResponse.eModuleId];
    sT_SPI_SlaveCtrl *pstSlaveControl = &pstSPIModule->stTSPIDevCtrl.st_DevCtrlMode.stTSlaveCtrl;
    sT_SlaveCallback_Config_t *pstDevConfig = &pstSlaveControl->stTUserNotifyCtrl.data_pathConfig.stTCallbackConfig;

    if(!bTryClaim_SlaveTransfer(pstSlaveControl))
    {
        FHALT("Slave cannot Transmit");
        return false;
    }
    vSet_SPISlave_HWReadyState(stTSlaveResponse.eModuleId, eSPISlave_Busy);

    lpspi_transfer_t stTransfer = {0};
    sT_SPIRxBuff_t *pstRxBuffer = pstGet_RxBuffer_byState(pstDevConfig->pstRxBuffHead, eBuffer_Filling);
    uint8_t *puiDrainBuffer = pstSlaveControl->stTUserNotifyCtrl.data_pathConfig.stTCallbackConfig.puiDrainBuffer;
    if(pstRxBuffer != NULL)
    {
        FHALT("Cannot arm slave TX while RX buffer is filling");
        vClear_SlaveTransferFlag(pstSlaveControl);
        vSet_SPISlave_HWReadyState(stTSlaveResponse.eModuleId, eSPISlave_Busy);
        return false;
    }
    
    stTransfer.rxData = puiDrainBuffer;
    stTransfer.txData = stTSlaveResponse.puiTxData;
    stTransfer.dataSize = stTSlaveResponse.uiLen;
    stTransfer.configFlags = 0U;

    vSet_SlaveTransferType(stTSlaveResponse.eModuleId, eTransfer_Tx_Only);

    status_t status = LPSPI_SlaveTransferNonBlocking(pstSPIModule->pstSPIDevice,
                                                     &pstSlaveControl->stSlaveHandle,
                                                     &stTransfer);
    if(status != kStatus_Success)
    {
        if(stTransfer.rxData != puiDrainBuffer)
        {
            vSet_RxBufferState(pstRxBuffer->uiBuffId, eBuffer_Free, pstDevConfig->pstRxBuffHead);
            FHALT("Slave HW Preperation failed for Callback");
        }
        vClear_SlaveTransferFlag(pstSlaveControl);
        vSet_SPISlave_HWReadyState(stTSlaveResponse.eModuleId, eSPISlave_Busy);
        return false;
    }

    vSet_SPISlave_HWReadyState(stTSlaveResponse.eModuleId, eSPISlave_Ready);

#ifdef DEBUG_SPI_SLAVE_TX
    uint32_t uiTcr = LPSPI_GetTcr(pstSPIModule->pstSPIDevice);
    uint32_t uiCfgr1 = pstSPIModule->pstSPIDevice->CFGR1;
    debug_SlaveTx_Print("Slave TX armed: TX=%p Len=%u TCR=0x%08x CFGR1=0x%08x TXMSK=%u RXMSK=%u PCS=%u PINCFG=%u OUTCFG=%u TXFIFO=%u RXFIFO=%u\n\r",
                        stTransfer.txData,
                        (unsigned int)stTransfer.dataSize,
                        (unsigned int)uiTcr,
                        (unsigned int)uiCfgr1,
                        (unsigned int)((uiTcr & LPSPI_TCR_TXMSK_MASK) >> LPSPI_TCR_TXMSK_SHIFT),
                        (unsigned int)((uiTcr & LPSPI_TCR_RXMSK_MASK) >> LPSPI_TCR_RXMSK_SHIFT),
                        (unsigned int)((uiTcr & LPSPI_TCR_PCS_MASK) >> LPSPI_TCR_PCS_SHIFT),
                        (unsigned int)((uiCfgr1 & LPSPI_CFGR1_PINCFG_MASK) >> LPSPI_CFGR1_PINCFG_SHIFT),
                        (unsigned int)((uiCfgr1 & LPSPI_CFGR1_OUTCFG_MASK) >> LPSPI_CFGR1_OUTCFG_SHIFT),
                        (unsigned int)LPSPI_GetTxFifoCount(pstSPIModule->pstSPIDevice),
                        (unsigned int)LPSPI_GetRxFifoCount(pstSPIModule->pstSPIDevice));
#endif

    return true;        
}

static inline void vSet_SPISlave_HWReadyState(eSPIModule_t eModuleId, eHWReady_State_t eState)
{
    if(eModuleId >= eNUMBER_OF_SPI_MODULEs)
        return;

    sT_SPIModuleConfig_t *pstSPIModule = &staSPIModule[eModuleId];
    sT_HWReadyPin_Ctrl *pstHWRdyCtrl = &pstSPIModule->stTSPIDevCtrl.st_DevCtrlMode.stTSlaveCtrl.stTHWRdyCtrl;
    if(!pstHWRdyCtrl->bHWReady_Used || pstHWRdyCtrl->pstGPIOStruct == NULL)
        return;
    
    switch(pstHWRdyCtrl->eHWRdy_PinState)
    {
        case eSPI_Rdy_Active_Low:
            if(eState == eSPISlave_Busy)
                gpio_pin_set_raw(pstHWRdyCtrl->pstGPIOStruct->port, pstHWRdyCtrl->pstGPIOStruct->pin, 1);
            else if(eState == eSPISlave_Ready)
                gpio_pin_set_raw(pstHWRdyCtrl->pstGPIOStruct->port, pstHWRdyCtrl->pstGPIOStruct->pin, 0);
            else
                return;
            break;
        case eSPI_Rdy_Active_High:
            if(eState == eSPISlave_Busy)
                gpio_pin_set_raw(pstHWRdyCtrl->pstGPIOStruct->port, pstHWRdyCtrl->pstGPIOStruct->pin, 0);
            else if(eState == eSPISlave_Ready)
                gpio_pin_set_raw(pstHWRdyCtrl->pstGPIOStruct->port, pstHWRdyCtrl->pstGPIOStruct->pin, 1);
            else
                return;
            break;
        default:
            break;
    }
}

static bool bInit_SPISlave_HWRDY_Control(sT_SPIConfig_t *pstSPIConfig)
{
    int ret;

    eSPIModule_t eModuleId = pstSPIConfig->eModule;
    sT_SPIModuleConfig_t *pstSPIModule = &staSPIModule[eModuleId];

    switch(eGetSPIMode(eModuleId))
    {
        case eSPI_Mode_Peripheral:
            break;
        case eSPI_Mode_Controller:
        default:
            FHALT("SPI Master Mode, SPI API has no control over the HW Ready Pin");
            return false;
    }

    sT_HWReadyPin_Ctrl *pstHWRdyCtrl = &pstSPIModule->stTSPIDevCtrl.st_DevCtrlMode.stTSlaveCtrl.stTHWRdyCtrl;
    pstHWRdyCtrl->bHWReady_Used = false;
    pstHWRdyCtrl->eHWRdy_PinState = pstSPIConfig->stTSPIModeCtrl.spi_mode.stTConfig_Peripheral.eHWRdy_PinState;
    pstHWRdyCtrl->pstGPIOStruct = NULL;

    switch(eModuleId)
    {
        case eSPI_0:
#ifdef SPI0_SLAVE_HWRDY_PIN_AVAILABLE
            pstHWRdyCtrl->pstGPIOStruct = &stSPI0SlaveRdyGPIO;
#endif
            break;
        case eSPI_1:
#ifdef SPI1_SLAVE_HWRDY_PIN_AVAILABLE
            pstHWRdyCtrl->pstGPIOStruct = &stSPI1SlaveRdyGPIO;
#endif
            break;
        default:
            return false;
    }

    if(pstHWRdyCtrl->pstGPIOStruct == NULL)
    {
        return true;
    }

    if(!gpio_is_ready_dt(pstHWRdyCtrl->pstGPIOStruct))
    {
        FHALT("HW Ready GPIO device is not ready");
        return false;
    }

    switch(pstHWRdyCtrl->eHWRdy_PinState)
    {
        case eSPI_Rdy_Active_Low:
            ret = gpio_pin_configure_dt(pstHWRdyCtrl->pstGPIOStruct, GPIO_OUTPUT);
            if(ret != 0)
            {
                FHALT("GPIO for HW Ready Counld not be configured. Failed with error : %d", ret);
                return false;
            }
            ret = gpio_pin_set_raw(pstHWRdyCtrl->pstGPIOStruct->port, pstHWRdyCtrl->pstGPIOStruct->pin, 1);
            if(ret != 0)
            {
                FHALT("GPIO for HW Ready Pin Counld not be set. Failed with error : %d", ret);
                return false;                
            }
            break;
        case eSPI_Rdy_Active_High:
            ret = gpio_pin_configure_dt(pstHWRdyCtrl->pstGPIOStruct, GPIO_OUTPUT);
            if(ret != 0)
            {
                FHALT("GPIO for HW Ready Counld not be configured. Failed with error : %d", ret);
                return false;
            }
            ret = gpio_pin_set_raw(pstHWRdyCtrl->pstGPIOStruct->port, pstHWRdyCtrl->pstGPIOStruct->pin, 0);
            if(ret != 0)
            {
                FHALT("GPIO for HW Ready Pin Counld not be set. Failed with error : %d", ret);
                return false;                
            }
            break;
        default:
            FHALT("Invalid HW Ready Pin Configuration : %d", 
                  pstHWRdyCtrl->eHWRdy_PinState);
            return false;
        
    }

    pstHWRdyCtrl->bHWReady_Used = true;
    return true;
}

static bool bArm_SlaveRx_ForCallback(sT_SPIModuleConfig_t *pstSPIModule,
                                     sT_SPI_SlaveCtrl *pstSlaveControl)
{
    sT_SlaveCallback_Config_t *pstDevConfig = &pstSlaveControl->stTUserNotifyCtrl.data_pathConfig.stTCallbackConfig;

    if(!bTryClaim_SlaveTransfer(pstSlaveControl))
    {
        return true;
    }
    vSet_SPISlave_HWReadyState(pstSPIModule->eModuleId, eSPISlave_Busy);

    sT_SPIRxBuff_t *pstRxBuffer = pstGet_RxBuffer_byState(pstDevConfig->pstRxBuffHead, eBuffer_Free);
    if(pstRxBuffer == NULL)
    {
        return bApply_OverflowPolicy(pstSPIModule, pstSlaveControl, pstRxBuffer);
    }

    vSet_RxEventType(&pstSlaveControl->stTDevRxOverflowControl, eSPI_PeripheralEvent_RxReady);
    vSet_RxBufferType(pstDevConfig, eSPI_RxTarget_AppBuffer);
    vSet_RxBuffId(pstDevConfig, pstRxBuffer->uiBuffId);
    vSet_RxBufferState(pstRxBuffer->uiBuffId, eBuffer_Filling, pstDevConfig->pstRxBuffHead);

    lpspi_transfer_t stTransfer = {
        .txData = NULL,
        .rxData = pstRxBuffer->puiBuffer,
        .dataSize = pstRxBuffer->uisize,
        .configFlags = 0U
    };

    vSet_SlaveTransferType(pstSPIModule->eModuleId, eTransfer_Rx_Only);

    status_t status = LPSPI_SlaveTransferNonBlocking(pstSPIModule->pstSPIDevice,
                                                     &pstSlaveControl->stSlaveHandle,
                                                     &stTransfer);
    if(status != kStatus_Success)
    {
        vClear_SlaveTransferFlag(pstSlaveControl);
        vSet_RxBufferState(pstRxBuffer->uiBuffId, eBuffer_Free, pstDevConfig->pstRxBuffHead);
        vSet_SPISlave_HWReadyState(pstSPIModule->eModuleId, eSPISlave_Busy);
        FHALT("Slave HW Preperation failed for Callback");
        return false;
    }
    vSet_SPISlave_HWReadyState(pstSPIModule->eModuleId, eSPISlave_Ready);
    return true;
}

static bool bApply_OverflowPolicy(sT_SPIModuleConfig_t *pstSPIModule, sT_SPI_SlaveCtrl *pstSlaveControl, sT_SPIRxBuff_t *pstRxBuffer)
{
    if(pstSlaveControl == NULL)
    {
        FHALT("Null Pointer reference");
        return false;
    }

    sT_SPI_RxOverflowCtrl_t *pstRxOverflowCtrl = &pstSlaveControl->stTDevRxOverflowControl;

    switch(pstRxOverflowCtrl->eOverflowPolicy)
    {
        case eSPI_Overflow_DropNewest:
            return bApplyPolicy_DropNewest(pstSPIModule, pstSlaveControl, pstRxOverflowCtrl);
        case eSPI_Overflow_DropOldest:
        case eSPI_Overflow_StopAndReport:
        case eSPI_Overflow_OverWriteOldest:
        default:
            FHALT("Requested Policy Not Implemented : %d", pstRxOverflowCtrl->eOverflowPolicy);
            return false;
    }
}

static bool bApplyPolicy_DropNewest(sT_SPIModuleConfig_t *pstSPIModule,
                                    sT_SPI_SlaveCtrl *pstSlaveControl, 
                                    sT_SPI_RxOverflowCtrl_t *pstRxOverflowCtrl)
{
    sT_SlaveCallback_Config_t *pstCallbackConfig = &pstSlaveControl->stTUserNotifyCtrl.data_pathConfig.stTCallbackConfig;    
    pstRxOverflowCtrl->uiOverflowCount++;

    lpspi_transfer_t stTransfer = {0};
    stTransfer.txData = NULL;
    stTransfer.rxData = pstCallbackConfig->puiDrainBuffer;
    stTransfer.dataSize = pstCallbackConfig->uiBuffSize;
    stTransfer.configFlags = 0U;

    vSet_SlaveTransferType(pstSPIModule->eModuleId, eTransfer_Rx_Only);
    
    status_t status = LPSPI_SlaveTransferNonBlocking(pstSPIModule->pstSPIDevice,
                                                     &pstSlaveControl->stSlaveHandle,
                                                     &stTransfer);
    if(status != kStatus_Success)
    {
        vClear_SlaveTransferFlag(pstSlaveControl);
        vSet_SPISlave_HWReadyState(pstSPIModule->eModuleId, eSPISlave_Busy);
        FHALT("Slave HW Preperation failed for Callback");
        vSet_RxEventType(pstRxOverflowCtrl, eSPI_PeripheralEvent_RxOverflow);
        return false;
    }
    vSet_SPISlave_HWReadyState(pstSPIModule->eModuleId, eSPISlave_Ready);
    vSet_RxEventType(pstRxOverflowCtrl, eSPI_PeripheralEvent_RxOverflow);
    vSet_RxBufferType(pstCallbackConfig, eSPI_RxTarget_DrainBuffer);
    return true;
}

static bool bAssign_UserDataPath_Configs(sT_SPI_SlaveCtrl *pstSlaveControl, sT_Peripheral_Config_t *pstPeripheralConfig)
{
    switch(pstPeripheralConfig->stTRxControl.eDataPathType)
    {
        case eTransfer_Use_Callback:
            return bConfig_CallbackConfigs(pstSlaveControl, pstPeripheralConfig);
            break;
        case eTransfer_Use_MessageBus:
        default:
            FHALT("Not Implemented Yet");
            return false;
    }
}

static bool bConfig_CallbackConfigs(sT_SPI_SlaveCtrl *pstSlaveControl, sT_Peripheral_Config_t *pstPeripheralConfig)
{
    sT_Callback_Ctrl *pstUserConfigs = &pstPeripheralConfig->stTRxControl.slave_dataPath.stTCallbackConfig;
    if(pstUserConfigs == NULL)
    {
        FHALT("Null pointer reference");
        return false;
    }

    pstSlaveControl->stTUserNotifyCtrl.eDataPathType = eTransfer_Use_Callback;
    sT_SlaveCallback_Config_t *pstDevConfig = &pstSlaveControl->stTUserNotifyCtrl.data_pathConfig.stTCallbackConfig;

    if(pstDevConfig->pstRxBuffHead != NULL)
    {
        vDeallocate_DrainBufferMemory(pstDevConfig);
        vFree_SPIRxBuffers(&pstDevConfig->pstRxBuffHead);
    }

    pstDevConfig->uiBuffDepth = pstUserConfigs->uiBuffCount;
    pstDevConfig->uiBuffSize = pstUserConfigs->uiBuffSize;

    uint8_t uiIndex = 0;
    while(uiIndex < pstDevConfig->uiBuffDepth)
    {
        bool res = bInsert_SPIRxBuffer_AtEnd(&pstDevConfig->pstRxBuffHead,
                                            uiIndex,
                                            eBuffer_Free,
                                            pstDevConfig->uiBuffSize);
        if(!res)
        {
            vDeallocate_DrainBufferMemory(pstDevConfig);
            vFree_SPIRxBuffers(&pstDevConfig->pstRxBuffHead);
            return false;
        }

        uiIndex++;
    }

    if(!bAllocate_MemoryForDrainBuffer(pstDevConfig))
    {
        vFree_SPIRxBuffers(&pstDevConfig->pstRxBuffHead);
        FHALT("API Drain Buffer could not be created");
        return false;
    }

    pstDevConfig->pvPeripheral_UserCallBack = pstUserConfigs->pvSPI_PeripheralCallBack;
    return true;
}

static bool bAllocate_MemoryForDrainBuffer(sT_SlaveCallback_Config_t *pstDevConfig)
{
    vDeallocate_DrainBufferMemory(pstDevConfig);

    pstDevConfig->puiDrainBuffer = (uint8_t *)malloc(pstDevConfig->uiBuffSize * sizeof(uint8_t));
    if(pstDevConfig->puiDrainBuffer == NULL)
    {
        FHALT("API Drain Buffer could not be created");
        return false;
    }
    return true;
}

static inline void vDeallocate_DrainBufferMemory(sT_SlaveCallback_Config_t *pstDevConfig)
{
    if(pstDevConfig->puiDrainBuffer == NULL)
        return;

    free(pstDevConfig->puiDrainBuffer);
    pstDevConfig->puiDrainBuffer = NULL;
}

static bool bSetup_HWMatchConfig(sT_SPIModuleConfig_t *pstSPIModule, sT_Peripheral_Config_t *pstPeripheralConfig)
{
    if(pstPeripheralConfig == NULL || pstSPIModule == NULL)
    {
        FHALT("Null Pointer reference");
        return false;
    }

    LPSPI_Type *pstSPIDevice = pstSPIModule->pstSPIDevice;
    sT_SPI_SlaveCtrl *pstPeriphreralConfig = &pstSPIModule->stTSPIDevCtrl.st_DevCtrlMode.stTSlaveCtrl;
    sT_HWMatch_Config_t *pstHWMatchConfig = &pstPeripheralConfig->stTHWMatchConfig;
    
    pstPeriphreralConfig->bIsHWMatchRequested = true;
    lpspi_match_config_t eMatchMode;
    switch(pstHWMatchConfig->eHW_Recv_SyncType)
    {
        case eHW_Match_Disabled:
            eMatchMode = kLPSI_MatchDisabled;
            pstPeriphreralConfig->bIsHWMatchRequested = false;
            break;
        case eHW_Match_FirstWord_Equals_Match0_Or_Match1:
            eMatchMode = kLPSI_1stWordEqualsM0orM1;
            break;
        case eHW_Match_AnyWord_Equals_Match0_Or_Match1:
            eMatchMode = kLPSI_AnyWordEqualsM0orM1;
            break;
        case eHW_Match_With_Match0_1_Sequentially:
            eMatchMode = kLPSI_1stWordEqualsM0and2ndWordEqualsM1;
            break;
        case eHW_Match_With_Match0_1_AnyWord:
            eMatchMode = kLPSI_AnyWordEqualsM0andNxtWordEqualsM1;
            break;
        case eHW_Match_FirstWord_With_Match0_MaskedWith_Match1:
            eMatchMode = kLPSI_1stWordAndM1EqualsM0andM1;
            break;
        case eHW_Match_AnyWord_With_Match0_MaskedWith_Match1:
            eMatchMode = kLPSI_AnyWordAndM1EqualsM0andM1;
            break;
        default:
            FHALT("Invalid HW Match Type: %d", pstHWMatchConfig->eHW_Recv_SyncType);
            pstPeriphreralConfig->bIsHWMatchRequested = false;
            return false;
    }

    bool bWasModEnabled = ((pstSPIDevice->CR & LPSPI_CR_MEN_MASK) != 0);
    LPSPI_Enable(pstSPIDevice, false);

    pstSPIDevice->DMR0 = LPSPI_DMR0_MATCH0(pstHWMatchConfig->uiMatch0_Value);
    pstSPIDevice->DMR1 = LPSPI_DMR1_MATCH1(pstHWMatchConfig->uiMatch1_Value);

    pstSPIDevice->CFGR1 = (pstSPIDevice->CFGR1 & ~LPSPI_CFGR1_MATCFG_MASK) | LPSPI_CFGR1_MATCFG(eMatchMode);

    if(pstHWMatchConfig->bFIFO_StoreOnly_MatchedData)
    {
        pstSPIDevice->CFGR0 |= LPSPI_CFGR0_RDMO_MASK;
    }
    else
    {
        pstSPIDevice->CFGR0 &= ~LPSPI_CFGR0_RDMO_MASK;
    }

    LPSPI_ClearStatusFlags(pstSPIDevice, kLPSPI_DataMatchFlag);
    bool bNeedLPSPI_IRQEn = false;

    if(pstSPIModule->eNotificationType == eNotify_Interrupt || pstPeriphreralConfig->bIsHWMatchRequested)
        bNeedLPSPI_IRQEn = true;
    if(bNeedLPSPI_IRQEn)
    {
        vEnable_SPI_IRQ(pstSPIModule->eModuleId);
    }
    if(pstPeriphreralConfig->bIsHWMatchRequested)
    {
        LPSPI_EnableInterrupts(pstSPIDevice, kLPSPI_DataMatchInterruptEnable);
    }
    else
    {
        LPSPI_DisableInterrupts(pstSPIDevice, kLPSPI_DataMatchInterruptEnable);
    }

    LPSPI_Enable(pstSPIDevice, bWasModEnabled);
    return true;
}

#pragma endregion

bool bDeInit_SPI( eSPIModule_t eSPIModule )
{
    if(eSPIModule >= eNUMBER_OF_SPI_MODULEs)
    {
        FHALT("Invalid SPI Module : %d", eSPIModule);
        return false;
    }
    if(!bIsModule_Initialized(eSPIModule))
    {
        FHALT("SPI Module[%d] not initialized", eSPIModule);
        return false;    
    }

    switch(eGetSPIMode(eSPIModule))
    {
        case eSPI_Mode_Controller:
            return bDeinit_MasterMode(eSPIModule);
        case eSPI_Mode_Peripheral:
            return bDeinit_SlaveMode(eSPIModule);
        default:
            return false;
    }
}

static bool bDeinit_MasterMode( eSPIModule_t eModule )
{
    sT_SPIModuleConfig_t *pstSPIModule = &staSPIModule[eModule];
    sT_SPI_MasterCtrl *pstMasterCtrl = &pstSPIModule->stTSPIDevCtrl.st_DevCtrlMode.stTMasterCtrl;
    lpspi_master_config_t *pstspiMasterConf = &pstMasterCtrl->stMasterConfig;

    switch(pstSPIModule->eNotificationType)
    {
        case eNotify_Interrupt:
            vDisable_SPI_IRQ(eModule);
            break;
        case eNotify_DMA:
            break;
        default:
            FHALT("Invalid Notification Type: %d", pstSPIModule->eNotificationType);
            break;
    }

    LPSPI_Deinit(pstSPIModule->pstSPIDevice);

    LPSPI_MasterGetDefaultConfig(pstspiMasterConf);
    vRelease_SPISLaves(&pstMasterCtrl->pstSPISlaveHead_Ctrl);//Release previously allocated memory
    vClear_TransferBusyFlag(pstSPIModule);
    pstSPIModule->bIsInitialized = false;
    pstMasterCtrl->eActiveSlaveId = eNUMBER_OF_SPI_SLAVEs;
    return true;
}

static bool bDeinit_SlaveMode( eSPIModule_t eModule )
{
    sT_SPIModuleConfig_t *pstSPIModule = &staSPIModule[eModule];
    sT_SPI_SlaveCtrl *pstSlaveCtrl = &pstSPIModule->stTSPIDevCtrl.st_DevCtrlMode.stTSlaveCtrl;
    lpspi_slave_config_t *pstspiSlaveConf = &pstSlaveCtrl->stSlaveConfig;

    switch(pstSPIModule->eNotificationType)
    {
        case eNotify_Interrupt:
            vDeinit_Slave_InterruptConfigurations(pstSPIModule, pstSlaveCtrl);
            break;
        case eNotify_DMA:
            break;
        default:
            FHALT("Invalid Notification Type: %d", pstSPIModule->eNotificationType);
            break;
    }

    LPSPI_Deinit(pstSPIModule->pstSPIDevice);
    LPSPI_SlaveGetDefaultConfig(pstspiSlaveConf);

    switch(pstSlaveCtrl->stTUserNotifyCtrl.eDataPathType)
    {
        case eTransfer_Use_Callback:
            vDeInit_Slave_CallbackConfigs(pstSlaveCtrl);
            break;
        case eTransfer_Use_MessageBus:
        default:
            FHALT("Not Implemented for this Notification Type : %d", pstSlaveCtrl->stTUserNotifyCtrl.eDataPathType);
            break;
    }

    vClear_TransferBusyFlag(pstSPIModule);    
    vSet_SlaveTransferType(eModule, eTransfer_Rx_Only);
    pstSPIModule->bIsInitialized = false;
    return true;
}

static void vDeinit_Slave_InterruptConfigurations( sT_SPIModuleConfig_t *pstSPIModule, sT_SPI_SlaveCtrl *pstSlaveCtrl)
{
    if(pstSlaveCtrl == NULL)
    {
        FHALT("Null Pointer Reference");
        return;
    }

    vDisable_SPI_IRQ(pstSPIModule->eModuleId);    
    LPSPI_SlaveTransferAbort(pstSPIModule->pstSPIDevice, &pstSlaveCtrl->stSlaveHandle);
    vClear_SlaveTransferFlag(pstSlaveCtrl);
    
    if(pstSlaveCtrl->bIsHWMatchRequested)
    {
        LPSPI_DisableInterrupts(pstSPIModule->pstSPIDevice, kLPSPI_DataMatchInterruptEnable);
        pstSlaveCtrl->bIsHWMatchRequested = false;
        vClear_HWMatchFlag(pstSlaveCtrl);        
    }

    LPSPI_DisableInterrupts(pstSPIModule->pstSPIDevice, kLPSPI_AllInterruptEnable);
    LPSPI_FlushFifo(pstSPIModule->pstSPIDevice, true, true);
    LPSPI_ClearStatusFlags(
        pstSPIModule->pstSPIDevice,
        kLPSPI_DataMatchFlag |
        kLPSPI_TransferCompleteFlag |
        kLPSPI_FrameCompleteFlag |
        kLPSPI_WordCompleteFlag |
        kLPSPI_TransmitErrorFlag |
        kLPSPI_ReceiveErrorFlag
    );    
}

static void vDeInit_Slave_CallbackConfigs(sT_SPI_SlaveCtrl *pstSlaveCtrl)
{
    if(pstSlaveCtrl == NULL)
    {
        FHALT("Null Pointer Reference");
        return;
    }

    sT_SlaveCallback_Config_t *pstSlaveCallback = &pstSlaveCtrl->stTUserNotifyCtrl.data_pathConfig.stTCallbackConfig;
    vFree_SPIRxBuffers(&pstSlaveCallback->pstRxBuffHead);
    vDeallocate_DrainBufferMemory(pstSlaveCallback);
    vSet_RxBufferType(pstSlaveCallback, eSPI_RxTarget_AppBuffer);
    vSet_RxEventType(&pstSlaveCtrl->stTDevRxOverflowControl, eSPI_PeripheralEvent_RxReady);
    pstSlaveCtrl->stTDevRxOverflowControl.uiDroppedFrameCount = 0U;
    pstSlaveCtrl->stTDevRxOverflowControl.uiOverflowCount = 0U;
}

static inline bool bIsModule_Initialized( eSPIModule_t eModule )
{
    if(eModule >= eNUMBER_OF_SPI_MODULEs)
    {
        FHALT("Invalid SPI Module : %d", eModule);
        return false;
    }
    return staSPIModule[eModule].bIsInitialized;
}

static bool bConfig_SPI_MasterMode( sT_SPIConfig_t *pstSPIConfig )
{
    sT_SPIModuleConfig_t *pstSPIModule = &staSPIModule[pstSPIConfig->eModule];
    sT_SPI_MasterCtrl *pstMasterCtrl = &pstSPIModule->stTSPIDevCtrl.st_DevCtrlMode.stTMasterCtrl;
    sT_SPISlave_Control_t *pstSPISlave = pstSPIConfig->stTSPIModeCtrl.spi_mode.pstSPISlaveHead_Ctrl;
    sT_SPISlave_Config_t *pstSlaveConfig = &pstSPISlave->stTConfigs;

    if(pstMasterCtrl->pstSPISlaveHead_Ctrl != NULL)
    {
        vRelease_SPISLaves(&pstMasterCtrl->pstSPISlaveHead_Ctrl);//Release previously allocated memory
    }

    pstSPIModule->bIsInitialized = false;
    vClear_TransferBusyFlag(pstSPIModule);
    pstSPIModule->eNotificationType = pstSPIConfig->eNotificationType;
    pstSPIModule->stTSPIDevCtrl.eMode = pstSPIConfig->stTSPIModeCtrl.eMode;
    pstMasterCtrl->eActiveSlaveId = eNUMBER_OF_SPI_SLAVEs;
    pstSPIModule->uiSPImodule_Clock_Hz = CLOCK_GetLpspiClkFreq(pstSPIConfig->eModule);

    pstSPIModule->pstSPIDevice = pstGetSPIDevice(pstSPIConfig->eModule);

    lpspi_master_config_t *pstspiMasterConf = &pstSPIModule->stTSPIDevCtrl.st_DevCtrlMode.stTMasterCtrl.stMasterConfig;
    LPSPI_MasterGetDefaultConfig(pstspiMasterConf);

    pstspiMasterConf->baudRate = pstSlaveConfig->uiSPI_Freq_Hz;
    pstspiMasterConf->bitsPerFrame = 8U;
    pstspiMasterConf->cpol = eGetDefault_CPOL(pstSlaveConfig->eCPOLCPH_Ctrl);
    pstspiMasterConf->cpha = eGetDefault_CPHA(pstSlaveConfig->eCPOLCPH_Ctrl);
    pstspiMasterConf->betweenTransferDelayInNanoSec = pstSlaveConfig->uiDelay_Between_BlockTx_ns;
    pstspiMasterConf->lastSckToPcsDelayInNanoSec = pstSlaveConfig->uiDelay_LastSCK_To_CS_Deassert_ns;
    pstspiMasterConf->dataOutConfig = eGetDataOutState_AtTxEnd(pstSPIConfig->eDataOutPinState);
    pstspiMasterConf->direction = eGetEndianType(pstSlaveConfig->eEndianFormat);
    pstspiMasterConf->enableInputDelay = pstSlaveConfig->bEn_SCKLoopBack_ForSampling;
    pstspiMasterConf->pcsToSckDelayInNanoSec = pstSlaveConfig->uiDelay_CS_Assert_To_SCK_ns;

    if(pstSPISlave->stTConfigs.bIs_CS_HWControlled)
    {
        pstspiMasterConf->pcsActiveHighOrLow = eGet_CS_PinActiveState(pstSlaveConfig->eCSPolarityType);
        pstspiMasterConf->whichPcs = eGet_SPI_HW_CSPin(pstSlaveConfig->eHW_PCS_Ctrl);
    }
    pstspiMasterConf->pcsFunc = eGet_PSCConfiguration(pstSlaveConfig->eSPI_BusWidth);
    pstspiMasterConf->pinCfg = eGet_SPIPinConfigurations(pstSPIConfig->ePinConfig);

    pstMasterCtrl->eSPI_BusWidth = pstSlaveConfig->eSPI_BusWidth;
    pstMasterCtrl->eActiveSlaveId = pstSPISlave->stTConfigs.eSlaveId;

    vAssign_SlaveDevices(pstSPIModule, pstSPIConfig);
    vAssign_PinConfigurations(pstSPIModule->eModuleId);

    LPSPI_MasterInit(pstSPIModule->pstSPIDevice, pstspiMasterConf, pstSPIModule->uiSPImodule_Clock_Hz);

    vConfig_TransferHandle(pstSPIModule);
    pstSPIModule->bIsInitialized = true;
    return true;
}

static void vAssign_PinConfigurations(eSPIModule_t eModuleId)
{
    switch(eModuleId)
    {
        case eSPI_0:
            #if defined(USE_SPI_0)
                static const struct pinctrl_dev_config *pstLPSPI0PinCfg = PINCTRL_DT_DEV_CONFIG_GET(LPSPI0_NODE);
                pinctrl_apply_state(pstLPSPI0PinCfg, PINCTRL_STATE_DEFAULT);
            #endif
            break;
        case eSPI_1:
            #if defined(USE_SPI_1)
                static const struct pinctrl_dev_config *pstLPSPI1PinCfg = PINCTRL_DT_DEV_CONFIG_GET(LPSPI1_NODE);
                pinctrl_apply_state(pstLPSPI1PinCfg, PINCTRL_STATE_DEFAULT);
            #endif
            break;
        default:
            FHALT("Invalid SPI Module : %d", eModuleId);
            break;
    }
}

static void vConfig_TransferHandle(sT_SPIModuleConfig_t *pstSPIModule)
{
    switch(pstSPIModule->eNotificationType)
    {
        case eNotify_Interrupt:
            vConfigure_SPIInterrupt(pstSPIModule);
            break;
        case eNotify_DMA:
            vConfigure_SPI_DMA(pstSPIModule);
            break;
        default:
            FHALT("Invalid Transfer Configuration : %d", pstSPIModule->eNotificationType);
            break;
    }
}

static void vConfigure_SPIInterrupt(sT_SPIModuleConfig_t *pstSPIModule)
{
    switch(pstSPIModule->stTSPIDevCtrl.eMode)
    {
        case eSPI_Mode_Controller:        
            LPSPI_MasterTransferCreateHandle(
                pstSPIModule->pstSPIDevice,
                &pstSPIModule->stTSPIDevCtrl.st_DevCtrlMode.stTMasterCtrl.stMasterHandle,
                vSPI_MasterCallback,
                pstSPIModule
            );
            vEnable_SPI_IRQ(pstSPIModule->eModuleId);
            break;
        case eSPI_Mode_Peripheral:
            LPSPI_SlaveTransferCreateHandle(
                pstSPIModule->pstSPIDevice,
                &pstSPIModule->stTSPIDevCtrl.st_DevCtrlMode.stTSlaveCtrl.stSlaveHandle,
                vSPI_PeripheralModeCallback,
                pstSPIModule
            );
            vEnable_SPI_IRQ(pstSPIModule->eModuleId);
            break;
        default:
            FHALT("Not Implemented");
            break;
    }
}

static void vEnable_SPI_IRQ(eSPIModule_t eModule)
{
    switch(eModule)
    {
        case eSPI_0:
            IRQ_CONNECT(LPSPI0_IRQn, SPI0_INTR_PRIORITY, vLPSPI0_ISR, NULL, 0);
            irq_enable(LPSPI0_IRQn);
            break;
        case eSPI_1:
            IRQ_CONNECT(LPSPI1_IRQn, SPI1_INTR_PRIORITY, vLPSPI1_ISR, NULL, 0);
            irq_enable(LPSPI1_IRQn);
            break;
        default:
            FHALT("Invalid Module : %d", eModule);
            break;
    }
}

static void vDisable_SPI_IRQ(eSPIModule_t eModule)
{
    switch(eModule)
    {
        case eSPI_0:
            irq_disable(LPSPI0_IRQn);
            break;
        case eSPI_1:
            irq_disable(LPSPI1_IRQn);
            break;
        default:
            FHALT("Invalid SPI Module : %d", eModule);
            break;
    }
}

static void vLPSPI0_ISR(const void *arg)
{
    ARG_UNUSED(arg);
    vHandle_SPIPeripheral_HWMatch(eSPI_0);
    LPSPI_DriverIRQHandler(0U);
}

static void vLPSPI1_ISR(const void *arg)
{
    ARG_UNUSED(arg);
    vHandle_SPIPeripheral_HWMatch(eSPI_1);
    LPSPI_DriverIRQHandler(1U);
}

static inline void vHandle_SPIPeripheral_HWMatch(eSPIModule_t eModuleId)
{
    sT_SPIModuleConfig_t *pstSPIModule = &staSPIModule[eModuleId];
    if(pstSPIModule->stTSPIDevCtrl.eMode != eSPI_Mode_Peripheral)
        return;

    sT_SPI_SlaveCtrl *pstPeriphreralConfig = &pstSPIModule->stTSPIDevCtrl.st_DevCtrlMode.stTSlaveCtrl;
    if(!pstPeriphreralConfig->bIsHWMatchRequested)
        return;
    
    uint32_t uiFlags = LPSPI_GetStatusFlags(pstSPIModule->pstSPIDevice);
    if(!(uiFlags & kLPSPI_DataMatchFlag))
        return;
    
    LPSPI_ClearStatusFlags(pstSPIModule->pstSPIDevice, kLPSPI_DataMatchFlag);
    vSet_HWMatchFlag(pstPeriphreralConfig);

    //To Disable unnecessary execution of HW match, Interrupts are disabled
    LPSPI_DisableInterrupts(
        pstSPIModule->pstSPIDevice,
        kLPSPI_DataMatchInterruptEnable
    );

    LPSPI_ClearStatusFlags(
        pstSPIModule->pstSPIDevice,
        kLPSPI_DataMatchFlag
    );
    //printf("HW Match\n\r");
}

static void vSPI_PeripheralModeCallback(LPSPI_Type *base,
                                        lpspi_slave_handle_t *handle,
                                        status_t status,
                                        void *userData)
{
    sT_SPIModuleConfig_t *pstSPIModule = (sT_SPIModuleConfig_t *)userData;
    sT_SPI_SlaveCtrl *pstPeriphreralConfig = &pstSPIModule->stTSPIDevCtrl.st_DevCtrlMode.stTSlaveCtrl;
    eTransfer_Type_t eType = eGet_SlaveTransferType(pstSPIModule->eModuleId);
    
    vClear_SlaveTransferFlag(pstPeriphreralConfig);
    vSet_SPISlave_HWReadyState(pstSPIModule->eModuleId, eSPISlave_Busy);

    eSPI_TransferResult_t eResult = (status == kStatus_Success) ? eTransfer_Success : eTransfer_Failed;
    if(status != kStatus_Success)
    {
#ifdef DEBUG_SPI_SLAVE_IRQ
        uint32_t uiSr = base->SR;
        debug_SlaveIRQ_Print("Slave IRQ error: status=%d SR=0x%08x TEF=%u REF=%u MBF=%u TXFIFO=%u RXFIFO=%u txRem=%u rxRem=%u wrRem=%u rdRem=%u errCnt=%u TCR=0x%08x CFGR1=0x%08x\n\r",
                                (int)status,
                                (unsigned int)uiSr,
                                (unsigned int)((uiSr & kLPSPI_TransmitErrorFlag) != 0U),
                                (unsigned int)((uiSr & kLPSPI_ReceiveErrorFlag) != 0U),
                                (unsigned int)((uiSr & kLPSPI_ModuleBusyFlag) != 0U),
                                (unsigned int)LPSPI_GetTxFifoCount(base),
                                (unsigned int)LPSPI_GetRxFifoCount(base),
                                (unsigned int)handle->txRemainingByteCount,
                                (unsigned int)handle->rxRemainingByteCount,
                                (unsigned int)handle->writeRegRemainingTimes,
                                (unsigned int)handle->readRegRemainingTimes,
                                (unsigned int)handle->errorCount,
                                (unsigned int)LPSPI_GetTcr(base),
                                (unsigned int)base->CFGR1);
#endif
        vRecover_SlaveHW_AfterError(base);
    }

    if(eType == eTransfer_Tx_Only)
    {
        vNotify_SlaveTxComplete(pstSPIModule, pstPeriphreralConfig, eResult);
        return;    
    }

    if(pstPeriphreralConfig->bIsHWMatchRequested && !bGet_HWMatchFlag(pstPeriphreralConfig))
    {
        bArm_SlaveRx_ForCallback(pstSPIModule, pstPeriphreralConfig);
        return;
    }
    
    vReConfigure_HWMatch_Interrupts(pstSPIModule, pstPeriphreralConfig);

    switch(pstPeriphreralConfig->stTUserNotifyCtrl.eDataPathType)
    {
        case eTransfer_Use_Callback:
            vNotify_Via_Callback(pstSPIModule->eModuleId,
                                 &pstPeriphreralConfig->stTUserNotifyCtrl.data_pathConfig.stTCallbackConfig,
                                 handle, eResult);
            break;
        case eTransfer_Use_MessageBus:
        default:
            break;
    }
    
}

static inline void vReConfigure_HWMatch_Interrupts(sT_SPIModuleConfig_t *pstSPIModule, sT_SPI_SlaveCtrl *pstPeriphreralConfig)
{    
    if(!pstPeriphreralConfig->bIsHWMatchRequested)
        return;
    if(pstPeriphreralConfig == NULL || pstSPIModule == NULL)
        return;

    vClear_HWMatchFlag(pstPeriphreralConfig);

    LPSPI_ClearStatusFlags(
        pstSPIModule->pstSPIDevice,
        kLPSPI_DataMatchFlag
    );

    LPSPI_EnableInterrupts(
        pstSPIModule->pstSPIDevice,
        kLPSPI_DataMatchInterruptEnable
    );        
}

static void vNotify_SlaveTxComplete(sT_SPIModuleConfig_t *pstSPIModule, sT_SPI_SlaveCtrl *pstSlaveCtrl, 
                                    eSPI_TransferResult_t eResult)
{    
    if(pstSPIModule == NULL || pstSlaveCtrl == NULL)
    {
        return;
    }

    if(!pstSlaveCtrl->bIsTxOnlyNotification_Requested)
    {
        bArm_SlaveRx_ForCallback(pstSPIModule, pstSlaveCtrl);
        return;
    }

    sT_SlaveCallback_Config_t *pstCallbackConf = &pstSlaveCtrl->stTUserNotifyCtrl.data_pathConfig.stTCallbackConfig;
    if(pstCallbackConf->pvPeripheral_UserCallBack == NULL)
    {
        bArm_SlaveRx_ForCallback(pstSPIModule, pstSlaveCtrl);
        return;
    }

    sT_RxBuffData_t stTBuffData = {
        .uiBuffId = SPI_SLAVE_RxCallBack_InvalidBuffId,
        .eState = eBuffer_None,
        .puiBuffer = NULL,
        .uisize = 0U,
        .bTxCompleted = (eResult == eTransfer_Success)?true:false
    };

    eSPI_PeripheralEvent_Type_t eEvent = (eResult == eTransfer_Success)? eSPI_PeripheralEvent_TxCompleted: eSPI_PeripheralEvent_TxError;
    bool bArmResult = bArm_SlaveRx_ForCallback(pstSPIModule, pstSlaveCtrl);
    
    pstCallbackConf->pvPeripheral_UserCallBack(eEvent, eResult, stTBuffData, bArmResult);
}

static inline void vNotify_Via_Callback(eSPIModule_t eModuleId, 
                                        sT_SlaveCallback_Config_t *pstCallbackConfig, 
                                        lpspi_slave_handle_t *pshandle,
                                        eSPI_TransferResult_t eResult)
{
    
    sT_SPIModuleConfig_t *pstSPIModule = &staSPIModule[eModuleId];
    sT_SPI_SlaveCtrl *pstSlaveControl = &pstSPIModule->stTSPIDevCtrl.st_DevCtrlMode.stTSlaveCtrl;

    if(pstCallbackConfig->pvPeripheral_UserCallBack != NULL)
    {
        eSPI_RxTarget_t eBuffType = eGet_RxBufferType(pstCallbackConfig);

        switch(eBuffType)
        {
            case eSPI_RxTarget_AppBuffer:
                vNotify_Application_AtNoError(pstSPIModule, pstSlaveControl, pstCallbackConfig, eResult);
                break;
            case eSPI_RxTarget_DrainBuffer:
                vNotify_Application_AtOverflow(pstSPIModule, pstSlaveControl, pstCallbackConfig, eResult);
                break;
            default:
                FHALT("Invalid Operation on Rx Buffers");
                return;
        }
    }
    else
    {
        bArm_SlaveRx_ForCallback(pstSPIModule, pstSlaveControl);
    }

}

static void vNotify_Application_AtNoError(sT_SPIModuleConfig_t *pstSPIModule,
                                          sT_SPI_SlaveCtrl *pstSlaveControl,
                                          sT_SlaveCallback_Config_t *pstCallbackConfig,
                                          eSPI_TransferResult_t eResult)
{
    eSPI_PeripheralEvent_Type_t eEventType = eGetCurrentRxEventType(&pstSlaveControl->stTDevRxOverflowControl);
    uint8_t uiBuffId = uiGet_RxBuffId(pstCallbackConfig);
    bool bArmResult = false;

    if(eResult != eTransfer_Success)
    {
        vSet_RxBufferState(uiBuffId, eBuffer_Free, pstCallbackConfig->pstRxBuffHead);

        sT_RxBuffData_t stTBuffData = {
            .uiBuffId = SPI_SLAVE_RxCallBack_InvalidBuffId,
            .eState = eBuffer_Error,
            .puiBuffer = NULL,
            .uisize = 0U
        };

        bArmResult = bArm_SlaveRx_ForCallback(pstSPIModule, pstSlaveControl);
        pstCallbackConfig->pvPeripheral_UserCallBack(eSPI_PeripheralEvent_RxError,
                                                     eResult, stTBuffData, bArmResult);
        return;
    }

    vSet_RxBufferState(uiBuffId, eBuffer_Ready, pstCallbackConfig->pstRxBuffHead);
    sT_SPIRxBuff_t *pstReadBuff = pstGet_RxReadyBuffer_byId(pstCallbackConfig->pstRxBuffHead, uiBuffId);
    if(pstReadBuff == NULL)
        return;

    vSet_RxEventType(&pstSlaveControl->stTDevRxOverflowControl,eSPI_PeripheralEvent_RxReady);

    sT_RxBuffData_t stTBuffData = {
        .uiBuffId = uiBuffId,
        .eState = eBuffer_Ready,
        .puiBuffer = pstReadBuff->puiBuffer,
        .uisize = pstReadBuff->uisize
    };

    pstCallbackConfig->pvPeripheral_UserCallBack(eEventType,
                                                 eResult, stTBuffData, bArmResult);
    if(!bIsSlaveTransferBusy(pstSlaveControl))
    {
        bArm_SlaveRx_ForCallback(pstSPIModule, pstSlaveControl);
    }
}

static void vNotify_Application_AtOverflow(sT_SPIModuleConfig_t *pstSPIModule, 
                                           sT_SPI_SlaveCtrl *pstSlaveControl,
                                           sT_SlaveCallback_Config_t *pstCallbackConfig,
                                           eSPI_TransferResult_t eResult)
{
    eSPI_PeripheralEvent_Type_t eEventType = eGetCurrentRxEventType(&pstSlaveControl->stTDevRxOverflowControl);
    pstSlaveControl->stTDevRxOverflowControl.uiDroppedFrameCount++;

    sT_RxBuffData_t stTBuffData = {
        .uiBuffId = SPI_SLAVE_RxCallBack_InvalidBuffId,
        .eState = eBuffer_Overflow,
        .puiBuffer = pstSlaveControl->stTUserNotifyCtrl.data_pathConfig.stTCallbackConfig.puiDrainBuffer,
        .uisize = pstSlaveControl->stTUserNotifyCtrl.data_pathConfig.stTCallbackConfig.uiBuffSize
    };

    bool bArmRes = bArm_SlaveRx_ForCallback(pstSPIModule, pstSlaveControl);

    if(pstCallbackConfig->pvPeripheral_UserCallBack != NULL)
    {
        pstCallbackConfig->pvPeripheral_UserCallBack(eEventType,
                                                     eTransfer_Failed, stTBuffData, bArmRes);
    }
}

static inline void vSet_RxBufferType(sT_SlaveCallback_Config_t *pstCallbackConfig, eSPI_RxTarget_t eNewTargetType)
{
    if(pstCallbackConfig == NULL)
        return;
    atomic_store_explicit(&pstCallbackConfig->eActiveRxTarget, eNewTargetType, memory_order_release);
}

static inline eSPI_RxTarget_t eGet_RxBufferType(sT_SlaveCallback_Config_t *pstCallbackConfig)
{
    if(pstCallbackConfig == NULL)
        return eNUMBER_OF_RXTARGET_BUFFERTYPEs;
    eSPI_RxTarget_t eBuffType = atomic_load_explicit(&pstCallbackConfig->eActiveRxTarget, memory_order_acquire);
    return eBuffType;    
}

static inline eSPI_PeripheralEvent_Type_t eGetCurrentRxEventType(sT_SPI_RxOverflowCtrl_t *pstRxFlowCtrl)
{
    eSPI_PeripheralEvent_Type_t eType = atomic_load_explicit(&pstRxFlowCtrl->eEventType, memory_order_acquire);
    return eType; 
}

static inline void vSet_RxEventType(sT_SPI_RxOverflowCtrl_t *pstRxFlowCtrl, eSPI_PeripheralEvent_Type_t eType)
{
    if(pstRxFlowCtrl == NULL)
        return;
    atomic_store_explicit(&pstRxFlowCtrl->eEventType, eType, memory_order_release);
}

bool bSPI_ReleasePeripheralMode_RxBuffer( eSPIModule_t eModuleId, uint8_t uiBuffId )
{
    if(eModuleId >= eNUMBER_OF_SPI_MODULEs)
    {
        FHALT("Invalid SPIModule : %d", eModuleId);
        return false;
    }
    if(eGetSPIMode(eModuleId) != eSPI_Mode_Peripheral)
    {
        FHALT("SPIMod[%d] is not in Slave Configuration", eModuleId);
        return false;        
    }

    sT_SPIModuleConfig_t *pstSPIModule = &staSPIModule[eModuleId];
    sT_SPI_SlaveCtrl *pstSlaveControl = &pstSPIModule->stTSPIDevCtrl.st_DevCtrlMode.stTSlaveCtrl;

    if(pstSlaveControl->stTUserNotifyCtrl.eDataPathType != eTransfer_Use_Callback)
    {
        FHALT("SPIMod[%d] is not in Callback Notification Mode", eModuleId);
        return false;
    }

    sT_SlaveCallback_Config_t *pstCallbackConfig = &pstSlaveControl->stTUserNotifyCtrl.data_pathConfig.stTCallbackConfig;
    sT_SPIRxBuff_t *pstRxBuff = pstGet_RxReadyBuffer_byId(pstCallbackConfig->pstRxBuffHead, uiBuffId);
    if(pstRxBuff == NULL)
    {
        FHALT("Invalid or non-ready SPI Rx Buffer Id: %d", uiBuffId);
        return false;
    }

    vSet_RxBufferState(uiBuffId, eBuffer_Free, pstCallbackConfig->pstRxBuffHead);
    return true;
}

static inline void vSet_SlaveTransferType(eSPIModule_t eModuleId, eTransfer_Type_t eTransferType)
{
    if(eModuleId >= eNUMBER_OF_SPI_MODULEs)
    {
        FHALT("Invalid SPI Module: %d", eModuleId);
        return;        
    }
    if(eGetSPIMode(eModuleId) != eSPI_Mode_Peripheral)
    {
        FHALT("Invalid Call to set transfer type when SPI is in MasterMode");
        return;
    }

    sT_SPI_SlaveCtrl *pstSlaveCtrl = &staSPIModule[eModuleId].stTSPIDevCtrl.st_DevCtrlMode.stTSlaveCtrl;
    atomic_store_explicit(&pstSlaveCtrl->eTransferType, eTransferType, memory_order_release);
}

static inline eTransfer_Type_t eGet_SlaveTransferType(eSPIModule_t eModuleId)
{
    if(eModuleId >= eNUMBER_OF_SPI_MODULEs)
    {
        FHALT("Invalid SPI Module: %d", eModuleId);
        return eNUMBER_OF_TRANSFER_TYPEs;        
    }

    sT_SPI_SlaveCtrl *pstSlaveCtrl = &staSPIModule[eModuleId].stTSPIDevCtrl.st_DevCtrlMode.stTSlaveCtrl;
    eTransfer_Type_t eType = atomic_load_explicit(&pstSlaveCtrl->eTransferType, memory_order_acquire);
    return eType;
}

static inline void vSet_RxBuffId(sT_SlaveCallback_Config_t *pstCallbackConfig, uint8_t uiId)
{
    atomic_store_explicit(&pstCallbackConfig->uiBuffIndex, uiId, memory_order_release);
}

static inline uint8_t uiGet_RxBuffId(sT_SlaveCallback_Config_t *pstCallbackConfig)
{
    uint8_t uiId = atomic_load_explicit(&pstCallbackConfig->uiBuffIndex, memory_order_acquire);
    return uiId;
}

static inline void vSet_HWMatchFlag(sT_SPI_SlaveCtrl *pstPeriphreralConfig)
{
    if(pstPeriphreralConfig == NULL)
        return;
    atomic_store_explicit(&pstPeriphreralConfig->bIsMatchOccurred, true, memory_order_release);
}

static inline bool bGet_HWMatchFlag(sT_SPI_SlaveCtrl *pstPeriphreralConfig)
{
    bool bIsSet = atomic_load_explicit(&pstPeriphreralConfig->bIsMatchOccurred, memory_order_acquire);
    return bIsSet;
}

static inline void vClear_HWMatchFlag(sT_SPI_SlaveCtrl *pstPeriphreralConfig)
{
    atomic_store_explicit(&pstPeriphreralConfig->bIsMatchOccurred, false, memory_order_release);
}

static void vSPI_MasterCallback(LPSPI_Type *base,
                                lpspi_master_handle_t *handle,
                                status_t status,
                                void *userData)
{
    sT_SPIModuleConfig_t *pstSPIModule = (sT_SPIModuleConfig_t *)userData;
    sT_SPI_MasterCtrl *pstMasterCtrl = &pstSPIModule->stTSPIDevCtrl.st_DevCtrlMode.stTMasterCtrl;

    sT_SPISlave_Control_t *pstSlave = pstGetSlaveInfo(pstMasterCtrl->eActiveSlaveId, pstMasterCtrl->pstSPISlaveHead_Ctrl);
    if(!bTryComplete_TransferBusyFlag(pstSPIModule))
        return;
        
    k_work_cancel_delayable(&pstSPIModule->kw_SPITransferMonitor);

    if(pstSlave == NULL || pstSlave->stTConfigs.pvSPI_CallBack == NULL)
        return;
    
    eSPI_TransferResult_t eResult = (status == kStatus_Success) ? eTransfer_Success : eTransfer_Failed;
    pstSlave->stTConfigs.pvSPI_CallBack(pstMasterCtrl->eActiveSlaveId, eResult);
}

static inline bool bTryComplete_TransferBusyFlag(sT_SPIModuleConfig_t *pstSPIModule)
{
    if(pstSPIModule == NULL)
    {
        FHALT("Null Pointer reference");
        return false;
    }

    return atomic_exchange_explicit(&pstSPIModule->bIsTransferBusy, false, memory_order_acq_rel);
}

static void vSPI_FaultRecoveryHandler( struct k_work *work )
{
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    sT_SPIModuleConfig_t *pstSPIModule = pstGetSPIModule_From_KWork(dwork);
    if(pstSPIModule == NULL)
        return;

    eSPIModule_t eModuleId = pstSPIModule->eModuleId;

    if(!pstSPIModule->bIsInitialized || 
       !bTryComplete_TransferBusyFlag(pstSPIModule) ||
       eGetSPIMode(eModuleId) != eSPI_Mode_Controller)
       return;
       
    sT_SPI_MasterCtrl *pstMasterCtrl = &pstSPIModule->stTSPIDevCtrl.st_DevCtrlMode.stTMasterCtrl;       
    LPSPI_MasterTransferAbort(pstSPIModule->pstSPIDevice, &pstMasterCtrl->stMasterHandle);

    sT_SPISlave_Control_t *pstSlave = pstGetSlaveInfo(pstMasterCtrl->eActiveSlaveId, pstMasterCtrl->pstSPISlaveHead_Ctrl);
    if(pstSlave == NULL || pstSlave->stTConfigs.pvSPI_CallBack == NULL)
        return;

    pstSlave->stTConfigs.pvSPI_CallBack(pstMasterCtrl->eActiveSlaveId, eTransfer_Timeout);
}

static inline sT_SPIModuleConfig_t *pstGetSPIModule_From_KWork(struct k_work_delayable *work)
{
    for(uint8_t i = eSPI_0; i < eNUMBER_OF_SPI_MODULEs; i++)
    {
        if(&staSPIModule[i].kw_SPITransferMonitor == work)
            return &staSPIModule[i];
    }

    return NULL;
}

static inline eSPI_Mode_t eGetSPIMode( eSPIModule_t eModule )
{
    if(eModule >= eNUMBER_OF_SPI_MODULEs)
    {
        FHALT("Invalid SPI Module : %d", eModule);
        return eNUMBER_OF_SPI_MODEs;
    }
    return staSPIModule[eModule].stTSPIDevCtrl.eMode;
}

bool bSPI_Transfer_InMasterMode(sT_SPIMasterTransfer_t stTTransfer)
{
    bool bResult = false;
    uint32_t uiPCSFlag = 0;

    eSPIModule_t eModuleId = stTTransfer.eModuleId;
    if(!bIsModule_Initialized(eModuleId))
    {
        FHALT("Module[%d] not initialized", eModuleId);
        return false;
    }
    if(eGetSPIMode(eModuleId) != eSPI_Mode_Controller)
    {
        FHALT("Module[%d] not in Master Mode", eModuleId);
        return false;
    }
    if(bTryClaim_TransferBusyFlag(eModuleId))
    {
        FHALT("Module[%d] Transfer in Progress", eModuleId);
        return false;
    }    
    
    sT_SPIModuleConfig_t *pstModule = &staSPIModule[eModuleId];
    sT_SPI_MasterCtrl *pstMasterCtrl = &pstModule->stTSPIDevCtrl.st_DevCtrlMode.stTMasterCtrl;
    if((pstMasterCtrl->eSPI_BusWidth == e2bit_Transfer || pstMasterCtrl->eSPI_BusWidth == e4bit_Transfer) &&
        stTTransfer.eType == eTransfer_Transceive)
    {
        vClear_TransferBusyFlag(pstModule);
        FHALT("Full duplex transfer cannot be executed while Module[%d] with Data Width: %d", eModuleId, pstMasterCtrl->eSPI_BusWidth);
        return false;        
    }

    if(pstMasterCtrl->eActiveSlaveId != stTTransfer.eSlaveId)
    {
        LPSPI_Enable(pstModule->pstSPIDevice, false);
        bResult = bSetUp_NewSlaveConfig(eModuleId, pstMasterCtrl, stTTransfer.eSlaveId);
        if(!bResult)
        {
            vClear_TransferBusyFlag(pstModule);
            LPSPI_Enable(pstModule->pstSPIDevice, true);
            return false;
        }
        LPSPI_Enable(pstModule->pstSPIDevice, true);
    }

    lpspi_transfer_t stLPSPITransfer = {0};

    switch(stTTransfer.eType)
    {
        case eTransfer_Tx_Only:
            if(!bSPI_Master_Send(&stTTransfer, &stLPSPITransfer))
            {
                vClear_TransferBusyFlag(pstModule);
                return false;
            }
            break;
        case eTransfer_Rx_Only:
            if(!bSPI_Master_Receive(&stTTransfer, &stLPSPITransfer))
            {
                vClear_TransferBusyFlag(pstModule);
                return false;
            }
            break;
        case eTransfer_Transceive:
            if(!bSPI_Master_Transceive(&stTTransfer, &stLPSPITransfer))
            {
                vClear_TransferBusyFlag(pstModule);
                return false;
            }
            break;
        default:
            vClear_TransferBusyFlag(pstModule);
            FHALT("Invalid Transfer Type: %d", stTTransfer.eType);
            return false;
    }

    sT_SPISlave_Control_t *pstCurrentSlaveInfo = pstGetSlaveInfo(pstMasterCtrl->eActiveSlaveId, 
                                                                 pstMasterCtrl->pstSPISlaveHead_Ctrl);
    if(pstCurrentSlaveInfo == NULL)
    {
        FHALT("Invalid Slave Info");
        vClear_TransferBusyFlag(pstModule);
        return false;
    }
    if(pstCurrentSlaveInfo->stTConfigs.bIs_CS_HWControlled)
    {
        uiPCSFlag = uiGetTransferPCsFlag(pstCurrentSlaveInfo->stTConfigs.eHW_PCS_Ctrl);
        if(uiPCSFlag == 0xFFFFFFFF)
        {
            FHALT("Invalid PCS Pin");
            vClear_TransferBusyFlag(pstModule);
            return false;
        }
        stLPSPITransfer.configFlags = uiPCSFlag | 
                                      (stTTransfer.bShould_CS_Asserted_For_EntireTransfer ? kLPSPI_MasterPcsContinuous : 0U);
    }

    status_t status = LPSPI_MasterTransferNonBlocking(pstModule->pstSPIDevice,
                                                      &pstMasterCtrl->stMasterHandle,
                                                      &stLPSPITransfer);
    if(status != kStatus_Success)
    {
        FHALT("Transfer Failed with Status: %d", status);
        vClear_TransferBusyFlag(pstModule);
        return false;
    }

    k_work_schedule(&pstModule->kw_SPITransferMonitor, K_MSEC(SPI_MASTER_TRANSFER_TIMEOUT_ms));
    return true;
}

static uint32_t uiGetTransferPCsFlag(eSPI_PCS_t eHW_PCS_Ctrl)
{
    switch(eHW_PCS_Ctrl)
    {
        case ePCS_0:
            return kLPSPI_MasterPcs0;
        case ePCS_1:
            return kLPSPI_MasterPcs1;
        case ePCS_2:
            return kLPSPI_MasterPcs2;
        case ePCS_3:
            return kLPSPI_MasterPcs3;
        default:
            FHALT("Invalid HW PCS Pin: %d", eHW_PCS_Ctrl);
            return 0xFFFFFFFF;
    }
}

static inline bool bSPI_Master_Send(sT_SPIMasterTransfer_t *pstTTransfer, lpspi_transfer_t *pstLPSPITransfer)
{
    if(pstTTransfer->puiTxData == NULL || pstTTransfer->uiTxDataLen == 0)
    {
        FHALT("Invalid Transfer Data");
        return false;
    }

    pstLPSPITransfer->txData = pstTTransfer->puiTxData;
    pstLPSPITransfer->rxData = NULL;
    pstLPSPITransfer->dataSize = pstTTransfer->uiTxDataLen;
    return true;
}

static inline bool bSPI_Master_Receive(sT_SPIMasterTransfer_t *pstTTransfer, lpspi_transfer_t *pstLPSPITransfer)
{
    if(pstTTransfer->puiRxData == NULL || pstTTransfer->uiRxDataLen == 0)
    {
        FHALT("Invalid Transfer Data");
        return false;
    }
    
    pstLPSPITransfer->txData = NULL;
    pstLPSPITransfer->rxData = pstTTransfer->puiRxData;
    pstLPSPITransfer->dataSize = pstTTransfer->uiRxDataLen;
    return true;
}

static inline bool bSPI_Master_Transceive(sT_SPIMasterTransfer_t *pstTTransfer, lpspi_transfer_t *pstLPSPITransfer)
{
    if(pstTTransfer->puiTxData == NULL || pstTTransfer->uiTxDataLen == 0)
    {
        FHALT("Invalid Transfer Data");
        return false;
    }

    if(pstTTransfer->puiRxData == NULL || pstTTransfer->uiRxDataLen == 0)
    {
        FHALT("Invalid Transfer Data");
        return false;
    }

    if(pstTTransfer->uiTxDataLen != pstTTransfer->uiRxDataLen)
    {
        FHALT("Tx and Rx Data Length Mismatch");
        return false;
    }

    pstLPSPITransfer->txData = pstTTransfer->puiTxData;
    pstLPSPITransfer->rxData = pstTTransfer->puiRxData;
    pstLPSPITransfer->dataSize = pstTTransfer->uiTxDataLen;
    return true;
}

static bool bSetUp_NewSlaveConfig(eSPIModule_t eModuleId, sT_SPI_MasterCtrl *pstMasterCtrl, eSPI_Slave_Id_t eNewSlaveId)
{
    bool bResult = false;

    sT_SPISlave_Control_t *pstNewSlaveInfo = pstGetSlaveInfo(eNewSlaveId, pstMasterCtrl->pstSPISlaveHead_Ctrl);
    if(pstNewSlaveInfo == NULL)
    {
        return false;
    }

    sT_SPISlave_Control_t *pstCurrentSlaveInfo = pstGetSlaveInfo(pstMasterCtrl->eActiveSlaveId, 
                                                                 pstMasterCtrl->pstSPISlaveHead_Ctrl);
    
    sT_SPISlave_Config_t *pstCurConf = (pstCurrentSlaveInfo != NULL)? &pstCurrentSlaveInfo->stTConfigs : NULL;
    sT_SPISlave_Config_t *pstNewConf = &pstNewSlaveInfo->stTConfigs;
    
    bResult = bSPI_Assign_NewFrequency(eModuleId, pstMasterCtrl, pstCurConf, pstNewConf);
    if(!bResult) return bResult;

    bResult = bSPI_Assign_SamplingEdge(eModuleId, pstMasterCtrl, pstCurConf, pstNewConf);
    if(!bResult) return bResult;

    bResult = bSPI_Assign_CSPolarity(eModuleId, pstMasterCtrl, pstNewConf);
    if(!bResult) return bResult;

    bResult = bSPI_Assign_CS_To_SCK_DelayTime(eModuleId, pstMasterCtrl, pstCurConf, pstNewConf);
    if(!bResult) return bResult;

    bResult = bSPI_Assign_SCK_To_CS_DelayTime(eModuleId, pstMasterCtrl, pstCurConf, pstNewConf);
    if(!bResult) return bResult;

    bResult = bSPI_Assign_Block_To_Block_DelayTime(eModuleId, pstMasterCtrl, pstCurConf, pstNewConf);
    if(!bResult) return bResult;

    pstMasterCtrl->eActiveSlaveId = eNewSlaveId;
    return true;
}

static inline bool bSPI_Assign_Block_To_Block_DelayTime(eSPIModule_t eModuleId,
                                                        sT_SPI_MasterCtrl *pstMasterCtrl, 
                                                        sT_SPISlave_Config_t *pstCurConf, 
                                                        sT_SPISlave_Config_t *pstNewConf)
{
    if(pstCurConf != NULL && pstCurConf->uiDelay_Between_BlockTx_ns == pstNewConf->uiDelay_Between_BlockTx_ns)
        return true;
    
    LPSPI_Type *pstSPIDevice = pstGetSPIDevice(eModuleId);
    sT_SPIModuleConfig_t *pstSPIModule = &staSPIModule[eModuleId];
    
    uint32_t uiDelay =LPSPI_MasterSetDelayTimes(pstSPIDevice,
                              pstNewConf->uiDelay_Between_BlockTx_ns,
                              kLPSPI_BetweenTransfer,
                              pstSPIModule->uiSPImodule_Clock_Hz);
    pstNewConf->uiDelay_Between_BlockTx_ns = uiDelay;
    return true;
}


static inline bool bSPI_Assign_CS_To_SCK_DelayTime(eSPIModule_t eModuleId,
                                                   sT_SPI_MasterCtrl *pstMasterCtrl, 
                                                   sT_SPISlave_Config_t *pstCurConf, 
                                                   sT_SPISlave_Config_t *pstNewConf)
{
    if(pstCurConf != NULL && pstCurConf->uiDelay_CS_Assert_To_SCK_ns == pstNewConf->uiDelay_CS_Assert_To_SCK_ns)
        return true;
    
    LPSPI_Type *pstSPIDevice = pstGetSPIDevice(eModuleId);
    sT_SPIModuleConfig_t *pstSPIModule = &staSPIModule[eModuleId];
    
    uint32_t uiDelay = LPSPI_MasterSetDelayTimes(pstSPIDevice,
                              pstNewConf->uiDelay_CS_Assert_To_SCK_ns,
                              kLPSPI_PcsToSck,
                              pstSPIModule->uiSPImodule_Clock_Hz);
    pstNewConf->uiDelay_CS_Assert_To_SCK_ns = uiDelay;
    return true;
}

static inline bool bSPI_Assign_SCK_To_CS_DelayTime(eSPIModule_t eModuleId,
                                                   sT_SPI_MasterCtrl *pstMasterCtrl, 
                                                   sT_SPISlave_Config_t *pstCurConf, 
                                                   sT_SPISlave_Config_t *pstNewConf)
{
    if(pstCurConf != NULL && pstCurConf->uiDelay_LastSCK_To_CS_Deassert_ns == pstNewConf->uiDelay_LastSCK_To_CS_Deassert_ns)
        return true;
    
    LPSPI_Type *pstSPIDevice = pstGetSPIDevice(eModuleId);
    sT_SPIModuleConfig_t *pstSPIModule = &staSPIModule[eModuleId];
    
    uint32_t uiDelay = LPSPI_MasterSetDelayTimes(pstSPIDevice,
                              pstNewConf->uiDelay_LastSCK_To_CS_Deassert_ns,
                              kLPSPI_LastSckToPcs,
                              pstSPIModule->uiSPImodule_Clock_Hz);
    pstNewConf->uiDelay_LastSCK_To_CS_Deassert_ns = uiDelay;
    return true;
}

static bool bSPI_Assign_CSPolarity(eSPIModule_t eModuleId,
                                   sT_SPI_MasterCtrl *pstMasterCtrl, 
                                   sT_SPISlave_Config_t *pstNewConf)
{
    if(!pstNewConf->bIs_CS_HWControlled)
    {
        return true;
    }

    LPSPI_Type *pstSPIDevice = pstGetSPIDevice(eModuleId);
    uint32_t uiPCSBit = 0U;

    switch(pstNewConf->eHW_PCS_Ctrl)
    {
        case ePCS_0:
            uiPCSBit = (uint32_t)(1U << (LPSPI_CFGR1_PCSPOL_SHIFT + 0U));
            break;
        case ePCS_1:
            uiPCSBit = (uint32_t)(1U << (LPSPI_CFGR1_PCSPOL_SHIFT + 1U));
            break;
        case ePCS_2:
            uiPCSBit = (uint32_t)(1U << (LPSPI_CFGR1_PCSPOL_SHIFT + 2U));
            break;
        case ePCS_3:
            uiPCSBit = (uint32_t)(1U << (LPSPI_CFGR1_PCSPOL_SHIFT + 3U));
            break;
        default:
            FHALT("Invalid HW PCS Pin: %d", pstNewConf->eHW_PCS_Ctrl);
            return false;
    }

    if(pstNewConf->eCSPolarityType == eCS_Active_Low)
    {
        pstSPIDevice->CFGR1 &= ~uiPCSBit;
    }
    else if(pstNewConf->eCSPolarityType == eCS_Active_High)
    {
        pstSPIDevice->CFGR1 |= uiPCSBit;
    }
    else
    {
        FHALT("Invalid CS Polarity Type: %d", pstNewConf->eCSPolarityType);
        return false;
    }

    pstSPIDevice->TCR = (LPSPI_GetTcr(pstSPIDevice) & ~LPSPI_TCR_PCS_MASK) | LPSPI_TCR_PCS((uint32_t)pstNewConf->eHW_PCS_Ctrl);

    return true;
}

static bool bSPI_Assign_SamplingEdge(eSPIModule_t eModuleId,
                                     sT_SPI_MasterCtrl *pstMasterCtrl, 
                                     sT_SPISlave_Config_t *pstCurConf, 
                                     sT_SPISlave_Config_t *pstNewConf)
{
    if(pstCurConf != NULL && pstCurConf->eCPOLCPH_Ctrl == pstNewConf->eCPOLCPH_Ctrl)
        return true;

    LPSPI_Type *pstSPIDevice = pstGetSPIDevice(eModuleId);
    sT_SPIModuleConfig_t *pstSPIModule = &staSPIModule[eModuleId];

    uint32_t uiTCR = LPSPI_GetTcr(pstSPIDevice);
    uiTCR &= ~(LPSPI_TCR_CPOL_MASK | LPSPI_TCR_CPHA_MASK);

    uiTCR |= LPSPI_TCR_CPOL(eGetDefault_CPOL(pstNewConf->eCPOLCPH_Ctrl));
    uiTCR |= LPSPI_TCR_CPHA(eGetDefault_CPHA(pstNewConf->eCPOLCPH_Ctrl));
    pstSPIModule->pstSPIDevice->TCR = uiTCR;
    return true;
}

static bool bSPI_Assign_NewFrequency(eSPIModule_t eModuleId,
                                     sT_SPI_MasterCtrl *pstMasterCtrl, 
                                     sT_SPISlave_Config_t *pstCurConf, 
                                     sT_SPISlave_Config_t *pstNewConf)
{    
    if(pstCurConf != NULL && pstCurConf->uiSPI_Freq_Hz == pstNewConf->uiSPI_Freq_Hz)
        return true;
    
    LPSPI_Type *pstSPIDevice = pstGetSPIDevice(eModuleId);
    sT_SPIModuleConfig_t *pstSPIModule = &staSPIModule[eModuleId];
    uint32_t uiPreScaleValue = 0U;

    uint32_t uiSetFrequency = LPSPI_MasterSetBaudRate(pstSPIDevice,
                                                      pstNewConf->uiSPI_Freq_Hz,
                                                      pstSPIModule->uiSPImodule_Clock_Hz,
                                                      &uiPreScaleValue);
    if(uiSetFrequency == 0U)
    {
        FHALT("Failed to set SPI Frequency: %d Hz, Actual Frequency: %d Hz", pstNewConf->uiSPI_Freq_Hz, uiSetFrequency);
        return false;
    }
    pstSPIModule->pstSPIDevice->TCR = (LPSPI_GetTcr(pstSPIDevice) & ~LPSPI_TCR_PRESCALE_MASK) | LPSPI_TCR_PRESCALE(uiPreScaleValue);
    pstMasterCtrl->stMasterConfig.baudRate = uiSetFrequency;
    return true;
}

static inline void vClear_TransferBusyFlag(sT_SPIModuleConfig_t *pstSPIModule)
{
    atomic_store_explicit(&pstSPIModule->bIsTransferBusy, false, memory_order_release);
}

static inline bool bTryClaim_TransferBusyFlag(eSPIModule_t eModuleId)
{
    bool bIsBusy = atomic_exchange_explicit(&staSPIModule[eModuleId].bIsTransferBusy, true, memory_order_acq_rel);
    if(bIsBusy)
    {
        FHALT("SPIMod[%d] Transfer in progress", eModuleId);
        return true;
    }
    return false;
}

static void vConfigure_SPI_DMA(sT_SPIModuleConfig_t *pstSPIModule)
{
    FHALT("Not Implemented yet");
}

static void vAssign_SlaveDevices(sT_SPIModuleConfig_t *pstSPIModule, sT_SPIConfig_t *pstSPIConfig)
{
    if(pstSPIModule == NULL || pstSPIConfig == NULL)
    {
        FHALT("Null Pointer reference for the slave device");
        return;
    }

    sT_SPI_MasterCtrl *pstMasterCtrl = &pstSPIModule->stTSPIDevCtrl.st_DevCtrlMode.stTMasterCtrl;
    pstMasterCtrl->pstSPISlaveHead_Ctrl = pstSPIConfig->stTSPIModeCtrl.spi_mode.pstSPISlaveHead_Ctrl;
    pstSPIConfig->stTSPIModeCtrl.spi_mode.pstSPISlaveHead_Ctrl = NULL;

}

static lpspi_which_pcs_t eGet_SPI_HW_CSPin(eSPI_PCS_t eHW_CSPin)
{
    switch(eHW_CSPin)
    {
        case ePCS_0:
            return kLPSPI_Pcs0;
        case ePCS_1:
            return kLPSPI_Pcs1;
        case ePCS_2:
            return kLPSPI_Pcs2;
        case ePCS_3:
            return kLPSPI_Pcs3;
        default:
            return kLPSPI_Pcs0;
    }
}

static lpspi_pin_config_t eGet_SPIPinConfigurations(eSPI_PinCfg_For_Transfer_t ePinConfig)
{
    switch(ePinConfig)
    {
        case eEn_FullDuplex_Transfer_Normal://SDI : Input, SDO : Output (FullDuplex)
            return kLPSPI_SdiInSdoOut;
        case eEn_HalfDuplex_Transfer_With_SDI://SDI : for Input and Output (Halfduplex transfer only)
            return kLPSPI_SdiInSdiOut;
        case eEn_HalfDuplex_Transfer_With_SDO://SDO : for Input and Output (Halfduplex transfer only)
            return kLPSPI_SdoInSdoOut;
        case eEn_FullDuplex_Transfer_Swapped:
            return kLPSPI_SdoInSdiOut;
        default:
            return kLPSPI_SdiInSdoOut;
    }
}

static lpspi_pcs_function_config_t eGet_PSCConfiguration(eSPI_DataLane_Width_t eDataLaneWidth)
{
    switch(eDataLaneWidth)
    {
        case e4bit_Transfer:
            return kLPSPI_PcsAsData;
        default:
            return kLPSPI_PcsAsCs;
    }
}

static lpspi_pcs_polarity_config_t eGet_CS_PinActiveState(eSPI_CS_Polarity_t eCSPinState)
{
    switch(eCSPinState)
    {
        case eCS_Active_Low:
            return kLPSPI_PcsActiveLow;
        case eCS_Active_High:
            return kLPSPI_PcsActiveHigh;
        default:
            return kLPSPI_PcsActiveLow;
    }
}

static lpspi_shift_direction_t eGetEndianType(eSPI_ShiftDirection_t eShiftDirection)
{
    switch(eShiftDirection)
    {
        case eLSB_First:
            return kLPSPI_LsbFirst;
        case eMSB_First:
            return kLPSPI_MsbFirst;
        default:
            return kLPSPI_MsbFirst;
    }
}

static lpspi_data_out_config_t eGetDataOutState_AtTxEnd(eSPI_DataOut_PinState_t eOutState)
{
    switch(eOutState)
    {
        case eData_Out_TriState:
            return kLpspiDataOutTristate;
        case eData_Out_RetainLastValue:
            return kLpspiDataOutRetained;
        default:
            return kLpspiDataOutTristate;
    }
}

static lpspi_clock_phase_t eGetDefault_CPHA(eSPI_CPOL_CPHA_Type_t eSPIConf_CPHA)
{
    switch(eSPIConf_CPHA)
    {
        case eCPOL_0_CPH_0:
        case eCPOL_1_CPH_0: 
            return kLPSPI_ClockPhaseFirstEdge;
        case eCPOL_1_CPH_1:           
        case eCPOL_0_CPH_1:
            return kLPSPI_ClockPhaseSecondEdge;
        default:
            return kLPSPI_ClockPhaseFirstEdge;
    }    
}

static lpspi_clock_polarity_t eGetDefault_CPOL(eSPI_CPOL_CPHA_Type_t eSPIConf_CPOL)
{
    switch(eSPIConf_CPOL)
    {
        case eCPOL_0_CPH_0:            
        case eCPOL_0_CPH_1:
            return kLPSPI_ClockPolarityActiveHigh;
        case eCPOL_1_CPH_0:
        case eCPOL_1_CPH_1:
            return kLPSPI_ClockPolarityActiveLow;
        default:
            return kLPSPI_ClockPolarityActiveHigh;
    }
}

static LPSPI_Type *pstGetSPIDevice(eSPIModule_t eModule)
{
    switch(eModule)
    {
        case eSPI_0:
            return LPSPI0;
        case eSPI_1:
            return LPSPI1;
        default:
            FHALT("Invalid SPI Module : %d", eModule);
            return NULL;
    }
}

#pragma region Utility Functions

const sT_SPISlave_Config_t *pstGetSlaveConfig(eSPIModule_t eModuleId, eSPI_Slave_Id_t eSlaveId)
{
    if(eModuleId >= eNUMBER_OF_SPI_MODULEs)
    {
        FHALT("Invalid SPI Module : %d", eModuleId);
        return NULL;
    }
    if(bIsModule_Initialized(eModuleId) == false)
    {
        FHALT("SPI Module[%d] not initialized", eModuleId);
        return NULL;
    }
    if(eGetSPIMode(eModuleId) != eSPI_Mode_Controller)
    {
        FHALT("SPI Module[%d] not in Master Mode", eModuleId);
        return NULL;
    }

    sT_SPIModuleConfig_t *pstSPIModule = &staSPIModule[eModuleId];
    sT_SPISlave_Control_t *pstSlaveHead = pstSPIModule->stTSPIDevCtrl.st_DevCtrlMode.stTMasterCtrl.pstSPISlaveHead_Ctrl;

    sT_SPISlave_Control_t *pstSlaveInfo = pstGetSlaveInfo(eSlaveId, pstSlaveHead);
    if(pstSlaveInfo == NULL)
    {
        FHALT("Invalid Slave Info for Module[%d] and SlaveId[%d]", eModuleId, eSlaveId);
        return NULL;
    }
    return &pstSlaveInfo->stTConfigs;
}

#pragma endregion

#endif

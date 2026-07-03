#include "../API_Usage_Definition.h"

#if defined(USE_SPI)

#include <stdbool.h>
#include <stdatomic.h>
#include <zephyr/drivers/pinctrl.h>
#include "fsl_lpspi.h"
#include "fsl_clock.h"
#include <zephyr/irq.h>

#include "NXP_SPI_API.h"
#include "NXP_SPI_ConfigValidation.h"
#include "NXP_SPI_LinkedList.h"
#include "../GenericMacro.h"

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

static void vConfig_TransferHandle(sT_SPIModuleConfig_t *pstSPIModule);
static void vConfigure_SPIInterrupt(sT_SPIModuleConfig_t *pstSPIModule);
static void vSPI_MasterCallback(LPSPI_Type *base,
                                lpspi_master_handle_t *handle,
                                status_t status,
                                void *userData);
static void vEnable_SPI_IRQ(eSPIModule_t eModule);
static void vDisable_SPI_IRQ(eSPIModule_t eModule);
static void vLPSPI0_ISR(const void *arg);
static void vLPSPI1_ISR(const void *arg);
static void vConfigure_SPI_DMA(sT_SPIModuleConfig_t *pstSPIModule);
static void vSPI_FaultRecoveryHandler( struct k_work *work );
static inline sT_SPIModuleConfig_t *pstGetSPIModule_From_KWork(struct k_work_delayable *work);

static inline bool bSPI_Master_Send(sT_SPITransfer_t *pstTTransfer, lpspi_transfer_t *pstLPSPITransfer);
static inline bool bSPI_Master_Receive(sT_SPITransfer_t *pstTTransfer, lpspi_transfer_t *pstLPSPITransfer);
static inline bool bSPI_Master_Transceive(sT_SPITransfer_t *pstTTransfer, lpspi_transfer_t *pstLPSPITransfer);
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
static inline void vSet_TransferBusyFlag(sT_SPIModuleConfig_t *pstSPIModule);
static inline bool bIsTransferBusy(eSPIModule_t eModuleId);
static inline bool bClaim_TransferBusyFlag(sT_SPIModuleConfig_t *pstSPIModule);

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
        default:
            bResult = false;
            break;
    }

    pstSPIConfig->bIsOk = bResult;

    sT_SPIModuleConfig_t *pstMoudle = &staSPIModule[pstSPIConfig->eModule];
    k_work_init_delayable(&pstMoudle->kw_SPITransferMonitor, vSPI_FaultRecoveryHandler);
    SPI_INIT_Print("SPI Module[%d] Init Result: %s\n", pstSPIConfig->eModule, (bResult) ? "Success" : "Failed");
}

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
    pstSPIModule->bIsInitialized = false;
    vClear_TransferBusyFlag(pstSPIModule);
    pstMasterCtrl->eActiveSlaveId = eNUMBER_OF_SPI_SLAVEs;
    return true;
}

static bool bDeinit_SlaveMode( eSPIModule_t eModule )
{
    FHALT("Not Initialized");
    return false;
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
    pstSPIModule->bIsTransferBusy = false;
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
    LPSPI_DriverIRQHandler(0U);
}

static void vLPSPI1_ISR(const void *arg)
{
    ARG_UNUSED(arg);
    LPSPI_DriverIRQHandler(1U);
}

static void vSPI_MasterCallback(LPSPI_Type *base,
                                lpspi_master_handle_t *handle,
                                status_t status,
                                void *userData)
{
    sT_SPIModuleConfig_t *pstSPIModule = (sT_SPIModuleConfig_t *)userData;
    sT_SPI_MasterCtrl *pstMasterCtrl = &pstSPIModule->stTSPIDevCtrl.st_DevCtrlMode.stTMasterCtrl;

    sT_SPISlave_Control_t *pstSlave = pstGetSlaveInfo(pstMasterCtrl->eActiveSlaveId, pstMasterCtrl->pstSPISlaveHead_Ctrl);
    if(!bClaim_TransferBusyFlag(pstSPIModule))
        return;
        
    k_work_cancel_delayable(&pstSPIModule->kw_SPITransferMonitor);

    if(pstSlave == NULL || pstSlave->stTConfigs.pvSPI_CallBack == NULL)
        return;
    
    eSPI_TransferResult_t eResult = (status == kStatus_Success) ? eTransfer_Success : eTransfer_Failed;
    pstSlave->stTConfigs.pvSPI_CallBack(pstMasterCtrl->eActiveSlaveId, eResult);
}

static inline bool bClaim_TransferBusyFlag(sT_SPIModuleConfig_t *pstSPIModule)
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
       !bClaim_TransferBusyFlag(pstSPIModule) ||
       eGetSPIMode(eModuleId) != eSPI_Mode_Controller)
       return;
       
    sT_SPI_MasterCtrl *pstMasterCtrl = &pstSPIModule->stTSPIDevCtrl.st_DevCtrlMode.stTMasterCtrl;       
    LPSPI_MasterTransferAbort(pstSPIModule->pstSPIDevice, &pstMasterCtrl->stMasterHandle);

    sT_SPISlave_Control_t *pstSlave = pstGetSlaveInfo(pstMasterCtrl->eActiveSlaveId, pstMasterCtrl->pstSPISlaveHead_Ctrl);
    if(pstSlave == NULL || pstSlave->stTConfigs.pvSPI_CallBack == NULL)
    {
        FHALT("Null Callback Pointer");
        return;
    }

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

bool bSPI_Transfer_InMasterMode(sT_SPITransfer_t stTTransfer)
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
    if(bIsTransferBusy(eModuleId))
    {
        FHALT("Module[%d] Transfer in Progress", eModuleId);
        return false;
    }    
    
    sT_SPIModuleConfig_t *pstModule = &staSPIModule[eModuleId];
    sT_SPI_MasterCtrl *pstMasterCtrl = &pstModule->stTSPIDevCtrl.st_DevCtrlMode.stTMasterCtrl;
    if((pstMasterCtrl->eSPI_BusWidth == e2bit_Transfer || pstMasterCtrl->eSPI_BusWidth == e4bit_Transfer) &&
        stTTransfer.eType == eTransfer_Transceive)
    {
        FHALT("Full duplex transfer cannot be executed while Module[%d] with Data Width: %d", eModuleId, pstMasterCtrl->eSPI_BusWidth);
        return false;        
    }

    if(pstMasterCtrl->eActiveSlaveId != stTTransfer.eSlaveId)
    {
        LPSPI_Enable(pstModule->pstSPIDevice, false);
        bResult = bSetUp_NewSlaveConfig(eModuleId, pstMasterCtrl, stTTransfer.eSlaveId);
        if(!bResult)
        {
            LPSPI_Enable(pstModule->pstSPIDevice, true);
            return false;
        }
        LPSPI_Enable(pstModule->pstSPIDevice, true);
    }

    vSet_TransferBusyFlag(pstModule);

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

static inline bool bSPI_Master_Send(sT_SPITransfer_t *pstTTransfer, lpspi_transfer_t *pstLPSPITransfer)
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

static inline bool bSPI_Master_Receive(sT_SPITransfer_t *pstTTransfer, lpspi_transfer_t *pstLPSPITransfer)
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

static inline bool bSPI_Master_Transceive(sT_SPITransfer_t *pstTTransfer, lpspi_transfer_t *pstLPSPITransfer)
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

static inline void vSet_TransferBusyFlag(sT_SPIModuleConfig_t *pstSPIModule)
{
    atomic_store_explicit(&pstSPIModule->bIsTransferBusy, true, memory_order_release);    
}

static inline bool bIsTransferBusy(eSPIModule_t eModuleId)
{
    bool bIsBusy = atomic_load_explicit(&staSPIModule[eModuleId].bIsTransferBusy, memory_order_acquire);
    return bIsBusy;
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
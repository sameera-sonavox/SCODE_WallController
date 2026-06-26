#include "../API_Usage_Definition.h"

#if defined(USE_SPI)

#include <stdbool.h>
#include <stdatomic.h>
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
} sT_SPIModuleConfig_t;

sT_SPIModuleConfig_t staSPIModule[eNUMBER_OF_SPI_MODULEs] = {
    {.eModuleId = eSPI_0},
    {.eModuleId = eSPI_1}
};

static bool bConfig_SPI_MasterMode( sT_SPIConfig_t *pstSPIConfig );
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

static bool bSetUp_NewSlaveConfig(eSPIModule_t eModuleId, sT_SPI_MasterCtrl *pstMasterCtrl, eSPI_Slave_Id_t eNewSlaveId);
static bool bSPI_Assign_NewFrequency(eSPIModule_t eModuleId, sT_SPISlave_Config_t *pstCurConf, sT_SPISlave_Config_t *pstNewConf);

static inline void vClear_TransferBusyFlag(sT_SPIModuleConfig_t *pstSPIModule);
static inline void vSet_TransferBusyFlag(sT_SPIModuleConfig_t *pstSPIModule);
static inline bool bIsTransferBusy(eSPIModule_t eModuleId);

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

void vInit_SPI( sT_SPIConfig_t *pstSPIConfig )
{
    bool bResult = false;

    if(pstSPIConfig == NULL)
    {
        SPI_INIT_Print("NULL Pointer Reference");
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

static inline eSPI_Mode_t eGetSPIMode( eSPIModule_t eModule )
{
    if(eModule >= eNUMBER_OF_SPI_MODULEs)
    {
        FHALT("Invalid SPI Module : %d", eModule);
        return false;
    }
    return staSPIModule[eModule].stTSPIDevCtrl.eMode;    
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

    LPSPI_MasterInit(pstSPIModule->pstSPIDevice, pstspiMasterConf, pstSPIModule->uiSPImodule_Clock_Hz);

    vConfig_TransferHandle(pstSPIModule);
    pstSPIModule->bIsInitialized = true;
    return true;
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
    vClear_TransferBusyFlag(pstSPIModule);

    if(pstSlave == NULL || pstSlave->stTConfigs.pvSPI_CallBack == NULL)
        return;
    
    pstSlave->stTConfigs.pvSPI_CallBack(pstMasterCtrl->eActiveSlaveId, NULL);
}

bool bSPI_Transfer_InMasterMode(sT_SPITransfer_t stTTransfer)
{
    bool bResult = false;

    eSPIModule_t eModuleId = stTTransfer.eModuleId;
    if(!bIsModule_Initialized(eModuleId))
    {
        FHALT("Module[%d] not initialized", eModuleId);
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
        bResult = bSetUp_NewSlaveConfig(eModuleId, pstMasterCtrl, stTTransfer.eSlaveId);
        if(!bResult)
            return false;
    }
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
    
    sT_SPISlave_Config_t *pstCurConf = &pstCurrentSlaveInfo->stTConfigs;
    sT_SPISlave_Config_t *pstNewConf = &pstNewSlaveInfo->stTConfigs;
    
    bResult = bSPI_Assign_NewFrequency(eModuleId, pstCurConf, pstNewConf);
    if(!bResult) return bResult;

    pstMasterCtrl->eActiveSlaveId = eNewSlaveId;
    return true;
}

static bool bSPI_Assign_NewFrequency(eSPIModule_t eModuleId, sT_SPISlave_Config_t *pstCurConf, sT_SPISlave_Config_t *pstNewConf)
{    
    if(pstCurConf->uiSPI_Freq_Hz == pstNewConf->uiSPI_Freq_Hz)
        return true;
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

#endif
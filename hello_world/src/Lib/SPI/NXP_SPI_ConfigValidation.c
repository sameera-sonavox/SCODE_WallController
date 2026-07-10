
#include "../API_Usage_Definition.h"

#if defined(USE_SPI)

#include "fsl_clock.h"
#include "NXP_SPI_ConfigValidation.h"
#include "../GenericMacro.h"
#include "NXP_SPI_ProjDef.h"

static bool bValidate_SPI_Controller_Configs( sT_SPIConfig_t *pstSPIConfig );
static bool bValidate_SPIBusWidth_Configurations(sT_SPISlave_Control_t *pstSlaveCtrl);

static bool bValidate_SPI_Peripheral_Configs( sT_SPIConfig_t *pstSPIConfig );
static bool bValidate_SPISlave_DataConfigPath(sT_SPISlave_RxControl_t *pstSlaveDataPath);
static bool bValidate_CallbackSettings(sT_Callback_Ctrl *pstCallBackSettings);

bool bValidate_SPI_Config( sT_SPIConfig_t *pstSPIConfig )
{
    if(pstSPIConfig == NULL)
    {
        FHALT("Null Pointer reference");
        return false;
    }
    if(pstSPIConfig->eModule >= eNUMBER_OF_SPI_MODULEs)
    {
        pstSPIConfig->bIsOk = false;
        FHALT("Invalid SPI Stack : %d", pstSPIConfig->eModule);
        return false;
    }

    if(pstSPIConfig->eModule == eSPI_0)
    {
        #if !defined(USE_SPI_0)
            pstSPIConfig->bIsOk = false;
            FHALT("SPI Module[%d] not enabled in Project. Please define 'USE_SPI_0' in your project settings.", pstSPIConfig->eModule);
            return false;
        #endif
    }

    if(pstSPIConfig->eModule == eSPI_1)
    {
        #if !defined(USE_SPI_1)
            pstSPIConfig->bIsOk = false;
            FHALT("SPI Module[%d] not enabled in Project. Please define 'USE_SPI_1' in your project settings.", pstSPIConfig->eModule);
            return false;
        #endif
    }
    
    if(pstSPIConfig->eDataOutPinState >= eNUMBER_OF_DATA_OUTSTATEs)
    {
        pstSPIConfig->bIsOk = false;
        FHALT("Invalid Pin Out State when Idle : %d", pstSPIConfig->eDataOutPinState);
        return false;
    }
    if(pstSPIConfig->eNotificationType >= eNUMBER_OF_SPI_NOTIFICATION_TYPEs)
    {
        pstSPIConfig->bIsOk = false;
        FHALT("Invalid Notification Type : %d", pstSPIConfig->eNotificationType);
        return false;
    }

    switch(pstSPIConfig->stTSPIModeCtrl.eMode)
    {
        case eSPI_Mode_Peripheral:
            return bValidate_SPI_Peripheral_Configs(pstSPIConfig);
        case eSPI_Mode_Controller:
            return bValidate_SPI_Controller_Configs(pstSPIConfig);
        default:
            pstSPIConfig->bIsOk = false;
            FHALT("Invalid SPI Mode Selected : %d", pstSPIConfig->stTSPIModeCtrl.eMode);
            return false;
    }
}

static bool bValidate_SPI_Peripheral_Configs( sT_SPIConfig_t *pstSPIConfig )
{
    sT_Peripheral_Config_t *pstSlaveConfig = &pstSPIConfig->stTSPIModeCtrl.spi_mode.stTConfig_Peripheral;
    if(pstSlaveConfig == NULL)
    {
        pstSPIConfig->bIsOk = false;
        FHALT("Null Pointer reference for Slave Configurations");
        return false;
    }

    if(pstSlaveConfig->eCPOLCPH_Ctrl >= eNUMBER_OF_CLK_POL_PHASE_COMBINATIONs)
    {
        pstSPIConfig->bIsOk = false;
        FHALT("Invalid Phase vs Polarity Config : %d", pstSlaveConfig->eCPOLCPH_Ctrl);
        return false;
    }
    if(pstSlaveConfig->eCSPin >= eNUMBER_OF_PERIPHERAL_CS_LINEs)
    {
        pstSPIConfig->bIsOk = false;
        FHALT("Invalid Chip Select Pin : %d", pstSlaveConfig->eCSPin);
        return false;
    }
    if(pstSlaveConfig->eEndianFormat >= eNUMBER_OF_SPIWORD_TXRX_TYPEs)
    {
        pstSPIConfig->bIsOk = false;
        FHALT("Invalid Endian Format : %d", pstSlaveConfig->eEndianFormat);
        return false;
    }
    if(pstSlaveConfig->eSlaveMode_CS_Ctrl >= eNUMBER_OF_CS_CONFIGs)
    {
        pstSPIConfig->bIsOk = false;
        FHALT("Invalid CS State : %d", pstSlaveConfig->eSlaveMode_CS_Ctrl);
        return false;
    }
    if(pstSlaveConfig->eSPI_BusWidth >= eNUMBER_OF_SPI_DATALANEs)
    {
        pstSPIConfig->bIsOk = false;
        FHALT("Invalid SPI Bus Width : %d", pstSlaveConfig->eSPI_BusWidth);
        return false;
    }

    sT_SPISlave_RxControl_t *pstSlaveDataPath = &pstSlaveConfig->stTRxControl;
    if(!bValidate_SPISlave_DataConfigPath(pstSlaveDataPath))
    {
        pstSPIConfig->bIsOk = false;
        FHALT("The Slave Data Path Validation Fail for SPI Module[%d]", pstSPIConfig->eModule);
        return false;        
    }

    sT_HWMatch_Config_t *pstHWMatchConfig = &pstSlaveConfig->stTHWMatchConfig;
    if(pstHWMatchConfig->eHW_Recv_SyncType >= eNUMBER_OF_HW_MATCH_CONFIGURATIONs)
    {
        FHALT("Invalid HW Match Configurations : %d", pstHWMatchConfig->eHW_Recv_SyncType);
        return false;
    }
    if(pstHWMatchConfig->eHW_Recv_SyncType != eHW_Match_Disabled && 
       (pstHWMatchConfig->uiMatch0_Value == 0 && pstHWMatchConfig->uiMatch1_Value == 0))
    {
        FHALT("Invalid Matching values for Match0 & 1 [ Match0 : %d, Match1: %d]", 
              pstHWMatchConfig->uiMatch0_Value,
              pstHWMatchConfig->uiMatch1_Value);
        return false;
    }
/*     if(pstSlaveConfig->)
    {
        pstSPIConfig->bIsOk = false;
        FHALT("Invalid SPI Bus Width : %d", pstSlaveConfig->eSPI_BusWidth);
        return false;
    } */
        
    return true;
}

static bool bValidate_SPISlave_DataConfigPath(sT_SPISlave_RxControl_t *pstSlaveDataPath)
{
    if(pstSlaveDataPath == NULL)
    {
        FHALT("Null Pointer reference");
        return false;
    }

    switch (pstSlaveDataPath->eDataPathType)
    {
        case eTransfer_Use_MessageBus:
            FHALT("Requested Path Not Implemented : %d", pstSlaveDataPath->eDataPathType);
            return false;
        case eTransfer_Use_Callback:
            return bValidate_CallbackSettings(&pstSlaveDataPath->slave_dataPath.stTCallbackConfig);        
        default:
            FHALT("Invalid User Data Path : %d", pstSlaveDataPath->eDataPathType);
            return false;
    }
}

static bool bValidate_CallbackSettings(sT_Callback_Ctrl *pstCallBackSettings)
{
    if(pstCallBackSettings == NULL)
    {
        FHALT("Null Pointer reference");
        return false;
    }

    if(pstCallBackSettings->pvSPI_PeripheralCallBack == NULL)
    {
        FHALT("User Callback Fn cannot be null");
        return false;
    }

    if(pstCallBackSettings->uiBuffSize == 0)
    {
        FHALT("Buffer size should be a non-zero value");
        return false;        
    }

    if(pstCallBackSettings->uiBuffCount == 0)
    {
        FHALT("User must request minimum of one Rx Buffer");
        return false;        
    }

    return true;
}

static bool bValidate_SPI_Controller_Configs( sT_SPIConfig_t *pstSPIConfig )
{
    sT_SPISlave_Control_t *pstSlaveCtrl = pstSPIConfig->stTSPIModeCtrl.spi_mode.pstSPISlaveHead_Ctrl;
    if(pstSlaveCtrl == NULL)
    {
        pstSPIConfig->bIsOk = false;
        FHALT("Null Pointer reference for Slave Head Node");
        return false;
    }

    uint32_t uiModuleSPIClock_Hz = CLOCK_GetLpspiClkFreq(pstSPIConfig->eModule);
    uiModuleSPIClock_Hz = (uiModuleSPIClock_Hz / 2U);
    if(uiModuleSPIClock_Hz == 0U)
    {
        pstSPIConfig->bIsOk = false;
        FHALT("Invalid SPI Clock Set for SPI Module[%d] : %d", pstSPIConfig->eModule, uiModuleSPIClock_Hz);
        return false;        
    }

    sT_SPISlave_Control_t *pstTemp = pstSlaveCtrl;
    while(pstTemp != NULL)
    {
        sT_SPISlave_Config_t *pstSlaveConfig = &pstTemp->stTConfigs;

        if(pstSlaveConfig->eSlaveId >= eNUMBER_OF_SPI_SLAVEs)
        {
            pstSPIConfig->bIsOk = false;
            FHALT("Invalid Id for Slave : %d", pstSlaveConfig->eSlaveId);
            return false;
        }

        if(pstSlaveConfig->eEndianFormat >= eNUMBER_OF_SPIWORD_TXRX_TYPEs)
        {
            pstSPIConfig->bIsOk = false;
            FHALT("Invalid Endian Format : %d", pstSlaveConfig->eEndianFormat);
            return false;
        }
        if(pstSlaveConfig->bIs_CS_HWControlled && pstSlaveConfig->eHW_PCS_Ctrl >= eNUMBER_OF_PERIPHERAL_CS_LINEs)
        {
            pstSPIConfig->bIsOk = false;
            FHALT("Invalid Slave ChipSelect Pin Setup : %d", pstSlaveConfig->eHW_PCS_Ctrl);
            return false;
        }
        if(pstSlaveConfig->eCPOLCPH_Ctrl >= eNUMBER_OF_CLK_POL_PHASE_COMBINATIONs)
        {
            pstSPIConfig->bIsOk = false;
            FHALT("Invalid Phase vs Polarity Config : %d", pstSlaveConfig->eCPOLCPH_Ctrl);
            return false;
        }
        if(pstSlaveConfig->eSPI_BusWidth >= eNUMBER_OF_SPI_DATALANEs)
        {
            pstSPIConfig->bIsOk = false;
            FHALT("Invalid Data Lane Setting : %d", pstSlaveConfig->eSPI_BusWidth);
            return false;
        }
        if(pstSlaveConfig->eCSPolarityType >= eNUMBER_OF_CS_CONFIGs)
        {
            pstSPIConfig->bIsOk = false;
            FHALT("Invalid CS Line Polarity : %d", pstSlaveConfig->eCSPolarityType);
            return false;
        }
        if(pstSlaveConfig->uiSPI_Freq_Hz <= 0U || pstSlaveConfig->uiSPI_Freq_Hz > uiModuleSPIClock_Hz)
        {
            pstSPIConfig->bIsOk = false;
            FHALT("Invalid SPI Frequency : %d", pstSlaveConfig->uiSPI_Freq_Hz);
            return false;
        }
        if(pstSlaveConfig->pvSPI_CallBack == NULL)
        {
            pstSPIConfig->bIsOk = false;
            FHALT("NULL Callback Function for SPI Slave : %d", pstSlaveConfig->eSlaveId);
            return false;
        }

        pstTemp = pstTemp->pstNextSlave;
    }

    if(!bValidate_SPIBusWidth_Configurations(pstSlaveCtrl))
        return false;

    return true;
}

static bool bValidate_SPIBusWidth_Configurations(sT_SPISlave_Control_t *pstSlaveCtrl)
{
    eSPI_DataLane_Width_t eBusWidth = eNUMBER_OF_SPI_DATALANEs;
    bool bIsChanged = false;

    sT_SPISlave_Control_t *pstTemp = pstSlaveCtrl;
    while(pstTemp != NULL)
    {
        sT_SPISlave_Config_t *pstSlaveConfig = &pstTemp->stTConfigs;

        if(eBusWidth != pstSlaveConfig->eSPI_BusWidth && !bIsChanged)
        {
            eBusWidth = pstSlaveConfig->eSPI_BusWidth;
            bIsChanged = true;
        }
        else if(bIsChanged && eBusWidth != pstSlaveConfig->eSPI_BusWidth)
        {
            FHALT("Bus Width cannot be different for multiple Slaves shares the same bus");
            return false;
        }

        if( pstSlaveConfig->bIs_CS_HWControlled &&
            pstSlaveConfig->eSPI_BusWidth == e4bit_Transfer && 
           (pstSlaveConfig->eHW_PCS_Ctrl == ePCS_2 || pstSlaveConfig->eHW_PCS_Ctrl == ePCS_3))
        {
            FHALT("You cannot use either 'ePCS_2' or 'ePCS_3' With 'e4bit_Transfer'");
            return false;            
        }

        pstTemp = pstTemp->pstNextSlave;
    }

    return true;
}

#endif
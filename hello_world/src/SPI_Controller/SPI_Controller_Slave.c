#ifdef USE_SPI

#include "SPI_Controller_Slave.h"
#include "../Lib/GenericMacro.h"
#include <zephyr/kernel.h>

uint8_t uiaTxData[8] = {0x55, 0x66, 0x77, 0x88, 0x99, 0x11, 0x22, 0x33};

static void vSPI_SlaveCallback(eSPI_PeripheralEvent_Type_t eEventType, 
                               eSPI_TransferResult_t eResult, 
                               sT_RxBuffData_t stTBuffData,
                               bool bIsBufferAssignmentSuccess);

void vConfigure_SPISLave( void )
{
	sT_SPIConfig_t stSPIConfig = {0};
	stSPIConfig.eModule = eSPI_0;
	stSPIConfig.eNotificationType = eNotify_Interrupt;
	stSPIConfig.eDataOutPinState = eData_Out_TriState;
	stSPIConfig.ePinConfig = eEn_FullDuplex_Transfer_Normal;

	stSPIConfig.stTSPIModeCtrl.eMode = eSPI_Mode_Peripheral;
    sT_Peripheral_Config_t *pstSlaveControl = &stSPIConfig.stTSPIModeCtrl.spi_mode.stTConfig_Peripheral;

    pstSlaveControl->bRequest_TxNotifications = false;
    pstSlaveControl->eCPOLCPH_Ctrl = eCPOL_0_CPH_0;
    pstSlaveControl->eSlaveMode_CS_Ctrl = eCS_Active_Low;
    pstSlaveControl->eCSPin = ePCS_0;
    pstSlaveControl->eEndianFormat = eMSB_First;
    pstSlaveControl->eSPI_BusWidth = e1bit_Transfer;
    pstSlaveControl->uiFrameSize = 8U;
    pstSlaveControl->eHWRdy_PinState = eSPI_Rdy_Active_High;

    sT_HWMatch_Config_t *pstHWMatchConfig = &pstSlaveControl->stTHWMatchConfig;
    pstHWMatchConfig->bFIFO_StoreOnly_MatchedData = false;
    pstHWMatchConfig->eHW_Recv_SyncType = eHW_Match_FirstWord_With_Match0_MaskedWith_Match1;
    pstHWMatchConfig->uiMatch0_Value = 0x34;
    pstHWMatchConfig->uiMatch1_Value = 0xFF;

    sT_SPISlave_RxControl_t *pstSlaveRxCtrl = &pstSlaveControl->stTRxControl;
    pstSlaveRxCtrl->eDataPathType = eTransfer_Use_Callback;
    pstSlaveRxCtrl->eOverflowPolicy = eSPI_Overflow_DropNewest;
    
    sT_Callback_Ctrl *pstCallbackCtrl = &pstSlaveRxCtrl->slave_dataPath.stTCallbackConfig;
    pstCallbackCtrl->pvSPI_PeripheralCallBack = vSPI_SlaveCallback;
    pstCallbackCtrl->uiBuffCount = 5U;
    pstCallbackCtrl->uiBuffSize = 8U;

    vInit_SPI(&stSPIConfig);   
}

static void vSPI_SlaveCallback(eSPI_PeripheralEvent_Type_t eEventType, 
                               eSPI_TransferResult_t eResult, 
                               sT_RxBuffData_t stTBuffData,
                               bool bIsBufferAssignmentSuccess)
{
    if(eEventType == eSPI_PeripheralEvent_RxReady &&
       eResult == eTransfer_Success &&
       stTBuffData.eState == eBuffer_Ready)
    {
        bSPI_ReleasePeripheralMode_RxBuffer(eSPI_0, stTBuffData.uiBuffId);

        sT_SPIPreipheralResponse_t stTResponse = {
            .eModuleId = eSPI_0,
            .puiTxData = uiaTxData,
            .uiLen = 8U
        };

        if(!bSPI_PeripheralSendResponse(stTResponse))
        {
            FHALT("Slave Tx Error");
        }

        return;
    }

    if(eEventType == eSPI_PeripheralEvent_TxCompleted)
    {
        return;
    }

    FHALT("SPI Event:%d Result:%d State:%d Size:%d\n\r",
          eEventType,
          eResult,
          stTBuffData.eState,
          stTBuffData.uisize);
}

#endif

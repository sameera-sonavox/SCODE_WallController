#include "SPI_Controller_Master.h"
#include "SPI/NXP_SPI_API.h"
#include "GenericMacro.h"

#ifdef USE_SPI
sT_SPIMasterTransfer_t stTTransfer[eNUMBER_OF_SPI_SLAVEs] = {0};
void SPI_Slave_0_Callback(eSPI_Slave_Id_t eSlaveId, eSPI_TransferResult_t eResult);
void vFree_SPI_TansferBuffers(sT_SPIMasterTransfer_t *pstTTransfer);
static inline bool bTry_ClaimSlave(eSPI_Slave_Id_t eSlaveId);
static inline void vClear_SlaveBusyFLag(eSPI_Slave_Id_t eSlaveId);
bool bCaller_Should_Release_Buffer(eSPIBuffer_ReleaseType_t eReleaseType);

void vConfigure_SPI( void )
{
	sT_SPIConfig_t stSPIConfig = {0};
	stSPIConfig.eModule = eSPI_0;
	stSPIConfig.eNotificationType = eNotify_Interrupt;
	stSPIConfig.eDataOutPinState = eData_Out_RetainLastValue;
	stSPIConfig.ePinConfig = eEn_FullDuplex_Transfer_Normal;

	stSPIConfig.stTSPIModeCtrl.eMode = eSPI_Mode_Controller;
	
	sT_SPISlave_Config_t stSlaveConfig = {0};
	stSlaveConfig.eSlaveId = eSPI_Slave_0;
	stSlaveConfig.bIs_CS_HWControlled = true;
	stSlaveConfig.bEn_SCKLoopBack_ForSampling = false;
	stSlaveConfig.eCPOLCPH_Ctrl = eCPOL_0_CPH_0;
	stSlaveConfig.eCSPolarityType = eCS_Active_Low;
	stSlaveConfig.eEndianFormat = eMSB_First;
	stSlaveConfig.eHW_PCS_Ctrl = ePCS_0;
	stSlaveConfig.eSPI_BusWidth = e1bit_Transfer;
	stSlaveConfig.uiSPI_Freq_Hz = 1000000;
	stSlaveConfig.uiDelay_Between_BlockTx_ns = 6000;
	stSlaveConfig.uiDelay_CS_Assert_To_SCK_ns = 100;
	stSlaveConfig.uiDelay_LastSCK_To_CS_Deassert_ns = 300;
	stSlaveConfig.pvSPI_CallBack = SPI_Slave_0_Callback;
	stSlaveConfig.stTHWReadyCtrl.bHWReady_Used = true;
	stSlaveConfig.stTHWReadyCtrl.eHWRdy_PinState = eSPI_Rdy_Active_High;
	stSlaveConfig.stTHWReadyCtrl.pstGPIOStruct = &stSPI0_Slave0_RdyGPIO;

	stSPIConfig.stTSPIModeCtrl.spi_mode.pstSPISlaveHead_Ctrl = pstCreate_SPISlave(&stSlaveConfig);

	
/* 	stSlaveConfig.eSlaveId = eSPI_Slave_1;
	stSlaveConfig.bIs_CS_HWControlled = true;
	stSlaveConfig.bEn_SCKLoopBack_ForSampling = false;
	stSlaveConfig.eCPOLCPH_Ctrl = eCPOL_0_CPH_0;
	stSlaveConfig.eCSPolarityType = eCS_Active_Low;
	stSlaveConfig.eEndianFormat = eMSB_First;
	stSlaveConfig.eHW_PCS_Ctrl = ePCS_1;
	stSlaveConfig.eSPI_BusWidth = e1bit_Transfer;
	stSlaveConfig.uiSPI_Freq_Hz = 3000000;
	stSlaveConfig.uiDelay_Between_BlockTx_ns = 1000;
	stSlaveConfig.uiDelay_CS_Assert_To_SCK_ns = 100;
	stSlaveConfig.uiDelay_LastSCK_To_CS_Deassert_ns = 100;
	stSlaveConfig.pvSPI_CallBack = SPI_Slave_0_Callback;
	if(!bInsert_SPISlave_AtEnd(&stSPIConfig.stTSPIModeCtrl.spi_mode.pstSPISlaveHead_Ctrl, &stSlaveConfig))
	{
		FHALT("Failed to insert SPI Slave[%d] at end of linked list", stSlaveConfig.eSlaveId);
		return;
	} */

	stTTransfer[eSPI_Slave_0].eModuleId = eSPI_0;
	stTTransfer[eSPI_Slave_0].eSlaveId = eSPI_Slave_0;
	stTTransfer[eSPI_Slave_0].eType = eTransfer_Tx_Only;
	stTTransfer[eSPI_Slave_0].bShould_CS_Asserted_For_EntireTransfer = true;
/* 
	stTTransfer[eSPI_Slave_1].eModuleId = eSPI_0;
	stTTransfer[eSPI_Slave_1].eSlaveId = eSPI_Slave_1;
	stTTransfer[eSPI_Slave_1].eType = eTransfer_Rx_Only;
	stTTransfer[eSPI_Slave_1].bShould_CS_Asserted_For_EntireTransfer = true; */

	vInit_SPI(&stSPIConfig);
  
}

static inline bool bTry_ClaimSlave(eSPI_Slave_Id_t eSlaveId)
{
	sT_SPIMasterTransfer_t *pstTTransfer = &stTTransfer[eSlaveId];
	bool bStatus = atomic_exchange_explicit(&pstTTransfer->bIsTransferBusy, true, memory_order_acq_rel);
	return !bStatus;
}

static inline void vClear_SlaveBusyFLag(eSPI_Slave_Id_t eSlaveId)
{
	sT_SPIMasterTransfer_t *pstTTransfer = &stTTransfer[eSlaveId];
	atomic_store_explicit(&pstTTransfer->bIsTransferBusy, false, memory_order_release);
}

bool bSPI_SendData(eSPI_Slave_Id_t eSlaveId, uint8_t *puiTxData, uint8_t uiLen)
{
	if(eSlaveId >= eNUMBER_OF_SPI_SLAVEs)
	{
		FHALT("Invalid SLave Id: %d", eSlaveId);
		return false;
	}
	if(!bTry_ClaimSlave(eSlaveId))
	{
		FHALT("SlaveWrite is Busy");
		return false;
	}

    sT_SPIMasterTransfer_t *pstTTransfer = &stTTransfer[eSlaveId];
    pstTTransfer->puiTxData = puiTxData;
    pstTTransfer->uiTxDataLen = uiLen;
	pstTTransfer->eType = eTransfer_Tx_Only;
	pstTTransfer->eTxBufReleaseType = eSPI_Buffer_Static;
	pstTTransfer->eRxBufReleaseType = eSPI_Buffer_None;
    
	bool bRes = bSPI_Transfer_InMasterMode(*pstTTransfer);
	if(!bRes)
	{
		vClear_SlaveBusyFLag(eSlaveId);
	}
	return bRes;
}

bool bSPI_ReceiveData(eSPI_Slave_Id_t eSlaveId, uint8_t *puiCMD, uint8_t uiCMDLen, uint8_t uiRxLen)
{
	if(eSlaveId >= eNUMBER_OF_SPI_SLAVEs)
	{
		FHALT("Invalid SLave Id: %d", eSlaveId);
		return false;
	}
	if(!bTry_ClaimSlave(eSlaveId))
	{
		FHALT("Slave Transfer is Busy");
		return false;		
	}

	sT_SPIMasterTransfer_t *pstTTransfer = &stTTransfer[eSlaveId];

	if((puiCMD != NULL && uiCMDLen == 0) || (puiCMD == NULL && uiCMDLen != 0))
	{
		vClear_SlaveBusyFLag(eSlaveId);
		FHALT("Invalid parameters for SPI Slave[%d] Receive Data: puiCMD is not NULL but uiCMDLen is 0", eSlaveId);
		return false;
	}

	if(puiCMD != NULL)
	{
		pstTTransfer->puiTxData = (uint8_t *)malloc(uiCMDLen + uiRxLen);
		if(pstTTransfer->puiTxData == NULL)
		{
			vClear_SlaveBusyFLag(eSlaveId);
			FHALT("Failed to allocate memory for SPI Slave[%d] Tx Data", eSlaveId);
			return false;
		}

		pstTTransfer->uiTxDataLen = uiRxLen + uiCMDLen;
		memcpy(pstTTransfer->puiTxData, puiCMD, uiCMDLen);
		memset(pstTTransfer->puiTxData + uiCMDLen, 0, uiRxLen);
		pstTTransfer->uiRxMaskLen = uiCMDLen;
		pstTTransfer->eType = eTransfer_Transceive;
		pstTTransfer->eTxBufReleaseType = eSPI_Buffer_Dynamic_And_Free_ByCaller;
	}
	else
	{
		pstTTransfer->puiTxData = NULL;
		pstTTransfer->uiTxDataLen = uiRxLen;
		pstTTransfer->uiRxMaskLen = 0;
		pstTTransfer->eType = eTransfer_Rx_Only;
		pstTTransfer->eTxBufReleaseType = eSPI_Buffer_Dynamic_And_Free_ByCaller;
	}

	uint8_t *puiData = (uint8_t *)malloc(pstTTransfer->uiTxDataLen);
	if(puiData == NULL)
	{
		if(pstTTransfer->eTxBufReleaseType != eSPI_Buffer_Static)
		{
			free(pstTTransfer->puiTxData);
			pstTTransfer->puiTxData = NULL;
		}
		vClear_SlaveBusyFLag(eSlaveId);
		FHALT("Failed to allocate memory for SPI Slave[%d] Rx Data", eSlaveId);
		return false;
	}
	
	pstTTransfer->puiRxData = puiData;
	pstTTransfer->uiRxDataLen = pstTTransfer->uiTxDataLen;
	pstTTransfer->eRxBufReleaseType = eSPI_Buffer_Dynamic_And_Free_ByCaller;

	bool bRes = bSPI_Transfer_InMasterMode(*pstTTransfer);
	if(bRes)
		return bRes;

	vFree_SPI_TansferBuffers(pstTTransfer);
	return false;
}

void SPI_Slave_0_Callback(eSPI_Slave_Id_t eSlaveId, eSPI_TransferResult_t eResult)
{
	if(eSlaveId >= eNUMBER_OF_SPI_SLAVEs)
	{
		FHALT("Invalid SLave Id: %d", eSlaveId);
		return;
	}

	sT_SPIMasterTransfer_t *pstTransfer = &stTTransfer[eSlaveId];

	if(eResult != eTransfer_Success)
	{
		vFree_SPI_TansferBuffers(pstTransfer);
		return;
	}

	switch(pstTransfer->eType)
	{
		case eTransfer_Tx_Only:
			vFree_SPI_TansferBuffers(pstTransfer);
			return;
    	case eTransfer_Rx_Only:
		case eTransfer_Transceive:
			break;
		default:
			vFree_SPI_TansferBuffers(pstTransfer);
			return;

	}
	//Need to get the received data
	uint8_t *puiRxData = pstTransfer->puiRxData + pstTransfer->uiRxMaskLen;
	uint8_t uiRxLen = pstTransfer->uiRxDataLen - pstTransfer->uiRxMaskLen;
	printf("Slave[%d] : Recv.Data Length = %d @Data[0]: %d\n\r", eSlaveId, uiRxLen, puiRxData[0]);

	vFree_SPI_TansferBuffers(pstTransfer);
}

void vFree_SPI_TansferBuffers(sT_SPIMasterTransfer_t *pstTTransfer)
{
	if(pstTTransfer == NULL)
	{
		FHALT("NULL Pointer Reference");
		return;
	}

	if(pstTTransfer->puiTxData != NULL && bCaller_Should_Release_Buffer(pstTTransfer->eTxBufReleaseType))
	{
		free(pstTTransfer->puiTxData);
		pstTTransfer->puiTxData = NULL;
	}
	if(pstTTransfer->puiRxData != NULL && bCaller_Should_Release_Buffer(pstTTransfer->eRxBufReleaseType))
	{
		free(pstTTransfer->puiRxData);
		pstTTransfer->puiRxData = NULL;
	}

	pstTTransfer->uiTxDataLen = 0;
	pstTTransfer->uiRxDataLen = 0;
	pstTTransfer->uiRxMaskLen = 0;
	pstTTransfer->eTxBufReleaseType = eSPI_Buffer_None;
	pstTTransfer->eRxBufReleaseType = eSPI_Buffer_None;
	vClear_SlaveBusyFLag(pstTTransfer->eSlaveId);
}

bool bCaller_Should_Release_Buffer(eSPIBuffer_ReleaseType_t eReleaseType)
{
	switch(eReleaseType)
	{
    	case eSPI_Buffer_Dynamic_And_Free_ByCaller:
			return true;
		case eSPI_Buffer_Static:
		case eSPI_Buffer_None:
			return false;
		default:
			FHALT("Invalid Buffer release type");
			return false;
	}
}
#endif

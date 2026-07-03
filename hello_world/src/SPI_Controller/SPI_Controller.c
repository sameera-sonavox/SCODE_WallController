#include "SPI_Controller.h"
#include "../Lib/SPI/NXP_SPI_API.h"
#include "../Lib/GenericMacro.h"

sT_SPITransfer_t stTTransfer[eNUMBER_OF_SPI_SLAVEs] = {0};
void SPI_Slave_0_Callback(eSPI_Slave_Id_t eSlaveId, eSPI_TransferResult_t eResult);
void vFree_SPI_TansferBuffers(sT_SPITransfer_t *pstTTransfer);

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
	stSlaveConfig.uiDelay_Between_BlockTx_ns = 1000;
	stSlaveConfig.uiDelay_CS_Assert_To_SCK_ns = 100;
	stSlaveConfig.uiDelay_LastSCK_To_CS_Deassert_ns = 300;
	stSlaveConfig.pvSPI_CallBack = SPI_Slave_0_Callback;

	stSPIConfig.stTSPIModeCtrl.spi_mode.pstSPISlaveHead_Ctrl = pstCreate_SPISlave(&stSlaveConfig);

	
	stSlaveConfig.eSlaveId = eSPI_Slave_1;
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
	}

	stTTransfer[eSPI_Slave_0].eModuleId = eSPI_0;
	stTTransfer[eSPI_Slave_0].eSlaveId = eSPI_Slave_0;
	stTTransfer[eSPI_Slave_0].eType = eTransfer_Tx_Only;
	stTTransfer[eSPI_Slave_0].bShould_CS_Asserted_For_EntireTransfer = true;

	stTTransfer[eSPI_Slave_1].eModuleId = eSPI_0;
	stTTransfer[eSPI_Slave_1].eSlaveId = eSPI_Slave_1;
	stTTransfer[eSPI_Slave_1].eType = eTransfer_Rx_Only;
	stTTransfer[eSPI_Slave_1].bShould_CS_Asserted_For_EntireTransfer = true;

	vInit_SPI(&stSPIConfig);
  
}

bool bSPI_SendData(eSPI_Slave_Id_t eSlaveId, uint8_t *puiTxData, uint8_t uiLen)
{
    sT_SPITransfer_t *pstTTransfer = &stTTransfer[eSlaveId];
    pstTTransfer->puiTxData = puiTxData;
    pstTTransfer->uiTxDataLen = uiLen;
	pstTTransfer->bTxAllocatedInternally = false;
	pstTTransfer->bRxAllocatedInternally = false;
    return bSPI_Transfer_InMasterMode(*pstTTransfer);
}

bool bSPI_ReceiveData(eSPI_Slave_Id_t eSlaveId, uint8_t *puiCMD, uint8_t uiCMDLen, uint8_t uiRxLen)
{	
	sT_SPITransfer_t *pstTTransfer = &stTTransfer[eSlaveId];

	if((puiCMD != NULL && uiCMDLen == 0) || (puiCMD == NULL && uiCMDLen != 0))
	{
		FHALT("Invalid parameters for SPI Slave[%d] Receive Data: puiCMD is not NULL but uiCMDLen is 0", eSlaveId);
		return false;
	}

	if(puiCMD != NULL)
	{
		pstTTransfer->puiTxData = (uint8_t *)malloc(uiCMDLen + uiRxLen);
		if(pstTTransfer->puiTxData == NULL)
		{
			FHALT("Failed to allocate memory for SPI Slave[%d] Tx Data", eSlaveId);
			return false;
		}

		pstTTransfer->uiTxDataLen = uiRxLen + uiCMDLen;
		memcpy(pstTTransfer->puiTxData, puiCMD, uiCMDLen);
		memset(pstTTransfer->puiTxData + uiCMDLen, 0, uiRxLen);
		pstTTransfer->uiRxMaskLen = uiCMDLen;
		pstTTransfer->eType = eTransfer_Transceive;
		pstTTransfer->bTxAllocatedInternally = true;
	}
	else
	{
		pstTTransfer->puiTxData = NULL;
		pstTTransfer->uiTxDataLen = uiRxLen;
		pstTTransfer->uiRxMaskLen = 0;
		pstTTransfer->eType = eTransfer_Rx_Only;
		pstTTransfer->bTxAllocatedInternally = false;
	}

	uint8_t *puiData = (uint8_t *)malloc(pstTTransfer->uiTxDataLen);
	if(puiData == NULL)
	{
		if(pstTTransfer->bTxAllocatedInternally)
		{
			free(pstTTransfer->puiTxData);
			pstTTransfer->puiTxData = NULL;
		}
		FHALT("Failed to allocate memory for SPI Slave[%d] Rx Data", eSlaveId);
		return false;
	}
	
	pstTTransfer->puiRxData = puiData;
	pstTTransfer->uiRxDataLen = pstTTransfer->uiTxDataLen;
	pstTTransfer->bRxAllocatedInternally = true;

	bool bRes = bSPI_Transfer_InMasterMode(*pstTTransfer);
	if(bRes)
		return bRes;

	vFree_SPI_TansferBuffers(pstTTransfer);
	return false;
}

void SPI_Slave_0_Callback(eSPI_Slave_Id_t eSlaveId, eSPI_TransferResult_t eResult)
{
	sT_SPITransfer_t *pstTransfer = &stTTransfer[eSlaveId];

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
	printf("Slave[%d] : Recv.Data Length = %d @Data[0]: %d", eSlaveId, uiRxLen, puiRxData[0]);

	vFree_SPI_TansferBuffers(pstTransfer);
}

void vFree_SPI_TansferBuffers(sT_SPITransfer_t *pstTTransfer)
{
	if(pstTTransfer == NULL)
	{
		FHALT("NULL Pointer Reference");
		return;
	}

	if(pstTTransfer->puiTxData != NULL && pstTTransfer->bTxAllocatedInternally)
	{
		free(pstTTransfer->puiTxData);
		pstTTransfer->puiTxData = NULL;
	}
	if(pstTTransfer->puiRxData != NULL && pstTTransfer->bRxAllocatedInternally)
	{
		free(pstTTransfer->puiRxData);
		pstTTransfer->puiRxData = NULL;
	}

	pstTTransfer->uiTxDataLen = 0;
	pstTTransfer->uiRxDataLen = 0;
	pstTTransfer->uiRxMaskLen = 0;
	pstTTransfer->bTxAllocatedInternally = false;
	pstTTransfer->bRxAllocatedInternally = false;
}
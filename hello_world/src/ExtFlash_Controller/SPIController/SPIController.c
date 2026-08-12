#include "SPIController.h"
#include "../Lib/GenericMacro.h"

#include <zephyr/kernel.h>

static bool bSetup_LPSPI_Configurations( void );
static void vLPSPI1_Callback(eSPI_Slave_Id_t eSlaveId, eSPI_TransferResult_t eResult);

static volatile eSPI_TransferResult_t eLastTransferResult;

K_MUTEX_DEFINE(stSPITransferMutex);
K_SEM_DEFINE(stTSem_SPITransferDone, 0, 1);

bool bInit_LPSPI_ForExtFlash( void )
{
    if(!bSetup_LPSPI_Configurations())
    {
        FHALT("Failed to setup LPSPI1 configurations for External Flash");
        return false;
    }

    return true;
}

static bool bSetup_LPSPI_Configurations( void )
{
    sT_SPIConfig_t stTSPIConfig = {0};

    stTSPIConfig.eModule = eSPI_1;
    stTSPIConfig.bIsOk = false;
    stTSPIConfig.eNotificationType = eNotify_Interrupt;
    stTSPIConfig.eDataOutPinState = eData_Out_TriState;
    stTSPIConfig.ePinConfig = eEn_FullDuplex_Transfer_Normal;

    sT_SPI_Mode_Config_t *pstSPIModeConfig = &stTSPIConfig.stTSPIModeCtrl;
    pstSPIModeConfig->eMode = eSPI_Mode_Controller;

    sT_SPISlave_Config_t stTSPISlaveConfig = {0};
    stTSPISlaveConfig.eSlaveId = eSPI_Slave_0;
    stTSPISlaveConfig.bIs_CS_HWControlled = true;
    stTSPISlaveConfig.bEn_SCKLoopBack_ForSampling = false;
    stTSPISlaveConfig.eCPOLCPH_Ctrl = eCPOL_0_CPH_0;
    stTSPISlaveConfig.eCSPolarityType = eCS_Active_Low;
    stTSPISlaveConfig.eEndianFormat = eMSB_First;
    stTSPISlaveConfig.eHW_PCS_Ctrl = ePCS_0;
    stTSPISlaveConfig.eSPI_BusWidth = e1bit_Transfer;
    stTSPISlaveConfig.pvSPI_CallBack = vLPSPI1_Callback;

    stTSPISlaveConfig.stTHWReadyCtrl.bHWReady_Used = false;

    stTSPISlaveConfig.uiSPI_Freq_Hz = 4000000;
    stTSPISlaveConfig.uiDelay_Between_BlockTx_ns = 100U;
    stTSPISlaveConfig.uiDelay_CS_Assert_To_SCK_ns = 100;
    stTSPISlaveConfig.uiDelay_LastSCK_To_CS_Deassert_ns = 100;
    
    pstSPIModeConfig->spi_mode.pstSPISlaveHead_Ctrl = pstCreate_SPISlave(&stTSPISlaveConfig);
    if(pstSPIModeConfig->spi_mode.pstSPISlaveHead_Ctrl == NULL)
    {
        FHALT("Failed to create SPI Slave Control for External Flash");
        return false;
    }

    vInit_SPI(&stTSPIConfig);

    if(!stTSPIConfig.bIsOk)
    {
        FHALT("Failed to initialize LPSPI1 for External Flash");
        return false;
    }

    return true;
}

static void vLPSPI1_Callback(eSPI_Slave_Id_t eSlaveId, eSPI_TransferResult_t eResult)
{
    eLastTransferResult = eResult;
    k_sem_give(&stTSem_SPITransferDone);
}

bool bSPI_Write(sT_SPIMasterTransfer_t *pstTSPITransfer)
{
    if(pstTSPITransfer == NULL)
    {
        FHALT("Invalid SPI Transfer Pointer");
        return false;
    }

    if(k_mutex_lock(&stSPITransferMutex, K_MSEC(100)) != 0)
    {
        return false;
    }

    k_sem_reset(&stTSem_SPITransferDone);
    eLastTransferResult = eTransfer_Failed;

    bool bRes = bSPI_Transfer_InMasterMode(*pstTSPITransfer);
    if(!bRes)
    {
        k_mutex_unlock(&stSPITransferMutex);
        FHALT("Failed to perform SPI Transfer in Master Mode");
        return false;
    }

    if(k_sem_take(&stTSem_SPITransferDone, K_MSEC(5)) != 0)
    {
        k_mutex_unlock(&stSPITransferMutex);
        FHALT("Timeout waiting for SPI Transfer to complete");
        return false;
    }

    if(eLastTransferResult != eTransfer_Success)
    {
        FHALT("SPI transfer callback result: %d", eLastTransferResult);
        k_mutex_unlock(&stSPITransferMutex);
        return false;
    }

    bRes = true;
    k_mutex_unlock(&stSPITransferMutex);
    return bRes;        
}

bool bSPI_MultiPhaseTranseive(sT_SPIMultiPhase_MasterTransfer_t *pstMultiPhaseTransfer)
{
    if(pstMultiPhaseTransfer == NULL)
    {
        FHALT("Invalid SPI Transfer Pointer");
        return false;
    }

    if(k_mutex_lock(&stSPITransferMutex, K_MSEC(100)) != 0)
    {
        return false;
    }

    k_sem_reset(&stTSem_SPITransferDone);
    eLastTransferResult = eTransfer_Failed;

    bool bRes = bSPI_TransferSequence_InMasterMode(pstMultiPhaseTransfer);
    if(!bRes)
    {
        k_mutex_unlock(&stSPITransferMutex);
        FHALT("Failed to perform SPI Transfer in Master Mode");
        return false;
    }

    if(k_sem_take(&stTSem_SPITransferDone, K_MSEC(200)) != 0)
    {
        k_mutex_unlock(&stSPITransferMutex);
        FHALT("Timeout waiting for SPI Transfer to complete");
        return false;
    }

    bRes = (eLastTransferResult == eTransfer_Success);
    if(!bRes)
    {
        FHALT("SPI multiphase callback result: %d", eLastTransferResult);
    }
    k_mutex_unlock(&stSPITransferMutex);
    return bRes;        
}

bool bSPI_Transceive(sT_SPIMasterTransfer_t *pstTSPITransfer)
{
    if(pstTSPITransfer == NULL)
    {
        FHALT("Invalid SPI Transfer Pointer");
        return false;
    }

    if(k_mutex_lock(&stSPITransferMutex, K_MSEC(100)) != 0)
    {
        return false;
    }

    k_sem_reset(&stTSem_SPITransferDone);
    eLastTransferResult = eTransfer_Failed;

    bool bRes = bSPI_Transfer_InMasterMode(*pstTSPITransfer);
    if(!bRes)
    {
        k_mutex_unlock(&stSPITransferMutex);
        FHALT("Failed to perform SPI Transfer in Master Mode");
        return false;
    }

    if(k_sem_take(&stTSem_SPITransferDone, K_MSEC(200)) != 0)
    {
        k_mutex_unlock(&stSPITransferMutex);
        FHALT("Timeout waiting for SPI Transfer to complete");
        return false;
    }

    bRes = (eLastTransferResult == eTransfer_Success);
    k_mutex_unlock(&stSPITransferMutex);
    return bRes;
}

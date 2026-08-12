#include "ExtFlash_ReadWriteControl.h"

#include "../SPIController/SPIController.h"
#include "../ExtFlash_ControllerTypes.h"
#include "../ExtFlash_Controller.h"
#include "../ExtFlashInitialSetup/ExtFlash_InitialSetupCtrl.h"

#include "../Lib/GenericMacro.h"

#include <errno.h>

#define EXT_FLASH_SECTOR_SIZE_BYTES 4096U

static int iPoll_ExtBusyStatus_Sync(uint32_t uiTimeout_ms);
static void vPrepare_MultiPhaseConfig(sT_SPIMultiPhase_MasterTransfer_t *pstTMultiPhaseCtrl, 
                                      sT_SPITransferPhase_Config_t *pstaPhaseConfig,
                                      const uint8_t *puiCMD_Phase1,
                                      const uint8_t *puiDummy_Phase2,
                                      uint8_t *puiRecvData);

int iWrite_DataToFlash(uint32_t uiAddr, const uint8_t *puiData, size_t uiDataLen)
{
    //Need to validata the Flash address here for the data storage boundaries
    //Validation code must come here

    if(puiData == NULL)
    {
        FHALT("Null pointer reference for the data buffer");
        return -EINVAL;
    }
    if(uiDataLen == 0U)
    {
        FHALT("Invalid data length for the data buffer");
        return -EINVAL;
    }
    if(!bIsExtFlash_SetupSUccess())
    {
        FHALT("ExtFlash: Invalid request while setup is fail");
        return -ENODEV;
    }

    uint32_t uiMemAddr = uiAddr;
    uint32_t uiCurrentBuffIndex = 0U;
    size_t uiWriteLen = uiDataLen;

    uint32_t uiCMDAddrLen = 4U;
    uint8_t uiCMD[260U] = {0};

    sT_SPIMasterTransfer_t stTSPITransfer = {0};
    stTSPITransfer.eModuleId = eSPI_1;
    stTSPITransfer.eSlaveId = eSPI_Slave_0;
    stTSPITransfer.eType = eTransfer_Tx_Only;
    stTSPITransfer.puiRxData = NULL;
    stTSPITransfer.uiRxDataLen = 0U;
    stTSPITransfer.eRxBufReleaseType = eSPI_Buffer_None;
    stTSPITransfer.eTxBufReleaseType = eSPI_Buffer_Static;
    stTSPITransfer.bIsTransferBusy = false;
    stTSPITransfer.bShould_CS_Asserted_For_EntireTransfer = true;
    stTSPITransfer.uiRxMaskLen = 0U;
    
    stTSPITransfer.puiTxData = uiCMD;

    while(uiWriteLen > 0)
    {
        uint16_t uiCurrentWriteLen = 0U;

        uiCMD[0] = (uint8_t)EXT_FLASH_WRITE_CMD;
        uiCMD[1] = (uint8_t)(uiMemAddr >> 16U);
        uiCMD[2] = (uint8_t)(uiMemAddr >> 8U);
        uiCMD[3] = (uint8_t)uiMemAddr;

        uint32_t uiAvailableBytes = 256U - (uiMemAddr & 0xFF);

        if(uiAvailableBytes > uiWriteLen)
        {
            uiCurrentWriteLen = uiWriteLen;
        }
        else
        {
            uiCurrentWriteLen = uiAvailableBytes;
        }

        memcpy(&uiCMD[uiCMDAddrLen], &puiData[uiCurrentBuffIndex], uiCurrentWriteLen);
        stTSPITransfer.uiTxDataLen = uiCurrentWriteLen + uiCMDAddrLen;

        if(!bExtFlash_WriteEnable())
        {
            return -EIO;
        }

        bool bRes = bSPI_Write(&stTSPITransfer);
        if(!bRes)
        {
            FHALT("ExtFlash: Failed to page-program command");
            return -EIO;
        }

        int iResult = iPoll_ExtBusyStatus_Sync(50U);
        if(iResult != 0)
        {
            return iResult;
        }

        uiWriteLen -= uiCurrentWriteLen;
        uiMemAddr += uiCurrentWriteLen;
        uiCurrentBuffIndex += uiCurrentWriteLen;
    }

    return 0;
}

int iErase_Flash_4KB(uint32_t uiAddr)
{
    if((uiAddr % EXT_FLASH_SECTOR_SIZE_BYTES) != 0U)
    {
        FHALT("ExtFlash: Sector erase address 0x%08X is not 4KB aligned", uiAddr);
        return -EINVAL;
    }
    if(!bIsExtFlash_SetupSUccess())
    {
        FHALT("ExtFlash: Invalid request while setup is fail");
        return -ENODEV;
    }

    if(!bExtFlash_WriteEnable())
        return -EIO;

    uint8_t uiCMD[4U] = {0};    

    uiCMD[0] = (uint8_t)EXT_FLASH_4KB_SECTOR_ERASE;
    uiCMD[1] = (uint8_t)(uiAddr >> 16U);
    uiCMD[2] = (uint8_t)(uiAddr >> 8U);
    uiCMD[3] = (uint8_t)uiAddr;
    
    sT_SPIMasterTransfer_t stTSPITransfer = {0};
    stTSPITransfer.eModuleId = eSPI_1;
    stTSPITransfer.eSlaveId = eSPI_Slave_0;
    stTSPITransfer.eType = eTransfer_Tx_Only;
    stTSPITransfer.puiTxData = uiCMD;
    stTSPITransfer.uiTxDataLen = 4U;
    stTSPITransfer.puiRxData = NULL;
    stTSPITransfer.uiRxDataLen = 0U;
    stTSPITransfer.eRxBufReleaseType = eSPI_Buffer_None;
    stTSPITransfer.eTxBufReleaseType = eSPI_Buffer_Static;
    stTSPITransfer.bIsTransferBusy = false;
    stTSPITransfer.bShould_CS_Asserted_For_EntireTransfer = true;
    stTSPITransfer.uiRxMaskLen = 0U;
    
    bool bRes = bSPI_Write(&stTSPITransfer);
    if(!bRes)
    {
        FHALT("ExtFlash: Failed send erase command");
        return -EIO;
    }

    int iResult = iPoll_ExtBusyStatus_Sync(2000U);
    if(iResult != 0)
    {
        return iResult;
    }

    return 0;
}

static int iPoll_ExtBusyStatus_Sync(uint32_t uiTimeout_ms)
{
    uint32_t uiStartTime = k_uptime_get_32();
    while(true)
    {
        bool bIsBusy = false;
        if(!bGet_ExtFlash_Busy(&bIsBusy))
        {
            FHALT("ExtFlash: Failed to read busy status");
            return -EIO;
        }

        if(!bIsBusy)
            break;

        if((k_uptime_get_32() - uiStartTime) >= uiTimeout_ms)
        {
            FHALT("ExtFlash: Flash write/erase timeout error");
            return -ETIMEDOUT;
        }

        k_msleep(2);
    }

    if(!bIsExtFlash_WEL_Cleared())
    {
        FHALT("ExtFlash: WE is not cleared by HW");
        return -EIO;
    }

    return 0;
}

int iRead_DataFromFlash_Normal(uint32_t uiAddr, uint8_t *puiRecvData, size_t uiDataLen)
{
    if(!bIsExtFlash_SetupSUccess())
    {
        FHALT("ExtFlash : Invalid access while initial setup is fail");
        return -ENODEV;
    }
    if(uiDataLen == 0U)
    {
        FHALT("ExtFlash : Invalid read length @Length: %zu", uiDataLen);
        return -EINVAL;
    }
    if(puiRecvData == NULL)
    {
        FHALT("ExtFlash : Null Pointer for received data");
        return -EINVAL;
    }

    //Need to validate the size against the allocated partition size once the wrapper is completed
    uint32_t uiTotLength = 0U, uiCurrentReadLen = 0U, uiMemAddr = uiAddr;
    uint8_t uiCMD[DEFAULT_READ_BUFF_LENGTH + 4U];
    memset(uiCMD, 0, sizeof(uiCMD));

    uint8_t uiRxData[DEFAULT_READ_BUFF_LENGTH + 4U];
    memset(uiRxData, 0, sizeof(uiRxData));

    sT_SPIMasterTransfer_t stTSPITransfer = {0};
    stTSPITransfer.eModuleId = eSPI_1;
    stTSPITransfer.eSlaveId = eSPI_Slave_0;
    stTSPITransfer.eType = eTransfer_Transceive;
    stTSPITransfer.puiTxData = uiCMD;
    stTSPITransfer.puiRxData = uiRxData;
    stTSPITransfer.eRxBufReleaseType = eSPI_Buffer_Static;
    stTSPITransfer.eTxBufReleaseType = eSPI_Buffer_Static;
    stTSPITransfer.bIsTransferBusy = false;
    stTSPITransfer.bShould_CS_Asserted_For_EntireTransfer = true;
    stTSPITransfer.uiRxMaskLen = 4U;

    while(uiTotLength < uiDataLen)
    {        
        uiCurrentReadLen = uiDataLen - uiTotLength;
        uiCurrentReadLen = (uiCurrentReadLen < DEFAULT_READ_BUFF_LENGTH)? uiCurrentReadLen: DEFAULT_READ_BUFF_LENGTH;

        uiCMD[0] = (uint8_t)EXT_FLASH_READ_CMD;
        uiCMD[1] = (uint8_t)(uiMemAddr >> 16U);
        uiCMD[2] = (uint8_t)(uiMemAddr >> 8U);
        uiCMD[3] = (uint8_t)uiMemAddr;

        stTSPITransfer.uiTxDataLen = uiCurrentReadLen + 4U;
        stTSPITransfer.uiRxDataLen = stTSPITransfer.uiTxDataLen;

        bool bRes = bSPI_Transceive(&stTSPITransfer);
        if(!bRes)
        {
            FHALT("Failed to read data from External Flash");
            return -EIO;
        }

        memcpy(&puiRecvData[uiTotLength], &uiRxData[stTSPITransfer.uiRxMaskLen], uiCurrentReadLen);
        uiTotLength += uiCurrentReadLen;
        uiMemAddr += uiCurrentReadLen;        
    }
    
    return 0;
}

int iRead_DataFromFlash_Quad(uint32_t uiAddr, uint8_t *puiRecvData, size_t uiDataLen)
{
    if(!bIsExtFlash_SetupSUccess())
    {
        FHALT("ExtFlash : Invalid access while initial setup is fail");
        return -ENODEV;
    }
    if(uiDataLen == 0U)
    {
        FHALT("ExtFlash : Invalid read length @Length: %zu", uiDataLen);
        return -EINVAL;
    }
    if(puiRecvData == NULL)
    {
        FHALT("ExtFlash : Null Pointer for received data");
        return -EINVAL;
    }

    //Need to validate the size against the allocated partition size once the wrapper is completed
    size_t uiTotalReadLength = 0U;
    while(uiTotalReadLength < uiDataLen)
    {
        size_t uiCurrentReadLength = uiDataLen - uiTotalReadLength;
        if(uiCurrentReadLength > QUAD_READ_MAX_TRANSACTION_LENGTH)
        {
            uiCurrentReadLength = QUAD_READ_MAX_TRANSACTION_LENGTH;
        }

        uint32_t uiCurrentAddress = uiAddr + (uint32_t)uiTotalReadLength;
        uint8_t uiaCMD_FirstPhase[4] = {
            EXT_FLASH_QUAD_READ_CMD,
            (uint8_t)(uiCurrentAddress >> 16U),
            (uint8_t)(uiCurrentAddress >> 8U),
            (uint8_t)uiCurrentAddress
        };
        uint8_t uiaDummyData[1] = {0};
        sT_SPITransferPhase_Config_t staConfigArray[3] = {0};
        sT_SPIMultiPhase_MasterTransfer_t stTMultiPhaseCtrl = {
            .pstPhase = staConfigArray,
            .uiPhaseCount = ARRAY_SIZE(staConfigArray)
        };

        vPrepare_MultiPhaseConfig(&stTMultiPhaseCtrl,
                                  staConfigArray,
                                  uiaCMD_FirstPhase,
                                  uiaDummyData,
                                  &puiRecvData[uiTotalReadLength]);
        staConfigArray[0].uiDataLen = sizeof(uiaCMD_FirstPhase);
        staConfigArray[1].uiDataLen = sizeof(uiaDummyData);
        staConfigArray[2].uiDataLen = uiCurrentReadLength;

        if(!bSPI_MultiPhaseTranseive(&stTMultiPhaseCtrl))
        {
            FHALT("ExtFlash: Quad read failed @0x%08X, length: %zu",
                  uiCurrentAddress,
                  uiCurrentReadLength);
            return -EIO;
        }

        uiTotalReadLength += uiCurrentReadLength;
    }

    return 0;
}

static void vPrepare_MultiPhaseConfig(sT_SPIMultiPhase_MasterTransfer_t *pstTMultiPhaseCtrl, 
                                      sT_SPITransferPhase_Config_t *pstaPhaseConfig,
                                      const uint8_t *puiCMD_Phase1,
                                      const uint8_t *puiDummy_Phase2,
                                      uint8_t *puiRecvData)
{
    pstTMultiPhaseCtrl->eModuleId = eSPI_1;
    pstTMultiPhaseCtrl->eSlaveId = eSPI_Slave_0;
    pstTMultiPhaseCtrl->bKeepCSAssertedBetweenPhases = true;
    
    sT_SPITransferPhase_Config_t *pstPhaseConfig = &pstaPhaseConfig[0];
    pstPhaseConfig->eType = eTransfer_Tx_Only;
    pstPhaseConfig->eWidth = e1bit_Transfer;
    pstPhaseConfig->puiRxData = NULL;
    pstPhaseConfig->puiTxData = puiCMD_Phase1;
    pstPhaseConfig->eRxBufReleaseType = eSPI_Buffer_None;
    pstPhaseConfig->eTxBufReleaseType = eSPI_Buffer_Static;

    pstPhaseConfig = &pstaPhaseConfig[1];
    pstPhaseConfig->eType = eTransfer_Tx_Only;
    pstPhaseConfig->eWidth = e1bit_Transfer;
    pstPhaseConfig->puiRxData = NULL;
    pstPhaseConfig->puiTxData = puiDummy_Phase2;
    pstPhaseConfig->eRxBufReleaseType = eSPI_Buffer_None;
    pstPhaseConfig->eTxBufReleaseType = eSPI_Buffer_Static;

    pstPhaseConfig = &pstaPhaseConfig[2];
    pstPhaseConfig->eType = eTransfer_Rx_Only;
    pstPhaseConfig->eWidth = e4bit_Transfer;
    pstPhaseConfig->puiRxData = puiRecvData;
    pstPhaseConfig->puiTxData = NULL;
    pstPhaseConfig->eRxBufReleaseType = eSPI_Buffer_Static;
    pstPhaseConfig->eTxBufReleaseType = eSPI_Buffer_None;
}

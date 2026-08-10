#include "ExtFlash_InitialSetupCtrl.h"
#include "../SPIController/SPIController.h"
#include "../ExtFlash_ControllerTypes.h"

#include "../Lib/GenericMacro.h"

#ifdef DEBUG_EXTFLASH_SETUP
    #define Print_ExtFlashSetup         printf
#else
    #define Print_ExtFlashSetup(...)
#endif

ExtFlash_StatusReg Flash_StatusReg;
ExtFlash_ConfigReg1 FlashConfigReg1;
ExtFlash_ConfigReg2 FlashConfigReg2;

static bool bGet_JEDEC_ID( void );
static bool bRead_Status_Register( void );
static bool bRead_Config_Registers( void );
static bool bPerform_SFDPMode( void );

static bool bExtFlash_Validate_WREnableDisable( void );

//Inline Functions
static inline bool bIsWrite_Enabled( void );

bool bExec_Flash_InitialSetup( void )
{
    if(!bGet_JEDEC_ID())
        return false;
    if(!bRead_Status_Register())
        return false;
    if(!bRead_Config_Registers())
        return false;
    if(!bExtFlash_Validate_WREnableDisable())
        return false;
    if(!bPerform_SFDPMode())
        return false;

    return true;
}

static bool bPerform_SFDPMode( void )
{
    uint8_t uiCMD[9] = {0}; // SFDP command, 3-byte address, dummy byte, and 4 data clocks
    uint8_t uiRxData[9] = {0};

    //Command Setup
    uint32_t uiAddr = (uint32_t)SFDP_MODE_ADDRESS;
    uiCMD[0] = (uint8_t)SFDP_MODE_CMD;
    uiCMD[1] = (uint8_t)(uiAddr >> 16U);
    uiCMD[2] = (uint8_t)(uiAddr >> 8U);
    uiCMD[3] = (uint8_t)(uiAddr);

    sT_SPIMasterTransfer_t stTSPITransfer = {0};
    stTSPITransfer.eModuleId = eSPI_1;
    stTSPITransfer.eSlaveId = eSPI_Slave_0;
    stTSPITransfer.eType = eTransfer_Transceive;
    stTSPITransfer.puiTxData = uiCMD;
    stTSPITransfer.uiTxDataLen = sizeof(uiCMD);
    stTSPITransfer.puiRxData = uiRxData;
    stTSPITransfer.uiRxDataLen = sizeof(uiRxData);
    stTSPITransfer.eRxBufReleaseType = eSPI_Buffer_Static;
    stTSPITransfer.eTxBufReleaseType = eSPI_Buffer_Static;
    stTSPITransfer.bIsTransferBusy = false;
    stTSPITransfer.bShould_CS_Asserted_For_EntireTransfer = true;
    stTSPITransfer.uiRxMaskLen = 5U;
    
    bool bRes = bSPI_Transceive(&stTSPITransfer);
    if(!bRes)
    {
        FHALT("Failed to get SFDP Response from External Flash");
        return false;
    }

    if(uiRxData[stTSPITransfer.uiRxMaskLen] == (uint8_t)SFDP_RECV_BYTE0 &&
       uiRxData[stTSPITransfer.uiRxMaskLen + 1U] == (uint8_t)SFDP_RECV_BYTE1 &&
       uiRxData[stTSPITransfer.uiRxMaskLen + 2U] == (uint8_t)SFDP_RECV_BYTE2 &&
       uiRxData[stTSPITransfer.uiRxMaskLen + 3U] == (uint8_t)SFDP_RECV_BYTE3)
    {
        return true;
    }

    FHALT("Invalid SFDP Response: 0x%02X 0x%02X 0x%02X 0x%02X\n",
          uiRxData[stTSPITransfer.uiRxMaskLen],
          uiRxData[stTSPITransfer.uiRxMaskLen + 1U],
          uiRxData[stTSPITransfer.uiRxMaskLen + 2U],
          uiRxData[stTSPITransfer.uiRxMaskLen + 3U]);
    return false;
}

static bool bExtFlash_Validate_WREnableDisable( void )
{
    if(!bExtFlash_WriteEnable())
        return false;
    if(!bExtFlash_WriteDisable())
        return false;

    Print_ExtFlashSetup("ExtFlash: Write Enable/Disable Validation Pass\n\r");
    return true;
}

bool bExtFlash_WriteEnable( void )
{
    uint8_t uiCMD[1] = {(uint8_t)WRITE_ENABLE_CMD};

    sT_SPIMasterTransfer_t stTSPITransfer = {0};
    stTSPITransfer.eModuleId = eSPI_1;
    stTSPITransfer.eSlaveId = eSPI_Slave_0;
    stTSPITransfer.eType = eTransfer_Tx_Only;
    stTSPITransfer.puiTxData = uiCMD;
    stTSPITransfer.uiTxDataLen = 1U;
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
        FHALT("Failed to enable 'Write' on Ext Flash");
        return false;
    }
    
    if(!bRead_Status_Register())
        return false;
    if(!bIsWrite_Enabled())
        return false;

    Print_ExtFlashSetup("ExtFlash: Write Enable @StatusReg: 0x%02X\n\r", Flash_StatusReg.uiRegVal);    
    return true;
}

bool bExtFlash_WriteDisable( void )
{
    uint8_t uiCMD[1] = {(uint8_t)WRITE_DISABLE_CMD};

    sT_SPIMasterTransfer_t stTSPITransfer = {0};
    stTSPITransfer.eModuleId = eSPI_1;
    stTSPITransfer.eSlaveId = eSPI_Slave_0;
    stTSPITransfer.eType = eTransfer_Tx_Only;
    stTSPITransfer.puiTxData = uiCMD;
    stTSPITransfer.uiTxDataLen = 1U;
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
        FHALT("Failed to disable 'Write' on Ext Flash");
        return false;
    }
    
    if(!bRead_Status_Register())
        return false;
    if(bIsWrite_Enabled())
        return false;

    Print_ExtFlashSetup("ExtFlash: Write Disable @StatusReg: 0x%02X\n\r", Flash_StatusReg.uiRegVal);    
    return true;
}

static bool bRead_Config_Registers( void )
{
    uint8_t uiCMD[3] = {0};
    uiCMD[0] = (uint8_t)CONFIG_REG_READ_CMD;

    uint8_t uiRxData[3] = {0};    

    sT_SPIMasterTransfer_t stTSPITransfer = {0};
    stTSPITransfer.eModuleId = eSPI_1;
    stTSPITransfer.eSlaveId = eSPI_Slave_0;
    stTSPITransfer.eType = eTransfer_Transceive;
    stTSPITransfer.puiTxData = uiCMD;
    stTSPITransfer.uiTxDataLen = sizeof(uiCMD);
    stTSPITransfer.puiRxData = uiRxData;
    stTSPITransfer.uiRxDataLen = sizeof(uiRxData);
    stTSPITransfer.eRxBufReleaseType = eSPI_Buffer_Static;
    stTSPITransfer.eTxBufReleaseType = eSPI_Buffer_Static;
    stTSPITransfer.bIsTransferBusy = false;
    stTSPITransfer.bShould_CS_Asserted_For_EntireTransfer = true;
    stTSPITransfer.uiRxMaskLen = 1U;
    
    bool bRes = bSPI_Transceive(&stTSPITransfer);
    if(!bRes)
    {
        FHALT("Failed to read config register");
        return false;
    }
    
    memcpy(&FlashConfigReg1, &uiRxData[stTSPITransfer.uiRxMaskLen], sizeof(FlashConfigReg1.uiRegVal));
    memcpy(&FlashConfigReg2, &uiRxData[stTSPITransfer.uiRxMaskLen + 1U], sizeof(FlashConfigReg2.uiRegVal));    
    return true;
    
}

static bool bRead_Status_Register( void )
{
    uint8_t uiCMD[2] = {0};
    uiCMD[0] = (uint8_t)STATUS_REG_READ_CMD;

    uint8_t uiRxData[2] = {0};    

    sT_SPIMasterTransfer_t stTSPITransfer = {0};
    stTSPITransfer.eModuleId = eSPI_1;
    stTSPITransfer.eSlaveId = eSPI_Slave_0;
    stTSPITransfer.eType = eTransfer_Transceive;
    stTSPITransfer.puiTxData = uiCMD;
    stTSPITransfer.uiTxDataLen = sizeof(uiCMD);
    stTSPITransfer.puiRxData = uiRxData;
    stTSPITransfer.uiRxDataLen = sizeof(uiRxData);
    stTSPITransfer.eRxBufReleaseType = eSPI_Buffer_Static;
    stTSPITransfer.eTxBufReleaseType = eSPI_Buffer_Static;
    stTSPITransfer.bIsTransferBusy = false;
    stTSPITransfer.bShould_CS_Asserted_For_EntireTransfer = true;
    stTSPITransfer.uiRxMaskLen = 1U;
    
    bool bRes = bSPI_Transceive(&stTSPITransfer);
    if(!bRes)
    {
        FHALT("Failed to read status register");
        return false;
    }

    memcpy(&Flash_StatusReg, &uiRxData[stTSPITransfer.uiRxMaskLen], sizeof(Flash_StatusReg.uiRegVal));
    return true;
}

static bool bGet_JEDEC_ID( void )
{
    uint8_t uiCMD[4] = {(uint8_t)JEDEC_ID_REQ_CMD, 0xFF, 0xFF, 0xFF}; // JEDEC ID command followed by 3 dummy bytes
    uint8_t uiRxData[4] = {0};

    sT_SPIMasterTransfer_t stTSPITransfer = {0};
    stTSPITransfer.eModuleId = eSPI_1;
    stTSPITransfer.eSlaveId = eSPI_Slave_0;
    stTSPITransfer.eType = eTransfer_Transceive;
    stTSPITransfer.puiTxData = uiCMD;
    stTSPITransfer.uiTxDataLen = sizeof(uiCMD);
    stTSPITransfer.puiRxData = uiRxData;
    stTSPITransfer.uiRxDataLen = sizeof(uiRxData);
    stTSPITransfer.eRxBufReleaseType = eSPI_Buffer_Static;
    stTSPITransfer.eTxBufReleaseType = eSPI_Buffer_Static;
    stTSPITransfer.bIsTransferBusy = false;
    stTSPITransfer.bShould_CS_Asserted_For_EntireTransfer = true;
    stTSPITransfer.uiRxMaskLen = 1U;
    
    bool bRes = bSPI_Transceive(&stTSPITransfer);
    if(!bRes)
    {
        FHALT("Failed to get JEDEC ID from External Flash");
        return false;
    }

    if(uiRxData[1] == JEDEC_ID_0 && 
       uiRxData[2] == JEDEC_ID_1 && 
       uiRxData[3] == JEDEC_ID_2)
       return true;

    FHALT("Invalid JEDEC ID: 0x%02X 0x%02X 0x%02X\n", uiRxData[1], uiRxData[2], uiRxData[3]);
    return false;
}

bool bGet_ExtFlash_Busy(bool *pbIsBusy)
{
    if(pbIsBusy == NULL)
    {
        FHALT("ExtFlash: Null pointer for busy status");
        return false;
    }

    bool bRes = bRead_Status_Register();
    if(!bRes)
        return false;

    *pbIsBusy = (Flash_StatusReg.bits.WIP == 1U);
    return true;
}

bool bIsExtFlash_WEL_Cleared( void )
{
    bool bRes = bRead_Status_Register();
    if(!bRes)
        return false;
    return (Flash_StatusReg.bits.WEL == 0U);
}

#pragma region Inline Functions for Register Access

static inline bool bIsWrite_Enabled( void )
{
    return (Flash_StatusReg.bits.WEL == 1U);
}

#pragma endregion

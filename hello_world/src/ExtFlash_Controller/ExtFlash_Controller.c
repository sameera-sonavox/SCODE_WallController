#include <zephyr/kernel.h>
#include "ExtFlash_Controller.h"

#include "../Lib/GenericMacro.h"
#include "../Lib/SPI/NXP_SPI_API.h"
#include "SPIController/SPIController.h"
#include "ExtFlashInitialSetup/ExtFlash_InitialSetupCtrl.h"
#include "ExtFlashReadWrite/ExtFlash_ReadWriteControl.h"
#include "ExtFlash_ControllerTypes.h"

#if EXT_FLASH_RUN_BULK_TEST

#define EXT_FLASH_BULK_TEST_SIZE_BYTES          (64U * 1024U)
#define EXT_FLASH_BULK_TEST_CHUNK_SIZE_BYTES    4096U
#define EXT_FLASH_BULK_TEST_SECTOR_SIZE_BYTES   4096U
#define EXT_FLASH_BULK_TEST_START_ADDRESS       \
    (DATA_PARTITION_END_ADDRESS - EXT_FLASH_BULK_TEST_SIZE_BYTES)

BUILD_ASSERT(EXT_FLASH_BULK_TEST_SIZE_BYTES <= DATA_PARTITION_SIZE_BYTES,
             "External flash bulk test exceeds the data partition");
BUILD_ASSERT((EXT_FLASH_BULK_TEST_START_ADDRESS % EXT_FLASH_BULK_TEST_SECTOR_SIZE_BYTES) == 0U,
             "External flash bulk test address must be sector aligned");
BUILD_ASSERT((EXT_FLASH_BULK_TEST_SIZE_BYTES % EXT_FLASH_BULK_TEST_CHUNK_SIZE_BYTES) == 0U,
             "External flash bulk test size must be a whole number of chunks");

#endif

sT_ExtFlash_Control_t stTExtFlashCtrl = {0};

#if EXT_FLASH_RUN_BULK_TEST

static uint8_t uiaExtFlashBulkTestBuffer[EXT_FLASH_BULK_TEST_CHUNK_SIZE_BYTES];

#endif

static inline void vSet_SetupSuccess_Flag( void );
static inline void vClear_SetupSuccess_Flag( void );

#if EXT_FLASH_RUN_BULK_TEST

static uint8_t uiGet_BulkTestPatternByte(size_t uiOffset);
static void vFill_BulkTestBuffer(size_t uiOffset);
static bool bVerify_BulkTestBuffer(size_t uiOffset, const char *pcReadType);
static bool bTest_Flash_Access( void );

#endif

bool bInit_ExtFlash( void )
{
    bool bResult = bInit_LPSPI_ForExtFlash();
    if(!bResult)
    {
        vClear_SetupSuccess_Flag();
        FHALT("Failed to initialize LPSPI1 for External Flash");
        return false;
    }

    if(!bExec_Flash_InitialSetup())
    {
        vClear_SetupSuccess_Flag();
        return false;
    }

    vSet_SetupSuccess_Flag();

#if EXT_FLASH_RUN_BULK_TEST
    for(uint8_t j = 0; j < 50; j++)
    {
        if(!bTest_Flash_Access())
        {
            FHALT("Flash Test Failed");
            return false;
        }
    }
#endif

    return true;
}

#if EXT_FLASH_RUN_BULK_TEST

static bool bTest_Flash_Access( void )
{
    uint32_t uiStartTime = k_uptime_get_32();

    for(size_t uiOffset = 0U;
        uiOffset < EXT_FLASH_BULK_TEST_SIZE_BYTES;
        uiOffset += EXT_FLASH_BULK_TEST_SECTOR_SIZE_BYTES)
    {
        uint32_t uiAddress = EXT_FLASH_BULK_TEST_START_ADDRESS + (uint32_t)uiOffset;
        int iResult = iErase_Flash_4KB(uiAddress);

        if(iResult < 0)
        {
            printf("ExtFlash bulk test: erase failed @0x%08X: %d\n\r",
                   uiAddress,
                   iResult);
            return false;
        }
    }

    uint32_t uiEraseTime_ms = k_uptime_get_32() - uiStartTime;
    uiStartTime = k_uptime_get_32();

    for(size_t uiOffset = 0U;
        uiOffset < EXT_FLASH_BULK_TEST_SIZE_BYTES;
        uiOffset += EXT_FLASH_BULK_TEST_CHUNK_SIZE_BYTES)
    {
        uint32_t uiAddress = EXT_FLASH_BULK_TEST_START_ADDRESS + (uint32_t)uiOffset;
        vFill_BulkTestBuffer(uiOffset);

        int iResult = iWrite_DataToFlash(uiAddress,
                                         uiaExtFlashBulkTestBuffer,
                                         sizeof(uiaExtFlashBulkTestBuffer));
        if(iResult < 0)
        {
            printf("ExtFlash bulk test: write failed @0x%08X: %d\n\r",
                   uiAddress,
                   iResult);
            return false;
        }
    }

    uint32_t uiWriteTime_ms = k_uptime_get_32() - uiStartTime;
    uiStartTime = k_uptime_get_32();

    for(size_t uiOffset = 0U;
        uiOffset < EXT_FLASH_BULK_TEST_SIZE_BYTES;
        uiOffset += EXT_FLASH_BULK_TEST_CHUNK_SIZE_BYTES)
    {
        uint32_t uiAddress = EXT_FLASH_BULK_TEST_START_ADDRESS + (uint32_t)uiOffset;
        int iResult = iRead_DataFromFlash_Normal(uiAddress,
                                                 uiaExtFlashBulkTestBuffer,
                                                 sizeof(uiaExtFlashBulkTestBuffer));
        if(iResult < 0)
        {
            printf("ExtFlash bulk test: normal read failed @0x%08X: %d\n\r",
                   uiAddress,
                   iResult);
            return false;
        }

        if(!bVerify_BulkTestBuffer(uiOffset, "normal"))
        {
            return false;
        }
    }

    uint32_t uiNormalReadTime_ms = k_uptime_get_32() - uiStartTime;
    uiStartTime = k_uptime_get_32();

    for(size_t uiOffset = 0U;
        uiOffset < EXT_FLASH_BULK_TEST_SIZE_BYTES;
        uiOffset += EXT_FLASH_BULK_TEST_CHUNK_SIZE_BYTES)
    {
        uint32_t uiAddress = EXT_FLASH_BULK_TEST_START_ADDRESS + (uint32_t)uiOffset;
        int iResult = iRead_DataFromFlash_Quad(uiAddress,
                                               uiaExtFlashBulkTestBuffer,
                                               sizeof(uiaExtFlashBulkTestBuffer));
        if(iResult < 0)
        {
            printf("ExtFlash bulk test: quad read failed @0x%08X: %d\n\r",
                   uiAddress,
                   iResult);
            return false;
        }

        if(!bVerify_BulkTestBuffer(uiOffset, "quad"))
        {
            return false;
        }
    }

    uint32_t uiQuadReadTime_ms = k_uptime_get_32() - uiStartTime;

    printf("ExtFlash bulk test passed: %u KiB, erase=%lu ms, write=%lu ms, normal=%lu ms, quad=%lu ms\n\r",
           EXT_FLASH_BULK_TEST_SIZE_BYTES / 1024U,
           (unsigned long)uiEraseTime_ms,
           (unsigned long)uiWriteTime_ms,
           (unsigned long)uiNormalReadTime_ms,
           (unsigned long)uiQuadReadTime_ms);
    return true;
}

static uint8_t uiGet_BulkTestPatternByte(size_t uiOffset)
{
    uint32_t uiValue = (uint32_t)uiOffset;
    uiValue ^= uiValue >> 7U;
    uiValue *= 0x45D9F3BU;
    uiValue ^= uiValue >> 11U;
    return (uint8_t)(uiValue ^ (uiValue >> 8U) ^ (uiValue >> 16U));
}

static void vFill_BulkTestBuffer(size_t uiOffset)
{
    for(size_t i = 0U; i < sizeof(uiaExtFlashBulkTestBuffer); i++)
    {
        uiaExtFlashBulkTestBuffer[i] = uiGet_BulkTestPatternByte(uiOffset + i);
    }
}

static bool bVerify_BulkTestBuffer(size_t uiOffset, const char *pcReadType)
{
    for(size_t i = 0U; i < sizeof(uiaExtFlashBulkTestBuffer); i++)
    {
        uint8_t uiExpected = uiGet_BulkTestPatternByte(uiOffset + i);

        if(uiaExtFlashBulkTestBuffer[i] != uiExpected)
        {
            uint32_t uiAddress = EXT_FLASH_BULK_TEST_START_ADDRESS +
                                 (uint32_t)(uiOffset + i);

            printf("ExtFlash bulk test: %s mismatch @0x%08X: expected 0x%02X, received 0x%02X\n\r",
                   pcReadType,
                   uiAddress,
                   uiExpected,
                   uiaExtFlashBulkTestBuffer[i]);
            return false;
        }
    }

    return true;
}

#endif

bool bIsExtFlash_SetupSUccess( void )
{
    return stTExtFlashCtrl.bIsExtFlashSetupSuccess;
}

static inline void vSet_SetupSuccess_Flag( void )
{
    stTExtFlashCtrl.bIsExtFlashSetupSuccess = true;
}

static inline void vClear_SetupSuccess_Flag( void )
{
    stTExtFlashCtrl.bIsExtFlashSetupSuccess = false;
}

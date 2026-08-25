#include "Flash_Memory_ValidateTest.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include "Flash_Memory_ValidateTest_ProjDef.h"
#include "../ExtFlashReadWrite/ExtFlash_ReadWriteControl.h"

#if EXT_FLASH_ENABLE_VALIDATION_TESTS && \
    EXT_FLASH_ANY_VALIDATION_TEST_ENABLED

#if EXT_FLASH_DATA_VALIDATION_TEST_ENABLED

BUILD_ASSERT(EXT_FLASH_BULK_TEST_SIZE_BYTES <= DATA_PARTITION_SIZE_BYTES,
             "External flash validation area exceeds the data partition");
BUILD_ASSERT((EXT_FLASH_VALIDATION_AREA_START_ADDRESS %
              EXT_FLASH_BULK_TEST_SECTOR_SIZE_BYTES) == 0U,
             "External flash validation area must be sector aligned");
BUILD_ASSERT((EXT_FLASH_BULK_TEST_SIZE_BYTES %
              EXT_FLASH_BULK_TEST_CHUNK_SIZE_BYTES) == 0U,
             "External flash bulk test size must be a whole number of chunks");
BUILD_ASSERT(EXT_FLASH_BULK_TEST_CHUNK_SIZE_BYTES <=
             EXT_FLASH_VALIDATION_BUFFER_SIZE_BYTES,
             "External flash bulk chunk exceeds the validation buffer");
BUILD_ASSERT(EXT_FLASH_VALIDATION_FAILURE_DUMP_LENGTH <=
             EXT_FLASH_VALIDATION_BUFFER_SIZE_BYTES,
             "External flash failure dump exceeds the validation buffer");

typedef struct
{
    uint32_t uiAddressOffset;
    size_t uiDataLength;
} sT_FlashValidationCase_t;

static uint8_t uiaExtFlashValidationBuffer[
    EXT_FLASH_VALIDATION_BUFFER_SIZE_BYTES];

#if EXT_FLASH_PATTERN_VALIDATION_ENABLED
static uint8_t uiGet_TestPatternByte(size_t uiOffset);
static void vFill_TestBuffer(size_t uiOffset, size_t uiDataLength);
static bool bWrite_TestPattern(uint32_t uiAddress, size_t uiDataLength);
static bool bVerify_TestBuffer(size_t uiPatternOffset,
                               size_t uiDataLength,
                               const char *pcTestName,
                               const char *pcReadType);
#endif
static void vFill_TestBufferWithCanary( void );
static bool bErase_TestRange(uint32_t uiAddress, size_t uiDataLength);
#if EXT_FLASH_ENABLE_ERASED_DATA_TEST
static bool bVerify_FixedValueBuffer(size_t uiDataLength,
                                     uint8_t uiExpectedValue,
                                     const char *pcTestName,
                                     const char *pcReadType);
#endif
static bool bVerify_CanaryTail(size_t uiDataLength,
                               const char *pcTestName);
#if EXT_FLASH_NORMAL_READ_VALIDATION_ENABLED
static bool bReadAndVerify_Normal(uint32_t uiAddress,
                                  size_t uiPatternOffset,
                                  size_t uiDataLength,
                                  const char *pcTestName);
#endif
#if EXT_FLASH_QUAD_PATTERN_VALIDATION_ENABLED
static bool bReadAndVerify_Quad(uint32_t uiAddress,
                                size_t uiPatternOffset,
                                size_t uiDataLength,
                                const char *pcTestName,
                                bool bCheckCanary);
#endif

#if EXT_FLASH_ENABLE_BULK_ACCESS_TEST
static bool bTest_BulkFlashAccess( void );
#endif
#if EXT_FLASH_ENABLE_QUAD_PARTIAL_FRAME_TEST
static bool bTest_QuadRead_PartialFrames( void );
#endif
#if EXT_FLASH_ENABLE_UNALIGNED_ADDRESS_TEST
static bool bTest_UnalignedStartingAddresses( void );
#endif
#if EXT_FLASH_ENABLE_PAGE_BOUNDARY_WRITE_TEST
static bool bTest_PageBoundaryWrites( void );
#endif
#if EXT_FLASH_ENABLE_QUAD_TRANSACTION_BOUNDARY_TEST
static bool bTest_QuadTransactionBoundaries( void );
#endif
#if EXT_FLASH_ENABLE_NORMAL_READ_BOUNDARY_TEST
static bool bTest_NormalReadBoundaries( void );
#endif
#if EXT_FLASH_ENABLE_ERASED_DATA_TEST
static bool bTest_ErasedData( void );
#endif

#endif

#if EXT_FLASH_ENABLE_INVALID_INPUT_TEST
static bool bExpect_InvalidInputResult(const char *pcCaseName,
                                       int iActualResult);
static bool bTest_InvalidInputs( void );
#endif

#endif

bool bRun_FlashMemory_ValidationTests( void )
{

#if EXT_FLASH_ENABLE_VALIDATION_TESTS && \
    EXT_FLASH_ANY_VALIDATION_TEST_ENABLED
    for(uint8_t uiIteration = 0U;
        uiIteration < EXT_FLASH_VALIDATION_REPEAT_COUNT;
        uiIteration++)
    {
#if EXT_FLASH_ENABLE_INVALID_INPUT_TEST
        if(!bTest_InvalidInputs())
        {
            return false;
        }
#endif

#if EXT_FLASH_ENABLE_ERASED_DATA_TEST
        if(!bTest_ErasedData())
        {
            return false;
        }
#endif

#if EXT_FLASH_ENABLE_NORMAL_READ_BOUNDARY_TEST
        if(!bTest_NormalReadBoundaries())
        {
            return false;
        }
#endif

#if EXT_FLASH_ENABLE_UNALIGNED_ADDRESS_TEST
        if(!bTest_UnalignedStartingAddresses())
        {
            return false;
        }
#endif

#if EXT_FLASH_ENABLE_PAGE_BOUNDARY_WRITE_TEST
        if(!bTest_PageBoundaryWrites())
        {
            return false;
        }
#endif

#if EXT_FLASH_ENABLE_QUAD_TRANSACTION_BOUNDARY_TEST
        if(!bTest_QuadTransactionBoundaries())
        {
            return false;
        }
#endif

#if EXT_FLASH_ENABLE_QUAD_PARTIAL_FRAME_TEST
        if(!bTest_QuadRead_PartialFrames())
        {
            return false;
        }
#endif

#if EXT_FLASH_ENABLE_BULK_ACCESS_TEST
        if(!bTest_BulkFlashAccess())
        {
            return false;
        }
#endif
    }
#endif
    return true;
}



#if EXT_FLASH_ENABLE_VALIDATION_TESTS && \
    EXT_FLASH_ANY_VALIDATION_TEST_ENABLED

#if EXT_FLASH_DATA_VALIDATION_TEST_ENABLED

#if EXT_FLASH_PATTERN_VALIDATION_ENABLED
static uint8_t uiGet_TestPatternByte(size_t uiOffset)
{
    uint32_t uiValue = (uint32_t)uiOffset;

    uiValue ^= uiValue >> 7U;
    uiValue *= 0x45D9F3BU;
    uiValue ^= uiValue >> 11U;

    return (uint8_t)(uiValue ^
                     (uiValue >> 8U) ^
                     (uiValue >> 16U));
}

static void vFill_TestBuffer(size_t uiOffset, size_t uiDataLength)
{
    for(size_t i = 0U; i < uiDataLength; i++)
    {
        uiaExtFlashValidationBuffer[i] =
            uiGet_TestPatternByte(uiOffset + i);
    }
}
#endif

static void vFill_TestBufferWithCanary( void )
{
    memset(uiaExtFlashValidationBuffer,
           EXT_FLASH_VALIDATION_CANARY_VALUE,
           sizeof(uiaExtFlashValidationBuffer));
}

static bool bErase_TestRange(uint32_t uiAddress, size_t uiDataLength)
{
    uint32_t uiFirstSectorAddress =
        uiAddress & ~(EXT_FLASH_BULK_TEST_SECTOR_SIZE_BYTES - 1U);
    uint32_t uiLastAddress = uiAddress + (uint32_t)uiDataLength - 1U;
    uint32_t uiLastSectorAddress =
        uiLastAddress & ~(EXT_FLASH_BULK_TEST_SECTOR_SIZE_BYTES - 1U);

    for(uint32_t uiSectorAddress = uiFirstSectorAddress;
        uiSectorAddress <= uiLastSectorAddress;
        uiSectorAddress += EXT_FLASH_BULK_TEST_SECTOR_SIZE_BYTES)
    {
        int iResult = iErase_Flash_4KB(uiSectorAddress);

        if(iResult < 0)
        {
            printf("ExtFlash validation: erase failed @0x%08X: %d\n\r",
                   uiSectorAddress,
                   iResult);
            return false;
        }
    }

    return true;
}

#if EXT_FLASH_PATTERN_VALIDATION_ENABLED
static bool bWrite_TestPattern(uint32_t uiAddress, size_t uiDataLength)
{
    size_t uiPatternOffset =
        (size_t)(uiAddress - EXT_FLASH_VALIDATION_AREA_START_ADDRESS);

    vFill_TestBuffer(uiPatternOffset, uiDataLength);

    int iResult = iWrite_DataToFlash(uiAddress,
                                     uiaExtFlashValidationBuffer,
                                     uiDataLength);
    if(iResult < 0)
    {
        printf("ExtFlash validation: write failed @0x%08X, length=%zu: %d\n\r",
               uiAddress,
               uiDataLength,
               iResult);
        return false;
    }

    return true;
}

static bool bVerify_TestBuffer(size_t uiPatternOffset,
                               size_t uiDataLength,
                               const char *pcTestName,
                               const char *pcReadType)
{
    for(size_t i = 0U; i < uiDataLength; i++)
    {
        uint8_t uiExpected = uiGet_TestPatternByte(uiPatternOffset + i);

        if(uiaExtFlashValidationBuffer[i] != uiExpected)
        {
            printf("ExtFlash %s: %s mismatch @%zu: expected 0x%02X, received 0x%02X\n\r",
                   pcTestName,
                   pcReadType,
                   i,
                   uiExpected,
                   uiaExtFlashValidationBuffer[i]);

            size_t uiDumpLength = MIN(
                (size_t)EXT_FLASH_VALIDATION_FAILURE_DUMP_LENGTH,
                uiDataLength);

            printf("  Index  Expected  Received\n\r");
            for(size_t uiDumpIndex = 0U;
                uiDumpIndex < uiDumpLength;
                uiDumpIndex++)
            {
                printf("  [%02u]   0x%02X      0x%02X\n\r",
                       (unsigned int)uiDumpIndex,
                       uiGet_TestPatternByte(uiPatternOffset + uiDumpIndex),
                       uiaExtFlashValidationBuffer[uiDumpIndex]);
            }

            return false;
        }
    }

    return true;
}
#endif

#if EXT_FLASH_ENABLE_ERASED_DATA_TEST
static bool bVerify_FixedValueBuffer(size_t uiDataLength,
                                     uint8_t uiExpectedValue,
                                     const char *pcTestName,
                                     const char *pcReadType)
{
    for(size_t i = 0U; i < uiDataLength; i++)
    {
        if(uiaExtFlashValidationBuffer[i] != uiExpectedValue)
        {
            printf("ExtFlash %s: %s mismatch @%zu: expected 0x%02X, received 0x%02X\n\r",
                   pcTestName,
                   pcReadType,
                   i,
                   uiExpectedValue,
                   uiaExtFlashValidationBuffer[i]);
            return false;
        }
    }

    return true;
}
#endif

static bool bVerify_CanaryTail(size_t uiDataLength,
                               const char *pcTestName)
{
    for(size_t i = uiDataLength;
        i < sizeof(uiaExtFlashValidationBuffer);
        i++)
    {
        if(uiaExtFlashValidationBuffer[i] !=
           EXT_FLASH_VALIDATION_CANARY_VALUE)
        {
            printf("ExtFlash %s: RX buffer overrun @%zu: expected 0x%02X, received 0x%02X\n\r",
                   pcTestName,
                   i,
                   EXT_FLASH_VALIDATION_CANARY_VALUE,
                   uiaExtFlashValidationBuffer[i]);
            return false;
        }
    }

    return true;
}

#if EXT_FLASH_NORMAL_READ_VALIDATION_ENABLED
static bool bReadAndVerify_Normal(uint32_t uiAddress,
                                  size_t uiPatternOffset,
                                  size_t uiDataLength,
                                  const char *pcTestName)
{
    vFill_TestBufferWithCanary();

    int iResult = iRead_DataFromFlash_Normal(
        uiAddress,
        uiaExtFlashValidationBuffer,
        uiDataLength);
    if(iResult < 0)
    {
        printf("ExtFlash %s: normal read failed @0x%08X, length=%zu: %d\n\r",
               pcTestName,
               uiAddress,
               uiDataLength,
               iResult);
        return false;
    }

    if(!bVerify_TestBuffer(uiPatternOffset,
                           uiDataLength,
                           pcTestName,
                           "normal read"))
    {
        return false;
    }

    return bVerify_CanaryTail(uiDataLength, pcTestName);
}
#endif

#if EXT_FLASH_QUAD_PATTERN_VALIDATION_ENABLED
static bool bReadAndVerify_Quad(uint32_t uiAddress,
                                size_t uiPatternOffset,
                                size_t uiDataLength,
                                const char *pcTestName,
                                bool bCheckCanary)
{
    vFill_TestBufferWithCanary();

    int iResult = iRead_DataFromFlash_Quad(
        uiAddress,
        uiaExtFlashValidationBuffer,
        uiDataLength);
    if(iResult < 0)
    {
        printf("ExtFlash %s: quad read failed @0x%08X, length=%zu: %d\n\r",
               pcTestName,
               uiAddress,
               uiDataLength,
               iResult);
        return false;
    }

    if(!bVerify_TestBuffer(uiPatternOffset,
                           uiDataLength,
                           pcTestName,
                           "quad read"))
    {
        return false;
    }

    if(bCheckCanary &&
       !bVerify_CanaryTail(uiDataLength, pcTestName))
    {
        return false;
    }

    return true;
}
#endif

#if EXT_FLASH_ENABLE_NORMAL_READ_BOUNDARY_TEST
static bool bTest_NormalReadBoundaries( void )
{
    static const size_t uiaTestLengths[] = {
        DEFAULT_READ_BUFF_LENGTH - 1U,
        DEFAULT_READ_BUFF_LENGTH,
        DEFAULT_READ_BUFF_LENGTH + 1U
    };

    uint32_t uiTestAddress =
        EXT_FLASH_VALIDATION_AREA_START_ADDRESS;
    size_t uiMaximumLength =
        DEFAULT_READ_BUFF_LENGTH + 1U;

    if(!bErase_TestRange(uiTestAddress, uiMaximumLength) ||
       !bWrite_TestPattern(uiTestAddress, uiMaximumLength))
    {
        return false;
    }

    for(size_t uiCaseIndex = 0U;
        uiCaseIndex < ARRAY_SIZE(uiaTestLengths);
        uiCaseIndex++)
    {
        if(!bReadAndVerify_Normal(
               uiTestAddress,
               0U,
               uiaTestLengths[uiCaseIndex],
               "normal-read boundary test"))
        {
            return false;
        }
    }

    printf("ExtFlash normal-read boundary test passed: %u, %u and %u bytes\n\r",
           DEFAULT_READ_BUFF_LENGTH - 1U,
           DEFAULT_READ_BUFF_LENGTH,
           DEFAULT_READ_BUFF_LENGTH + 1U);
    return true;
}
#endif

#if EXT_FLASH_ENABLE_ERASED_DATA_TEST
static bool bTest_ErasedData( void )
{
    uint32_t uiTestAddress =
        EXT_FLASH_VALIDATION_AREA_START_ADDRESS;
    size_t uiTestLength =
        EXT_FLASH_BULK_TEST_SECTOR_SIZE_BYTES;

    if(!bErase_TestRange(uiTestAddress, uiTestLength))
    {
        return false;
    }

    memset(uiaExtFlashValidationBuffer, 0x00, uiTestLength);
    int iResult = iWrite_DataToFlash(uiTestAddress,
                                     uiaExtFlashValidationBuffer,
                                     uiTestLength);
    if(iResult < 0)
    {
        printf("ExtFlash erased-data test: zero-fill write failed: %d\n\r",
               iResult);
        return false;
    }

    if(!bErase_TestRange(uiTestAddress, uiTestLength))
    {
        return false;
    }

    vFill_TestBufferWithCanary();
    iResult = iRead_DataFromFlash_Normal(uiTestAddress,
                                         uiaExtFlashValidationBuffer,
                                         uiTestLength);
    if(iResult < 0 ||
       !bVerify_FixedValueBuffer(uiTestLength,
                                 0xFFU,
                                 "erased-data test",
                                 "normal read") ||
       !bVerify_CanaryTail(uiTestLength, "erased-data test"))
    {
        if(iResult < 0)
        {
            printf("ExtFlash erased-data test: normal read failed: %d\n\r",
                   iResult);
        }
        return false;
    }

    vFill_TestBufferWithCanary();
    iResult = iRead_DataFromFlash_Quad(uiTestAddress,
                                       uiaExtFlashValidationBuffer,
                                       uiTestLength);
    if(iResult < 0 ||
       !bVerify_FixedValueBuffer(uiTestLength,
                                 0xFFU,
                                 "erased-data test",
                                 "quad read") ||
       !bVerify_CanaryTail(uiTestLength, "erased-data test"))
    {
        if(iResult < 0)
        {
            printf("ExtFlash erased-data test: quad read failed: %d\n\r",
                   iResult);
        }
        return false;
    }

    printf("ExtFlash erased-data test passed: all %zu bytes are 0xFF\n\r",
           uiTestLength);
    return true;
}
#endif

#if EXT_FLASH_ENABLE_UNALIGNED_ADDRESS_TEST
static bool bTest_UnalignedStartingAddresses( void )
{
    static const sT_FlashValidationCase_t staTestCases[] = {
        {1U,   31U},
        {259U, 37U},
        {529U, 63U}
    };

    uint32_t uiTestBaseAddress =
        EXT_FLASH_VALIDATION_AREA_START_ADDRESS;

    if(!bErase_TestRange(uiTestBaseAddress,
                         EXT_FLASH_BULK_TEST_SECTOR_SIZE_BYTES))
    {
        return false;
    }

    for(size_t uiCaseIndex = 0U;
        uiCaseIndex < ARRAY_SIZE(staTestCases);
        uiCaseIndex++)
    {
        uint32_t uiAddress =
            uiTestBaseAddress + staTestCases[uiCaseIndex].uiAddressOffset;
        size_t uiDataLength = staTestCases[uiCaseIndex].uiDataLength;
        size_t uiPatternOffset =
            (size_t)(uiAddress - EXT_FLASH_VALIDATION_AREA_START_ADDRESS);

        if(!bWrite_TestPattern(uiAddress, uiDataLength) ||
           !bReadAndVerify_Normal(uiAddress,
                                  uiPatternOffset,
                                  uiDataLength,
                                  "unaligned-address test") ||
           !bReadAndVerify_Quad(uiAddress,
                                uiPatternOffset,
                                uiDataLength,
                                "unaligned-address test",
                                true))
        {
            return false;
        }
    }

    printf("ExtFlash unaligned-address test passed: offsets 1, 259 and 529\n\r");
    return true;
}
#endif

#if EXT_FLASH_ENABLE_PAGE_BOUNDARY_WRITE_TEST
static bool bTest_PageBoundaryWrites( void )
{
    static const sT_FlashValidationCase_t staTestCases[] = {
        {255U, 2U},
        {254U, 3U},
        {1U,   255U},
        {0U,   256U},
        {0U,   257U}
    };

    uint32_t uiTestBaseAddress =
        EXT_FLASH_VALIDATION_AREA_START_ADDRESS;

    for(size_t uiCaseIndex = 0U;
        uiCaseIndex < ARRAY_SIZE(staTestCases);
        uiCaseIndex++)
    {
        uint32_t uiAddress =
            uiTestBaseAddress + staTestCases[uiCaseIndex].uiAddressOffset;
        size_t uiDataLength = staTestCases[uiCaseIndex].uiDataLength;
        size_t uiPatternOffset =
            (size_t)(uiAddress - EXT_FLASH_VALIDATION_AREA_START_ADDRESS);

        if(!bErase_TestRange(uiAddress, uiDataLength) ||
           !bWrite_TestPattern(uiAddress, uiDataLength) ||
           !bReadAndVerify_Normal(uiAddress,
                                  uiPatternOffset,
                                  uiDataLength,
                                  "page-boundary write test") ||
           !bReadAndVerify_Quad(uiAddress,
                                uiPatternOffset,
                                uiDataLength,
                                "page-boundary write test",
                                true))
        {
            return false;
        }
    }

    printf("ExtFlash page-boundary write test passed: 255+2, 254+3, 255, 256 and 257-byte cases\n\r");
    return true;
}
#endif

#if EXT_FLASH_ENABLE_QUAD_TRANSACTION_BOUNDARY_TEST
static bool bTest_QuadTransactionBoundaries( void )
{
    static const size_t uiaTestLengths[] = {
        QUAD_READ_MAX_TRANSACTION_LENGTH - 1U,
        QUAD_READ_MAX_TRANSACTION_LENGTH,
        QUAD_READ_MAX_TRANSACTION_LENGTH + 1U
    };

    uint32_t uiTestAddress =
        EXT_FLASH_VALIDATION_AREA_START_ADDRESS;
    size_t uiMaximumLength =
        EXT_FLASH_QUAD_BOUNDARY_MAX_LENGTH_BYTES;

    if(!bErase_TestRange(uiTestAddress, uiMaximumLength) ||
       !bWrite_TestPattern(uiTestAddress, uiMaximumLength))
    {
        return false;
    }

    for(size_t uiCaseIndex = 0U;
        uiCaseIndex < ARRAY_SIZE(uiaTestLengths);
        uiCaseIndex++)
    {
        if(!bReadAndVerify_Quad(uiTestAddress,
                                0U,
                                uiaTestLengths[uiCaseIndex],
                                "quad transaction-boundary test",
                                true))
        {
            return false;
        }
    }

    printf("ExtFlash quad transaction-boundary test passed: %u, %u and %u bytes\n\r",
           QUAD_READ_MAX_TRANSACTION_LENGTH - 1U,
           QUAD_READ_MAX_TRANSACTION_LENGTH,
           QUAD_READ_MAX_TRANSACTION_LENGTH + 1U);
    return true;
}
#endif

#if EXT_FLASH_ENABLE_QUAD_PARTIAL_FRAME_TEST
static bool bTest_QuadRead_PartialFrames( void )
{
    static const size_t uiaTestLengths[] = {
        1U,
        2U,
        3U,
        5U,
        257U,
        4093U
    };

    uint32_t uiTestAddress =
        EXT_FLASH_VALIDATION_AREA_START_ADDRESS;
    size_t uiMaximumLength = uiaTestLengths[ARRAY_SIZE(uiaTestLengths) - 1U];

    if(!bErase_TestRange(uiTestAddress, uiMaximumLength) ||
       !bWrite_TestPattern(uiTestAddress, uiMaximumLength))
    {
        return false;
    }

    for(size_t uiCaseIndex = 0U;
        uiCaseIndex < ARRAY_SIZE(uiaTestLengths);
        uiCaseIndex++)
    {
        if(!bReadAndVerify_Quad(uiTestAddress,
                                0U,
                                uiaTestLengths[uiCaseIndex],
                                "partial-frame test",
                                true))
        {
            return false;
        }
    }

    printf("ExtFlash partial-frame test passed: 1, 2, 3, 5, 257 and 4093 bytes\n\r");
    return true;
}
#endif

#if EXT_FLASH_ENABLE_BULK_ACCESS_TEST
static bool bTest_BulkFlashAccess( void )
{
    uint32_t uiStartTime = k_uptime_get_32();

    if(!bErase_TestRange(EXT_FLASH_BULK_TEST_START_ADDRESS,
                         EXT_FLASH_BULK_TEST_SIZE_BYTES))
    {
        return false;
    }

    uint32_t uiEraseTime_ms = k_uptime_get_32() - uiStartTime;
    uiStartTime = k_uptime_get_32();

    for(size_t uiOffset = 0U;
        uiOffset < EXT_FLASH_BULK_TEST_SIZE_BYTES;
        uiOffset += EXT_FLASH_BULK_TEST_CHUNK_SIZE_BYTES)
    {
        uint32_t uiAddress =
            EXT_FLASH_BULK_TEST_START_ADDRESS + (uint32_t)uiOffset;

        if(!bWrite_TestPattern(uiAddress,
                               EXT_FLASH_BULK_TEST_CHUNK_SIZE_BYTES))
        {
            return false;
        }
    }

    uint32_t uiWriteTime_ms = k_uptime_get_32() - uiStartTime;
    uiStartTime = k_uptime_get_32();

    for(size_t uiOffset = 0U;
        uiOffset < EXT_FLASH_BULK_TEST_SIZE_BYTES;
        uiOffset += EXT_FLASH_BULK_TEST_CHUNK_SIZE_BYTES)
    {
        uint32_t uiAddress =
            EXT_FLASH_BULK_TEST_START_ADDRESS + (uint32_t)uiOffset;

        if(!bReadAndVerify_Normal(
               uiAddress,
               uiOffset,
               EXT_FLASH_BULK_TEST_CHUNK_SIZE_BYTES,
               "bulk test"))
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
        uint32_t uiAddress =
            EXT_FLASH_BULK_TEST_START_ADDRESS + (uint32_t)uiOffset;

        if(!bReadAndVerify_Quad(
               uiAddress,
               uiOffset,
               EXT_FLASH_BULK_TEST_CHUNK_SIZE_BYTES,
               "bulk test",
               true))
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
#endif

#endif

#if EXT_FLASH_ENABLE_INVALID_INPUT_TEST
static bool bExpect_InvalidInputResult(const char *pcCaseName,
                                       int iActualResult)
{
    if(iActualResult != -EINVAL)
    {
        printf("ExtFlash invalid-input test: %s returned %d, expected %d\n\r",
               pcCaseName,
               iActualResult,
               -EINVAL);
        return false;
    }

    return true;
}

static bool bTest_InvalidInputs( void )
{
    uint8_t uiTestData = 0U;
    uint32_t uiValidAddress =
        EXT_FLASH_VALIDATION_AREA_START_ADDRESS;

    printf("ExtFlash invalid-input test: the following driver validation messages are expected\n\r");

    if(!bExpect_InvalidInputResult(
           "write null buffer",
           iWrite_DataToFlash(uiValidAddress, NULL, 1U)) ||
       !bExpect_InvalidInputResult(
           "write zero length",
           iWrite_DataToFlash(uiValidAddress, &uiTestData, 0U)) ||
       !bExpect_InvalidInputResult(
           "normal-read null buffer",
           iRead_DataFromFlash_Normal(uiValidAddress, NULL, 1U)) ||
       !bExpect_InvalidInputResult(
           "normal-read zero length",
           iRead_DataFromFlash_Normal(uiValidAddress, &uiTestData, 0U)) ||
       !bExpect_InvalidInputResult(
           "quad-read null buffer",
           iRead_DataFromFlash_Quad(uiValidAddress, NULL, 1U)) ||
       !bExpect_InvalidInputResult(
           "quad-read zero length",
           iRead_DataFromFlash_Quad(uiValidAddress, &uiTestData, 0U)) ||
       !bExpect_InvalidInputResult(
           "unaligned sector erase",
           iErase_Flash_4KB(uiValidAddress + 1U)))
    {
        return false;
    }

    printf("ExtFlash invalid-input test passed: 7 invalid requests rejected with -EINVAL\n\r");
    return true;
}
#endif

#endif

#include "PC_UART_API_ValidateTest.h"

#include "PC_UART_API_ProjDef.h"

#if PC_UART_API_ENABLE_BULK_VALIDATION_TEST

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>

#include "PC_UART_API_Internal.h"
#include "PC_UART_Protocol.h"
#include "../ExtFlash_Controller/LittleFsController/LittleFs_Controller.h"

#define PC_UART_BULK_TEST_FILE_NAME          "pc_uart_bulk_test.bin"
#define PC_UART_BULK_TEST_FILE_PATH          "/lfs/Images/pc_uart_bulk_test.bin"
#define PC_UART_BULK_TEST_FILE_SIZE          4097U
#define PC_UART_BULK_TEST_RETRY_TIMEOUT_MS   2000U

static uint8_t uiTestPattern(uint32_t uiOffset)
{
    return (uint8_t)((uiOffset * 37U + 0x5AU) & 0xFFU);
}

static uint16_t u16CRC16_CCITT_Update(
    uint16_t uiCRC,
    const uint8_t *puiData,
    uint32_t uiLen)
{
    for(uint32_t i = 0U; i < uiLen; i++)
    {
        uiCRC ^= ((uint16_t)puiData[i] << 8U);
        for(uint8_t uiBit = 0U; uiBit < 8U; uiBit++)
        {
            uiCRC = (uiCRC & 0x8000U)
                        ? (uint16_t)((uiCRC << 1U) ^ 0x1021U)
                        : (uint16_t)(uiCRC << 1U);
        }
    }
    return uiCRC;
}

static void vWriteU16BE(uint8_t *puiData, uint16_t uiValue)
{
    puiData[0] = (uint8_t)(uiValue >> 8U);
    puiData[1] = (uint8_t)uiValue;
}

static void vWriteU32BE(uint8_t *puiData, uint32_t uiValue)
{
    puiData[0] = (uint8_t)(uiValue >> 24U);
    puiData[1] = (uint8_t)(uiValue >> 16U);
    puiData[2] = (uint8_t)(uiValue >> 8U);
    puiData[3] = (uint8_t)uiValue;
}

static uint16_t u16Calculate_TestFileCRC(void)
{
    uint8_t uiaChunk[PC_UART_PROTOCOL_BULK_MAX_DATA_LENGTH];
    uint16_t uiCRC = 0xFFFFU;

    for(uint32_t uiOffset = 0U;
        uiOffset < PC_UART_BULK_TEST_FILE_SIZE;
        uiOffset += sizeof(uiaChunk))
    {
        uint16_t uiLength = (uint16_t)(
            PC_UART_BULK_TEST_FILE_SIZE - uiOffset);
        if(uiLength > sizeof(uiaChunk))
        {
            uiLength = sizeof(uiaChunk);
        }
        for(uint16_t i = 0U; i < uiLength; i++)
        {
            uiaChunk[i] = uiTestPattern(uiOffset + i);
        }
        uiCRC = u16CRC16_CCITT_Update(uiCRC, uiaChunk, uiLength);
    }
    return uiCRC;
}

static bool bVerify_TestFile(void)
{
    struct fs_file_t stFile;
    fs_file_t_init(&stFile);
    if(fs_open(&stFile, PC_UART_BULK_TEST_FILE_PATH, FS_O_READ) != 0)
    {
        return false;
    }

    uint8_t uiaReadBuffer[PC_UART_PROTOCOL_BULK_MAX_DATA_LENGTH];
    uint32_t uiOffset = 0U;
    bool bResult = true;
    while(uiOffset < PC_UART_BULK_TEST_FILE_SIZE)
    {
        uint16_t uiRequestedLength = (uint16_t)(
            PC_UART_BULK_TEST_FILE_SIZE - uiOffset);
        if(uiRequestedLength > sizeof(uiaReadBuffer))
        {
            uiRequestedLength = sizeof(uiaReadBuffer);
        }

        ssize_t iReadLength = fs_read(
            &stFile,
            uiaReadBuffer,
            uiRequestedLength);
        if(iReadLength != uiRequestedLength)
        {
            bResult = false;
            break;
        }
        for(uint16_t i = 0U; i < uiRequestedLength; i++)
        {
            if(uiaReadBuffer[i] != uiTestPattern(uiOffset + i))
            {
                bResult = false;
                break;
            }
        }
        if(!bResult)
        {
            break;
        }
        uiOffset += uiRequestedLength;
    }

    if(fs_close(&stFile) != 0)
    {
        bResult = false;
    }
    return bResult;
}

bool bPC_UART_API_RunBulkValidationTest(void)
{
    const uint8_t uiaBulkOperation[] = {
        ePC_UART_Operation_LocalBulkFile,
    };
    const uint8_t uiaFirmwareOperation[] = {
        ePC_UART_Operation_FirmwareBridge,
    };
    if(ePC_UART_API_DispatchFrame(
           ePC_UART_Command_BulkStart,
           NULL,
           0U) != ePC_UART_Error_InvalidOperation)
    {
        return false;
    }
    if(ePC_UART_API_DispatchFrame(
           ePC_UART_Command_SelectOperation,
           uiaBulkOperation,
           sizeof(uiaBulkOperation)) != ePC_UART_Error_None)
    {
        return false;
    }
    if(ePC_UART_API_DispatchFrame(
           ePC_UART_Command_SelectOperation,
           uiaFirmwareOperation,
           sizeof(uiaFirmwareOperation)) != ePC_UART_Error_OperationBusy)
    {
        return false;
    }

    const size_t uiFileNameLength =
        sizeof(PC_UART_BULK_TEST_FILE_NAME) - 1U;
    uint8_t uiaStartPayload[
        PC_UART_PROTOCOL_BULK_START_FIXED_LENGTH +
        sizeof(PC_UART_BULK_TEST_FILE_NAME) - 1U] = {0};
    uiaStartPayload[0] = eFile_Image;
    vWriteU32BE(&uiaStartPayload[1], PC_UART_BULK_TEST_FILE_SIZE);
    vWriteU16BE(&uiaStartPayload[5], u16Calculate_TestFileCRC());
    uiaStartPayload[7] = (uint8_t)uiFileNameLength;
    memcpy(
        &uiaStartPayload[8],
        PC_UART_BULK_TEST_FILE_NAME,
        uiFileNameLength);

    if(ePC_UART_API_DispatchFrame(
           ePC_UART_Command_BulkStart,
           uiaStartPayload,
           PC_UART_PROTOCOL_BULK_START_FIXED_LENGTH - 1U) !=
       ePC_UART_Error_InvalidLength)
    {
        return false;
    }

    if(ePC_UART_API_DispatchFrame(
           ePC_UART_Command_BulkStart,
           uiaStartPayload,
           sizeof(uiaStartPayload)) != ePC_UART_Error_None)
    {
        return false;
    }

    uint8_t uiaDataPayload[
        PC_UART_PROTOCOL_BULK_DATA_FIXED_LENGTH +
        PC_UART_PROTOCOL_BULK_MAX_DATA_LENGTH] = {0};

    /* Prove that frame ordering and the per-frame CRC are rejected without
     * advancing the accepted frame sequence.
     */
    vWriteU32BE(&uiaDataPayload[0], 1U);
    vWriteU16BE(&uiaDataPayload[4], 1U);
    uiaDataPayload[8] = uiTestPattern(0U);
    vWriteU16BE(
        &uiaDataPayload[6],
        u16CRC16_CCITT_Update(0xFFFFU, &uiaDataPayload[8], 1U));
    if(ePC_UART_API_DispatchFrame(
           ePC_UART_Command_BulkData,
           uiaDataPayload,
           PC_UART_PROTOCOL_BULK_DATA_FIXED_LENGTH + 1U) !=
       ePC_UART_Error_InvalidFrame)
    {
        return false;
    }

    vWriteU32BE(&uiaDataPayload[0], 0U);
    vWriteU16BE(&uiaDataPayload[6], 0U);
    if(ePC_UART_API_DispatchFrame(
           ePC_UART_Command_BulkData,
           uiaDataPayload,
           PC_UART_PROTOCOL_BULK_DATA_FIXED_LENGTH + 1U) !=
       ePC_UART_Error_InvalidFrame)
    {
        return false;
    }

    uint32_t uiOffset = 0U;
    uint32_t uiFrameId = 0U;
    while(uiOffset < PC_UART_BULK_TEST_FILE_SIZE)
    {
        uint16_t uiDataLength = (uint16_t)(
            PC_UART_BULK_TEST_FILE_SIZE - uiOffset);
        if(uiDataLength > PC_UART_PROTOCOL_BULK_MAX_DATA_LENGTH)
        {
            uiDataLength = PC_UART_PROTOCOL_BULK_MAX_DATA_LENGTH;
        }

        vWriteU32BE(&uiaDataPayload[0], uiFrameId);
        vWriteU16BE(&uiaDataPayload[4], uiDataLength);
        for(uint16_t i = 0U; i < uiDataLength; i++)
        {
            uiaDataPayload[8U + i] = uiTestPattern(uiOffset + i);
        }
        vWriteU16BE(
            &uiaDataPayload[6],
            u16CRC16_CCITT_Update(
                0xFFFFU,
                &uiaDataPayload[8],
                uiDataLength));

        int64_t iDeadline =
            k_uptime_get() + PC_UART_BULK_TEST_RETRY_TIMEOUT_MS;
        ePC_UART_Error_t eResult;
        do
        {
            eResult = ePC_UART_API_DispatchFrame(
                ePC_UART_Command_BulkData,
                uiaDataPayload,
                PC_UART_PROTOCOL_BULK_DATA_FIXED_LENGTH +
                    uiDataLength);
            if(eResult == ePC_UART_Error_QueueFull)
            {
                k_msleep(1);
            }
        } while(eResult == ePC_UART_Error_QueueFull &&
                k_uptime_get() < iDeadline);

        if(eResult != ePC_UART_Error_None)
        {
            return false;
        }
        uiOffset += uiDataLength;
        uiFrameId++;
        k_msleep(1);
    }

    if(ePC_UART_API_DispatchFrame(
           ePC_UART_Command_BulkEnd,
           NULL,
           0U) != ePC_UART_Error_None)
    {
        return false;
    }
    if(!bVerify_TestFile())
    {
        return false;
    }

    int iDeleteResult = iDelete_File(PC_UART_BULK_TEST_FILE_PATH);
    if(iDeleteResult != 0 && iDeleteResult != -ENOENT)
    {
        return false;
    }

    if(ePC_UART_API_DispatchFrame(
           ePC_UART_Command_BulkData,
           uiaDataPayload,
           PC_UART_PROTOCOL_BULK_DATA_FIXED_LENGTH + 1U) !=
       ePC_UART_Error_InvalidOperation)
    {
        return false;
    }

    printf("PC UART API bulk validation passed: %u bytes\n\r",
           PC_UART_BULK_TEST_FILE_SIZE);
    return true;
}

#else

bool bPC_UART_API_RunBulkValidationTest(void)
{
    return true;
}

#endif /* PC_UART_API_ENABLE_BULK_VALIDATION_TEST */

#include "PC_UART_BulkFileService.h"

#include <errno.h>
#include <stdatomic.h>
#include <stddef.h>
#include <string.h>

#include "../ExtFlash_Controller/LittleFsController/LittleFs_Controller.h"

_Static_assert(
    PC_UART_PROTOCOL_BULK_MAX_DATA_LENGTH == MAX_FRAME_LENGTH,
    "PC UART bulk payload and LittleFS frame sizes must match");

static _Atomic bool bBulkSessionActive;

static uint16_t u16Read_BigEndian(const uint8_t *puiData)
{
    return ((uint16_t)puiData[0] << 8U) | puiData[1];
}

static uint32_t uiRead_BigEndian(const uint8_t *puiData)
{
    return ((uint32_t)puiData[0] << 24U) |
           ((uint32_t)puiData[1] << 16U) |
           ((uint32_t)puiData[2] << 8U) |
           puiData[3];
}

static ePC_UART_Error_t eProcess_BulkStart(
    const uint8_t *puiPayload,
    uint16_t uiPayloadLen)
{
    if(puiPayload == NULL ||
       uiPayloadLen < PC_UART_PROTOCOL_BULK_START_FIXED_LENGTH)
    {
        return ePC_UART_Error_InvalidLength;
    }

    uint8_t uiFileNameLen = puiPayload[7];
    if(uiFileNameLen == 0U ||
       uiFileNameLen >= MAX_FILE_NAME_LENGTH ||
       uiPayloadLen !=
           (PC_UART_PROTOCOL_BULK_START_FIXED_LENGTH + uiFileNameLen))
    {
        return ePC_UART_Error_InvalidLength;
    }

    sT_BulkTransferData stTransferData = {0};
    stTransferData.eFileType = (eFileType_t)puiPayload[0];
    stTransferData.uiFileSize = uiRead_BigEndian(&puiPayload[1]);
    stTransferData.uiCRC = u16Read_BigEndian(&puiPayload[5]);
    memcpy(stTransferData.pcaFileName, &puiPayload[8], uiFileNameLen);
    stTransferData.pcaFileName[uiFileNameLen] = '\0';

    if(stTransferData.eFileType <= eFile_Error ||
       stTransferData.eFileType >= eNUMBER_OF_FILE_TYPEs ||
       stTransferData.uiFileSize == 0U)
    {
        return ePC_UART_Error_InvalidFrame;
    }

    if(!bStart_BulkDataTransfer(stTransferData))
    {
        return ePC_UART_Error_BulkStartFailed;
    }

    atomic_store_explicit(&bBulkSessionActive, true, memory_order_release);
    return ePC_UART_Error_None;
}

static ePC_UART_Error_t eProcess_BulkData(
    const uint8_t *puiPayload,
    uint16_t uiPayloadLen)
{
    if(!bPC_UART_BulkFileService_IsSessionActive())
    {
        return ePC_UART_Error_InvalidOperation;
    }
    if(puiPayload == NULL ||
       uiPayloadLen < PC_UART_PROTOCOL_BULK_DATA_FIXED_LENGTH)
    {
        return ePC_UART_Error_InvalidLength;
    }

    uint16_t uiDataLen = u16Read_BigEndian(&puiPayload[4]);
    if(uiDataLen == 0U ||
       uiDataLen > MAX_FRAME_LENGTH ||
       uiPayloadLen !=
           (PC_UART_PROTOCOL_BULK_DATA_FIXED_LENGTH + uiDataLen))
    {
        return ePC_UART_Error_InvalidLength;
    }

    sT_MsgData_t stMessage = {0};
    stMessage.uiFrameId = uiRead_BigEndian(&puiPayload[0]);
    stMessage.uiDataLen = uiDataLen;
    stMessage.uiFrameCRC = u16Read_BigEndian(&puiPayload[6]);
    memcpy(stMessage.uiMsg, &puiPayload[8], uiDataLen);

    int iResult = iSend_BulkTransferData(stMessage);
    if(iResult == -ENOMSG || iResult == -EAGAIN)
    {
        return ePC_UART_Error_QueueFull;
    }
    return (iResult == 0)
               ? ePC_UART_Error_None
               : ePC_UART_Error_InvalidFrame;
}

static ePC_UART_Error_t eProcess_BulkEnd(
    const uint8_t *puiPayload,
    uint16_t uiPayloadLen)
{
    (void)puiPayload;
    if(uiPayloadLen != 0U)
    {
        return ePC_UART_Error_InvalidLength;
    }
    if(!bPC_UART_BulkFileService_IsSessionActive())
    {
        return ePC_UART_Error_InvalidOperation;
    }

    sT_TransferResult_t stResult = {0};
    vEnd_BulkDataTransfer(&stResult);
    atomic_store_explicit(&bBulkSessionActive, false, memory_order_release);

    if(stResult.eTransferState == eFileTransfer_Success &&
       stResult.eErrorState == eErrorState_None)
    {
        return ePC_UART_Error_None;
    }

    switch(stResult.eErrorState)
    {
        case eErrorState_TimeOut:
            return ePC_UART_Error_BulkTimeout;
        case eErrorState_CRCMismatch:
            return ePC_UART_Error_BulkCRCMismatch;
        case eErrorState_ReceivedByteCountMismatch:
            return ePC_UART_Error_BulkSizeMismatch;
        default:
            return ePC_UART_Error_BulkTransferFailed;
    }
}

ePC_UART_Error_t ePC_UART_BulkFileService_Process(
    uint8_t uiCommand,
    const uint8_t *puiPayload,
    uint16_t uiPayloadLen,
    bool *pbOperationComplete)
{
    if(pbOperationComplete == NULL)
    {
        return ePC_UART_Error_InvalidFrame;
    }
    *pbOperationComplete = false;

    switch(uiCommand)
    {
        case ePC_UART_Command_BulkStart:
            return eProcess_BulkStart(puiPayload, uiPayloadLen);
        case ePC_UART_Command_BulkData:
            return eProcess_BulkData(puiPayload, uiPayloadLen);
        case ePC_UART_Command_BulkEnd:
            *pbOperationComplete = true;
            return eProcess_BulkEnd(puiPayload, uiPayloadLen);
        default:
            return ePC_UART_Error_InvalidCommand;
    }
}

bool bPC_UART_BulkFileService_IsSessionActive(void)
{
    return atomic_load_explicit(&bBulkSessionActive, memory_order_acquire);
}

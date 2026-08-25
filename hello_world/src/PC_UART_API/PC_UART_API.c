#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>

#include "PC_UART_API.h"
#include "PC_UART_API_Internal.h"
#include "PC_UART_BulkFileService.h"
#include "PC_UART_FWBridgeService.h"
#include "PC_UART_Protocol.h"

#define PC_UART_API_THREAD_STACK_SIZE_BYTEs       2048
#define PC_UART_API_CAN_THREAD_STACK_SIZE_BYTEs   2048
#define PC_UART_API_THREAD_PRIORITY               6
#define PC_UART_API_CAN_THREAD_PRIORITY           7
#define PC_UART_API_CAN_RX_QUEUE_DEPTH            8

#define PC_UART_API_SOF_BYTE_0                    'B'
#define PC_UART_API_SOF_BYTE_1                    'L'
#define PC_UART_API_SOF_BYTE_2                    'U'
#define PC_UART_API_SOF_BYTE_3                    'P'
#define PC_UART_API_BYTE_TIMEOUT_MS               1000

#if DT_NODE_HAS_STATUS(DT_ALIAS(pc_uart_api), okay)
    #define PC_UART_API_NODE                      DT_ALIAS(pc_uart_api)
#elif DT_NODE_HAS_STATUS(DT_ALIAS(uart_can_bridge), okay)
    /* Temporary compatibility with boards that still carry the old alias. */
    #define PC_UART_API_NODE                      DT_ALIAS(uart_can_bridge)
#else
    #define PC_UART_API_NODE                      DT_CHOSEN(zephyr_console)
#endif

#define DEBUG_PC_UART_API

#if defined(DEBUG_PC_UART_API)
    #define PC_UART_API_Print                     printk
#else
    #define PC_UART_API_Print(...)
#endif

static const struct device *pstUARTDev;
static struct k_thread stPC_UART_APIThread_t;
static struct k_thread stPC_UART_APICANThread_t;
static struct k_mutex stUARTTxMutex;
static bool bUARTDebugNextFrame;
static bool bPC_UART_APIInitialized;
static uint16_t uiPC_UART_APITxSeq;
static _Atomic ePC_UART_Operation_t eActiveOperation;
static PC_UART_API_DataRequestCallback_t pfADCDataRequestCallback;
K_THREAD_STACK_DEFINE(thread_PC_UART_APIStack, PC_UART_API_THREAD_STACK_SIZE_BYTEs);
K_THREAD_STACK_DEFINE(thread_PC_UART_APICANStack, PC_UART_API_CAN_THREAD_STACK_SIZE_BYTEs);
K_MSGQ_DEFINE(msgq_PC_UART_APICANRx, sizeof(struct can_frame), PC_UART_API_CAN_RX_QUEUE_DEPTH, 4);

static void vThread_PC_UART_API( void *p1, void *p2, void *p3 );
static void vThread_PC_UART_API_CANRx( void *p1, void *p2, void *p3 );
static int iRead_UARTByte( uint8_t *puiByte );
static bool bRead_UARTBytesWithTimeout( uint8_t *puiBuffer, uint16_t uiLen, uint32_t uiTimeoutMs );
static bool bRead_UARTFrame( uint8_t *puiCommand, uint8_t *puiPayload, uint16_t *puiPayloadLen, uint16_t *puiSeq );
static ePC_UART_Error_t eSelect_Operation(const uint8_t *puiPayload, uint16_t uiPayloadLen);
static ePC_UART_Error_t eRelease_Operation(uint16_t uiPayloadLen);
static ePC_UART_Operation_t eGet_ActiveOperation(void);
static void vSet_ActiveOperation(ePC_UART_Operation_t eOperation);
static void vSend_UARTFrameToPC( const struct can_frame *pstFrame );
static void vSend_UARTResponse( uint8_t uiStatus, uint8_t uiErrorCode );
static void vSend_UARTDebugByte( uint8_t uiDebugByte );
static void vUART_WriteByte( uint8_t uiByte );
static void vUART_WriteBytes( const uint8_t *puiData, uint16_t uiLen );
static uint16_t u16CRC16_CCITT_Update( uint16_t uiCRC, const uint8_t *puiData, uint32_t uiLen );

void vInit_PC_UART_API( void )
{
    pstUARTDev = DEVICE_DT_GET(PC_UART_API_NODE);
    if(!device_is_ready(pstUARTDev))
    {
        PC_UART_API_Print("PC UART API: UART device not ready\n\r");
        return;
    }

    k_mutex_init(&stUARTTxMutex);
    uiPC_UART_APITxSeq = 0;
    vSet_ActiveOperation(ePC_UART_Operation_Idle);
    bPC_UART_APIInitialized = true;

    k_thread_create(&stPC_UART_APIThread_t,
                    thread_PC_UART_APIStack,
                    K_THREAD_STACK_SIZEOF(thread_PC_UART_APIStack),
                    vThread_PC_UART_API,
                    NULL, NULL, NULL,
                    PC_UART_API_THREAD_PRIORITY,
                    0,
                    K_NO_WAIT);

    k_thread_create(&stPC_UART_APICANThread_t,
                    thread_PC_UART_APICANStack,
                    K_THREAD_STACK_SIZEOF(thread_PC_UART_APICANStack),
                    vThread_PC_UART_API_CANRx,
                    NULL, NULL, NULL,
                    PC_UART_API_CAN_THREAD_PRIORITY,
                    0,
                    K_NO_WAIT);

    PC_UART_API_Print("PC UART API initialized\n\r");
}

bool bPC_UART_API_SendData( const uint8_t *puiData, uint16_t uiLen )
{
    if(!bPC_UART_APIInitialized || puiData == NULL || uiLen == 0U)
        return false;

    k_mutex_lock(&stUARTTxMutex, K_FOREVER);
    vUART_WriteBytes(puiData, uiLen);
    k_mutex_unlock(&stUARTTxMutex);
    return true;
}

bool bPC_UART_API_SendDataWithPostDelay( const uint8_t *puiData, uint16_t uiLen, uint32_t uiDelayMs )
{
    if(!bPC_UART_APIInitialized || puiData == NULL || uiLen == 0U)
        return false;

    k_mutex_lock(&stUARTTxMutex, K_FOREVER);
    vUART_WriteBytes(puiData, uiLen);
    if(uiDelayMs > 0U)
        k_msleep(uiDelayMs);
    k_mutex_unlock(&stUARTTxMutex);
    return true;
}

void vPC_UART_API_RegisterADCDataRequestCallback( PC_UART_API_DataRequestCallback_t pfCallback )
{
    pfADCDataRequestCallback = pfCallback;
}

void vPC_UART_API_ForwardCANFrame( const struct can_frame *pstFrame )
{
    if(pstFrame == NULL ||
       eGet_ActiveOperation() != ePC_UART_Operation_FirmwareBridge)
        return;

    if(k_msgq_put(&msgq_PC_UART_APICANRx, pstFrame, K_NO_WAIT) != 0)
    {
        PC_UART_API_Print("PC UART API: CAN RX forwarding queue full\n\r");
    }
}

static void vThread_PC_UART_API( void *p1, void *p2, void *p3 )
{
    uint8_t uiCommand = 0;
    uint8_t uiaPayload[PC_UART_PROTOCOL_MAX_PAYLOAD_LENGTH];
    uint16_t uiPayloadLen = 0;
    uint16_t uiSeq = 0;
    uint32_t uiFrameCount = 0;

    while(1)
    {
        if(!bRead_UARTFrame(&uiCommand, uiaPayload, &uiPayloadLen, &uiSeq))
            continue;

        uiFrameCount++;

        ePC_UART_Error_t eError =
            ePC_UART_API_DispatchFrame(uiCommand, uiaPayload, uiPayloadLen);
        vSend_UARTResponse(
            (eError == ePC_UART_Error_None)
                ? PC_UART_PROTOCOL_ACK
                : PC_UART_PROTOCOL_NACK,
            (uint8_t)eError);
        bUARTDebugNextFrame = false;
    }
}

static void vThread_PC_UART_API_CANRx( void *p1, void *p2, void *p3 )
{
    struct can_frame stFrame;

    while(1)
    {
        if(k_msgq_get(&msgq_PC_UART_APICANRx, &stFrame, K_FOREVER) != 0)
            continue;

        if(eGet_ActiveOperation() == ePC_UART_Operation_FirmwareBridge)
        {
            vSend_UARTFrameToPC(&stFrame);
        }
    }
}

static int iRead_UARTByte( uint8_t *puiByte )
{
    int ret = uart_poll_in(pstUARTDev, puiByte);
    if(ret != 0)
        k_msleep(1);

    return ret;
}

static bool bRead_UARTBytesWithTimeout( uint8_t *puiBuffer, uint16_t uiLen, uint32_t uiTimeoutMs )
{
    int64_t iDeadline = k_uptime_get() + uiTimeoutMs;

    for(uint16_t i = 0; i < uiLen; i++)
    {
        while(iRead_UARTByte(&puiBuffer[i]) != 0)
        {
            if(k_uptime_get() >= iDeadline)
                return false;
        }
    }

    return true;
}

static bool bRead_UARTFrame( uint8_t *puiCommand, uint8_t *puiPayload, uint16_t *puiPayloadLen, uint16_t *puiSeq )
{
    static const uint8_t uiaSOF[] = {
        PC_UART_API_SOF_BYTE_0,
        PC_UART_API_SOF_BYTE_1,
        PC_UART_API_SOF_BYTE_2,
        PC_UART_API_SOF_BYTE_3,
    };

    uint8_t uiByte = 0;
    uint8_t uiSOFIndex = 0;
    uint8_t uiaHeaderTail[PC_UART_PROTOCOL_HEADER_LENGTH - sizeof(uiaSOF)];
    uint8_t uiaCRC[2];
    uint16_t uiFrameCRC = 0;
    uint16_t uiCalculatedCRC = 0xFFFF;

    while(uiSOFIndex < sizeof(uiaSOF))
    {
        if(iRead_UARTByte(&uiByte) != 0)
            continue;

        if(uiByte == '?')
        {
            uart_poll_out(pstUARTDev, '!');
            continue;
        }
        if(uiByte == 'D')
        {
            bUARTDebugNextFrame = true;
            uart_poll_out(pstUARTDev, 'd');
            continue;
        }
        if(uiByte == 'A')
        {
            if(pfADCDataRequestCallback != NULL)
                pfADCDataRequestCallback();
            continue;
        }

        if(uiByte == uiaSOF[uiSOFIndex])
        {
            uiSOFIndex++;
        }
        else
        {
            uiSOFIndex = (uiByte == uiaSOF[0]) ? 1 : 0;
        }
    }

    vSend_UARTDebugByte('S');
    if(!bRead_UARTBytesWithTimeout(uiaHeaderTail, sizeof(uiaHeaderTail), PC_UART_API_BYTE_TIMEOUT_MS))
    {
        vSend_UARTDebugByte('h');
        vSend_UARTResponse(PC_UART_PROTOCOL_NACK, ePC_UART_Error_InvalidLength);
        return false;
    }
    vSend_UARTDebugByte('H');

    if(uiaHeaderTail[0] != PC_UART_PROTOCOL_VERSION)
    {
        vSend_UARTDebugByte('v');
        vSend_UARTResponse(PC_UART_PROTOCOL_NACK, ePC_UART_Error_InvalidVersion);
        return false;
    }

    *puiCommand = uiaHeaderTail[1];
    *puiSeq = ((uint16_t)uiaHeaderTail[2] << 8) | uiaHeaderTail[3];
    *puiPayloadLen = ((uint16_t)uiaHeaderTail[4] << 8) | uiaHeaderTail[5];

    if(*puiPayloadLen > PC_UART_PROTOCOL_MAX_PAYLOAD_LENGTH)
    {
        vSend_UARTDebugByte('l');
        vSend_UARTResponse(PC_UART_PROTOCOL_NACK, ePC_UART_Error_InvalidLength);
        return false;
    }

    if(!bRead_UARTBytesWithTimeout(puiPayload, *puiPayloadLen, PC_UART_API_BYTE_TIMEOUT_MS))
    {
        vSend_UARTDebugByte('p');
        vSend_UARTResponse(PC_UART_PROTOCOL_NACK, ePC_UART_Error_InvalidLength);
        return false;
    }
    vSend_UARTDebugByte('P');
    if(!bRead_UARTBytesWithTimeout(uiaCRC, sizeof(uiaCRC), PC_UART_API_BYTE_TIMEOUT_MS))
    {
        vSend_UARTDebugByte('t');
        vSend_UARTResponse(PC_UART_PROTOCOL_NACK, ePC_UART_Error_InvalidLength);
        return false;
    }
    vSend_UARTDebugByte('T');

    uiFrameCRC = ((uint16_t)uiaCRC[0] << 8) | uiaCRC[1];
    uiCalculatedCRC = u16CRC16_CCITT_Update(uiCalculatedCRC, uiaHeaderTail, sizeof(uiaHeaderTail));
    uiCalculatedCRC = u16CRC16_CCITT_Update(uiCalculatedCRC, puiPayload, *puiPayloadLen);

    if(uiFrameCRC != uiCalculatedCRC)
    {
        vSend_UARTDebugByte('c');
        vSend_UARTResponse(PC_UART_PROTOCOL_NACK, ePC_UART_Error_FrameCRCMismatch);
        return false;
    }

    vSend_UARTDebugByte('C');
    return true;
}

ePC_UART_Error_t ePC_UART_API_DispatchFrame(
    uint8_t uiCommand,
    const uint8_t *puiPayload,
    uint16_t uiPayloadLen)
{
    if(uiCommand == ePC_UART_Command_SelectOperation)
    {
        return eSelect_Operation(puiPayload, uiPayloadLen);
    }
    if(uiCommand == ePC_UART_Command_ReleaseOperation)
    {
        return eRelease_Operation(uiPayloadLen);
    }

    ePC_UART_Operation_t eOperation = eGet_ActiveOperation();
    switch(eOperation)
    {
        case ePC_UART_Operation_FirmwareBridge:
            return ePC_UART_FWBridgeService_Process(
                uiCommand,
                puiPayload,
                uiPayloadLen);

        case ePC_UART_Operation_LocalBulkFile:
        {
            bool bOperationComplete = false;
            ePC_UART_Error_t eError =
                ePC_UART_BulkFileService_Process(
                    uiCommand,
                    puiPayload,
                    uiPayloadLen,
                    &bOperationComplete);
            if(bOperationComplete)
            {
                vSet_ActiveOperation(ePC_UART_Operation_Idle);
            }
            return eError;
        }

        case ePC_UART_Operation_Idle:
            return ePC_UART_Error_InvalidOperation;

        default:
            return ePC_UART_Error_ServiceUnavailable;
    }
}

static ePC_UART_Error_t eSelect_Operation(
    const uint8_t *puiPayload,
    uint16_t uiPayloadLen)
{
    if(puiPayload == NULL || uiPayloadLen != 1U)
    {
        return ePC_UART_Error_InvalidLength;
    }

    ePC_UART_Operation_t eRequestedOperation =
        (ePC_UART_Operation_t)puiPayload[0];
    if(eRequestedOperation <= ePC_UART_Operation_Idle ||
       eRequestedOperation >= eNUMBER_OF_PC_UART_OPERATIONS)
    {
        return ePC_UART_Error_InvalidOperation;
    }

    ePC_UART_Operation_t eCurrentOperation = eGet_ActiveOperation();
    if(eCurrentOperation == eRequestedOperation)
    {
        return ePC_UART_Error_None;
    }
    if(eCurrentOperation != ePC_UART_Operation_Idle)
    {
        return ePC_UART_Error_OperationBusy;
    }

    vSet_ActiveOperation(eRequestedOperation);
    return ePC_UART_Error_None;
}

static ePC_UART_Error_t eRelease_Operation(uint16_t uiPayloadLen)
{
    if(uiPayloadLen != 0U)
    {
        return ePC_UART_Error_InvalidLength;
    }
    if(eGet_ActiveOperation() == ePC_UART_Operation_LocalBulkFile &&
       bPC_UART_BulkFileService_IsSessionActive())
    {
        return ePC_UART_Error_OperationBusy;
    }

    vSet_ActiveOperation(ePC_UART_Operation_Idle);
    return ePC_UART_Error_None;
}

static ePC_UART_Operation_t eGet_ActiveOperation(void)
{
    return atomic_load_explicit(&eActiveOperation, memory_order_acquire);
}

static void vSet_ActiveOperation(ePC_UART_Operation_t eOperation)
{
    atomic_store_explicit(
        &eActiveOperation,
        eOperation,
        memory_order_release);
}

static void vSend_UARTFrameToPC( const struct can_frame *pstFrame )
{
    uint8_t uiLen = 0;
    uint8_t uiPayloadLen = 0;
    uint8_t uiaHeaderTail[PC_UART_PROTOCOL_HEADER_LENGTH - 4U];
    uint16_t uiCRC = 0xFFFF;

    if(pstFrame == NULL)
        return;

    uiLen = can_dlc_to_bytes(pstFrame->dlc);
    if(uiLen == 0)
        return;

    uiPayloadLen = uiLen - 1;
    uiaHeaderTail[0] = PC_UART_PROTOCOL_VERSION;
    uiaHeaderTail[1] = pstFrame->data[0];
    uiaHeaderTail[2] = (uiPC_UART_APITxSeq >> 8) & 0xFF;
    uiaHeaderTail[3] = uiPC_UART_APITxSeq & 0xFF;
    uiaHeaderTail[4] = 0;
    uiaHeaderTail[5] = uiPayloadLen;

    uiCRC = u16CRC16_CCITT_Update(uiCRC, uiaHeaderTail, sizeof(uiaHeaderTail));
    if(uiPayloadLen > 0)
        uiCRC = u16CRC16_CCITT_Update(uiCRC, &pstFrame->data[1], uiPayloadLen);

    k_mutex_lock(&stUARTTxMutex, K_FOREVER);
    vUART_WriteBytes((const uint8_t *)"BLUP", 4);
    vUART_WriteBytes(uiaHeaderTail, sizeof(uiaHeaderTail));
    if(uiPayloadLen > 0)
        vUART_WriteBytes(&pstFrame->data[1], uiPayloadLen);
    vUART_WriteByte((uiCRC >> 8) & 0xFF);
    vUART_WriteByte(uiCRC & 0xFF);
    k_mutex_unlock(&stUARTTxMutex);

    uiPC_UART_APITxSeq++;
}

static void vSend_UARTResponse( uint8_t uiStatus, uint8_t uiErrorCode )
{
    k_mutex_lock(&stUARTTxMutex, K_FOREVER);
    vUART_WriteByte(uiStatus);
    vUART_WriteByte(uiErrorCode);
    k_mutex_unlock(&stUARTTxMutex);
}

static void vSend_UARTDebugByte( uint8_t uiDebugByte )
{
    if(bUARTDebugNextFrame)
    {
        k_mutex_lock(&stUARTTxMutex, K_FOREVER);
        vUART_WriteByte(uiDebugByte);
        k_mutex_unlock(&stUARTTxMutex);
    }
}

static void vUART_WriteByte( uint8_t uiByte )
{
    uart_poll_out(pstUARTDev, uiByte);
}

static void vUART_WriteBytes( const uint8_t *puiData, uint16_t uiLen )
{
    for(uint16_t i = 0; i < uiLen; i++)
    {
        vUART_WriteByte(puiData[i]);
    }
}

static uint16_t u16CRC16_CCITT_Update( uint16_t uiCRC, const uint8_t *puiData, uint32_t uiLen )
{
    for(uint32_t i = 0; i < uiLen; i++)
    {
        uiCRC ^= ((uint16_t)puiData[i] << 8);

        for(uint8_t bit = 0; bit < 8; bit++)
        {
            if(uiCRC & 0x8000)
                uiCRC = (uiCRC << 1) ^ 0x1021;
            else
                uiCRC <<= 1;
        }
    }

    return uiCRC;
}

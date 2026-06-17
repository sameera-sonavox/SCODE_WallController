#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>

#include "../CAN_Controller/CAN_Controller.h"
#include "../Lib/CAN/NXP_CAN_ProjDef.h"

#include "UART_CAN_Bridge.h"

#define UART_CAN_BRIDGE_THREAD_STACK_SIZE_BYTEs       2048
#define UART_CAN_BRIDGE_CAN_THREAD_STACK_SIZE_BYTEs   2048
#define UART_CAN_BRIDGE_THREAD_PRIORITY               6
#define UART_CAN_BRIDGE_CAN_THREAD_PRIORITY           7
#define UART_CAN_BRIDGE_CAN_RX_QUEUE_DEPTH            8

#define UART_CAN_BRIDGE_SOF_BYTE_0                    'B'
#define UART_CAN_BRIDGE_SOF_BYTE_1                    'L'
#define UART_CAN_BRIDGE_SOF_BYTE_2                    'U'
#define UART_CAN_BRIDGE_SOF_BYTE_3                    'P'
#define UART_CAN_BRIDGE_VERSION                       1

#define UART_CAN_BRIDGE_HEADER_LENGTH                 10
#define UART_CAN_BRIDGE_PAYLOAD_MAX_LENGTH            (CAN_MSG_MAX_SIZE - 1)
#define UART_CAN_BRIDGE_CAN_TX_TIMEOUT_MS             50
#define UART_CAN_BRIDGE_BYTE_TIMEOUT_MS               200

#define UART_CAN_BRIDGE_ACK                           0x06
#define UART_CAN_BRIDGE_NACK                          0x15

#if DT_NODE_HAS_STATUS(DT_ALIAS(uart_can_bridge), okay)
    #define UART_CAN_BRIDGE_NODE                      DT_ALIAS(uart_can_bridge)
#else
    #define UART_CAN_BRIDGE_NODE                      DT_CHOSEN(zephyr_console)
#endif

#define DEBUG_UART_CAN_BRIDGE

#if defined(DEBUG_UART_CAN_BRIDGE)
    #define UART_CAN_BRIDGE_Print                     printk
#else
    #define UART_CAN_BRIDGE_Print(...)
#endif

typedef enum{
    eUART_CAN_Bridge_Error_None = 0,
    eUART_CAN_Bridge_Error_InvalidVersion,
    eUART_CAN_Bridge_Error_InvalidLength,
    eUART_CAN_Bridge_Error_CRCMismatch,
    eUART_CAN_Bridge_Error_CANTxFail,
} eT_UART_CAN_Bridge_Error;

static const struct device *pstUARTDev;
static struct k_thread stUART_CAN_BridgeThread_t;
static struct k_thread stUART_CAN_BridgeCANThread_t;
static struct k_mutex stUARTTxMutex;
static bool bUARTDebugNextFrame;
static bool bUART_CAN_BridgeInitialized;
static uint16_t uiUART_CANBridgeTxSeq;
static UART_CAN_Bridge_DataRequestCallback_t pfADCDataRequestCallback;
K_THREAD_STACK_DEFINE(thread_UART_CAN_BridgeStack, UART_CAN_BRIDGE_THREAD_STACK_SIZE_BYTEs);
K_THREAD_STACK_DEFINE(thread_UART_CAN_BridgeCANStack, UART_CAN_BRIDGE_CAN_THREAD_STACK_SIZE_BYTEs);
K_MSGQ_DEFINE(msgq_UART_CANBridgeRx, sizeof(struct can_frame), UART_CAN_BRIDGE_CAN_RX_QUEUE_DEPTH, 4);

static void vThread_UART_CAN_Bridge( void *p1, void *p2, void *p3 );
static void vThread_UART_CAN_Bridge_CANRx( void *p1, void *p2, void *p3 );
static int iRead_UARTByte( uint8_t *puiByte );
static bool bRead_UARTBytesWithTimeout( uint8_t *puiBuffer, uint16_t uiLen, uint32_t uiTimeoutMs );
static bool bRead_UARTFrame( uint8_t *puiCommand, uint8_t *puiPayload, uint16_t *puiPayloadLen, uint16_t *puiSeq );
static bool bForward_FrameToCAN( uint8_t uiCommand, const uint8_t *puiPayload, uint16_t uiPayloadLen );
static void vSend_UARTFrameToPC( const struct can_frame *pstFrame );
static void vSend_UARTResponse( uint8_t uiStatus, uint8_t uiErrorCode );
static void vSend_UARTDebugByte( uint8_t uiDebugByte );
static void vUART_WriteByte( uint8_t uiByte );
static void vUART_WriteBytes( const uint8_t *puiData, uint16_t uiLen );
static uint16_t u16CRC16_CCITT_Update( uint16_t uiCRC, const uint8_t *puiData, uint32_t uiLen );

void vInit_UART_CAN_Bridge( void )
{
    pstUARTDev = DEVICE_DT_GET(UART_CAN_BRIDGE_NODE);
    if(!device_is_ready(pstUARTDev))
    {
        UART_CAN_BRIDGE_Print("UART CAN Bridge: UART device not ready\n\r");
        return;
    }

    k_mutex_init(&stUARTTxMutex);
    uiUART_CANBridgeTxSeq = 0;
    bUART_CAN_BridgeInitialized = true;

    k_thread_create(&stUART_CAN_BridgeThread_t,
                    thread_UART_CAN_BridgeStack,
                    K_THREAD_STACK_SIZEOF(thread_UART_CAN_BridgeStack),
                    vThread_UART_CAN_Bridge,
                    NULL, NULL, NULL,
                    UART_CAN_BRIDGE_THREAD_PRIORITY,
                    0,
                    K_NO_WAIT);

    k_thread_create(&stUART_CAN_BridgeCANThread_t,
                    thread_UART_CAN_BridgeCANStack,
                    K_THREAD_STACK_SIZEOF(thread_UART_CAN_BridgeCANStack),
                    vThread_UART_CAN_Bridge_CANRx,
                    NULL, NULL, NULL,
                    UART_CAN_BRIDGE_CAN_THREAD_PRIORITY,
                    0,
                    K_NO_WAIT);

    UART_CAN_BRIDGE_Print("UART CAN Bridge initialized\n\r");
}

bool bUART_CAN_Bridge_SendData( const uint8_t *puiData, uint16_t uiLen )
{
    if(!bUART_CAN_BridgeInitialized || puiData == NULL || uiLen == 0U)
        return false;

    k_mutex_lock(&stUARTTxMutex, K_FOREVER);
    vUART_WriteBytes(puiData, uiLen);
    k_mutex_unlock(&stUARTTxMutex);
    return true;
}

bool bUART_CAN_Bridge_SendDataWithPostDelay( const uint8_t *puiData, uint16_t uiLen, uint32_t uiDelayMs )
{
    if(!bUART_CAN_BridgeInitialized || puiData == NULL || uiLen == 0U)
        return false;

    k_mutex_lock(&stUARTTxMutex, K_FOREVER);
    vUART_WriteBytes(puiData, uiLen);
    if(uiDelayMs > 0U)
        k_msleep(uiDelayMs);
    k_mutex_unlock(&stUARTTxMutex);
    return true;
}

void vUART_CAN_Bridge_RegisterADCDataRequestCallback( UART_CAN_Bridge_DataRequestCallback_t pfCallback )
{
    pfADCDataRequestCallback = pfCallback;
}

void vUART_CAN_Bridge_ForwardCANFrame( const struct can_frame *pstFrame )
{
    if(pstFrame == NULL)
        return;

    if(k_msgq_put(&msgq_UART_CANBridgeRx, pstFrame, K_NO_WAIT) != 0)
    {
        UART_CAN_BRIDGE_Print("UART CAN Bridge: CAN RX forwarding queue full\n\r");
    }
}

static void vThread_UART_CAN_Bridge( void *p1, void *p2, void *p3 )
{
    uint8_t uiCommand = 0;
    uint8_t uiaPayload[UART_CAN_BRIDGE_PAYLOAD_MAX_LENGTH];
    uint16_t uiPayloadLen = 0;
    uint16_t uiSeq = 0;
    uint32_t uiFrameCount = 0;

    while(1)
    {
        if(!bRead_UARTFrame(&uiCommand, uiaPayload, &uiPayloadLen, &uiSeq))
            continue;

        uiFrameCount++;

        if(bForward_FrameToCAN(uiCommand, uiaPayload, uiPayloadLen))
        {
            vSend_UARTResponse(UART_CAN_BRIDGE_ACK, eUART_CAN_Bridge_Error_None);
        }
        else
        {
            vSend_UARTResponse(UART_CAN_BRIDGE_NACK, eUART_CAN_Bridge_Error_CANTxFail);
        }
        bUARTDebugNextFrame = false;
    }
}

static void vThread_UART_CAN_Bridge_CANRx( void *p1, void *p2, void *p3 )
{
    struct can_frame stFrame;

    while(1)
    {
        if(k_msgq_get(&msgq_UART_CANBridgeRx, &stFrame, K_FOREVER) != 0)
            continue;

        vSend_UARTFrameToPC(&stFrame);
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
        UART_CAN_BRIDGE_SOF_BYTE_0,
        UART_CAN_BRIDGE_SOF_BYTE_1,
        UART_CAN_BRIDGE_SOF_BYTE_2,
        UART_CAN_BRIDGE_SOF_BYTE_3,
    };

    uint8_t uiByte = 0;
    uint8_t uiSOFIndex = 0;
    uint8_t uiaHeaderTail[UART_CAN_BRIDGE_HEADER_LENGTH - sizeof(uiaSOF)];
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
    if(!bRead_UARTBytesWithTimeout(uiaHeaderTail, sizeof(uiaHeaderTail), UART_CAN_BRIDGE_BYTE_TIMEOUT_MS))
    {
        vSend_UARTDebugByte('h');
        vSend_UARTResponse(UART_CAN_BRIDGE_NACK, eUART_CAN_Bridge_Error_InvalidLength);
        return false;
    }
    vSend_UARTDebugByte('H');

    if(uiaHeaderTail[0] != UART_CAN_BRIDGE_VERSION)
    {
        vSend_UARTDebugByte('v');
        vSend_UARTResponse(UART_CAN_BRIDGE_NACK, eUART_CAN_Bridge_Error_InvalidVersion);
        return false;
    }

    *puiCommand = uiaHeaderTail[1];
    *puiSeq = ((uint16_t)uiaHeaderTail[2] << 8) | uiaHeaderTail[3];
    *puiPayloadLen = ((uint16_t)uiaHeaderTail[4] << 8) | uiaHeaderTail[5];

    if(*puiPayloadLen > UART_CAN_BRIDGE_PAYLOAD_MAX_LENGTH)
    {
        vSend_UARTDebugByte('l');
        vSend_UARTResponse(UART_CAN_BRIDGE_NACK, eUART_CAN_Bridge_Error_InvalidLength);
        return false;
    }

    if(!bRead_UARTBytesWithTimeout(puiPayload, *puiPayloadLen, UART_CAN_BRIDGE_BYTE_TIMEOUT_MS))
    {
        vSend_UARTDebugByte('p');
        vSend_UARTResponse(UART_CAN_BRIDGE_NACK, eUART_CAN_Bridge_Error_InvalidLength);
        return false;
    }
    vSend_UARTDebugByte('P');
    if(!bRead_UARTBytesWithTimeout(uiaCRC, sizeof(uiaCRC), UART_CAN_BRIDGE_BYTE_TIMEOUT_MS))
    {
        vSend_UARTDebugByte('t');
        vSend_UARTResponse(UART_CAN_BRIDGE_NACK, eUART_CAN_Bridge_Error_InvalidLength);
        return false;
    }
    vSend_UARTDebugByte('T');

    uiFrameCRC = ((uint16_t)uiaCRC[0] << 8) | uiaCRC[1];
    uiCalculatedCRC = u16CRC16_CCITT_Update(uiCalculatedCRC, uiaHeaderTail, sizeof(uiaHeaderTail));
    uiCalculatedCRC = u16CRC16_CCITT_Update(uiCalculatedCRC, puiPayload, *puiPayloadLen);

    if(uiFrameCRC != uiCalculatedCRC)
    {
        vSend_UARTDebugByte('c');
        vSend_UARTResponse(UART_CAN_BRIDGE_NACK, eUART_CAN_Bridge_Error_CRCMismatch);
        return false;
    }

    vSend_UARTDebugByte('C');
    return true;
}

static bool bForward_FrameToCAN( uint8_t uiCommand, const uint8_t *puiPayload, uint16_t uiPayloadLen )
{
    uint8_t uiaCANPayload[CAN_MSG_MAX_SIZE];
    sT_CAN_TXMsg_t stTCANMsg;
    eT_CAN_TxResult eTxResult;

    if((uiPayloadLen + 1) > sizeof(uiaCANPayload))
        return false;

    uiaCANPayload[0] = uiCommand;
    if(uiPayloadLen > 0)
        memcpy(&uiaCANPayload[1], puiPayload, uiPayloadLen);

    stTCANMsg.uiID = CAN_RX_BOOTLOADER_ID;
    stTCANMsg.uiLen = uiPayloadLen + 1;
    stTCANMsg.puiData = uiaCANPayload;

    eTxResult = eFlexCAN_SendMsg(&stTCANMsg);
    if(eTxResult != eCAN_TxResult_Ok)
    {
        vSend_UARTDebugByte('f');
        UART_CAN_BRIDGE_Print("UART CAN Bridge: CAN tx failed %d\n\r", eTxResult);
        return false;
    }

    vSend_UARTDebugByte('F');
    return true;
}

static void vSend_UARTFrameToPC( const struct can_frame *pstFrame )
{
    uint8_t uiLen = 0;
    uint8_t uiPayloadLen = 0;
    uint8_t uiaHeaderTail[UART_CAN_BRIDGE_HEADER_LENGTH - 4];
    uint16_t uiCRC = 0xFFFF;

    if(pstFrame == NULL)
        return;

    uiLen = can_dlc_to_bytes(pstFrame->dlc);
    if(uiLen == 0)
        return;

    uiPayloadLen = uiLen - 1;
    uiaHeaderTail[0] = UART_CAN_BRIDGE_VERSION;
    uiaHeaderTail[1] = pstFrame->data[0];
    uiaHeaderTail[2] = (uiUART_CANBridgeTxSeq >> 8) & 0xFF;
    uiaHeaderTail[3] = uiUART_CANBridgeTxSeq & 0xFF;
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

    uiUART_CANBridgeTxSeq++;
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

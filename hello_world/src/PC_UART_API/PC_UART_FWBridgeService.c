#include "PC_UART_FWBridgeService.h"

#include <stdbool.h>
#include <string.h>

#include "../CAN_Controller/CAN_Controller.h"
#include "../Bootloader_Controller/Bootloader_TypeDef.h"
#include "CAN/NXP_CAN_ProjDef.h"

ePC_UART_Error_t ePC_UART_FWBridgeService_Process(
    uint8_t uiCommand,
    const uint8_t *puiPayload,
    uint16_t uiPayloadLen)
{
    if(uiCommand < (uint8_t)eBootloader_CMD_FWUpReq ||
       uiCommand >= (uint8_t)eNUMBER_OF_BOOTLOADER_COMMANDs)
    {
        return ePC_UART_Error_InvalidCommand;
    }

    if(uiPayloadLen > (CAN_MSG_MAX_SIZE - 1U) ||
       (uiPayloadLen > 0U && puiPayload == NULL))
    {
        return ePC_UART_Error_InvalidLength;
    }

    uint8_t uiaCANPayload[CAN_MSG_MAX_SIZE] = {0};
    uiaCANPayload[0] = uiCommand;
    if(uiPayloadLen > 0U)
    {
        memcpy(&uiaCANPayload[1], puiPayload, uiPayloadLen);
    }

    sT_CAN_TXMsg_t stTCANMsg = {
        .uiID = CAN_RX_BOOTLOADER_ID,
        .uiLen = uiPayloadLen + 1U,
        .puiData = uiaCANPayload,
    };

    return (eFlexCAN_SendMsg(&stTCANMsg) == eCAN_TxResult_Ok)
               ? ePC_UART_Error_None
               : ePC_UART_Error_CANTxFail;
}

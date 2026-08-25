#ifndef PC_UART_FW_BRIDGE_SERVICE_H
#define PC_UART_FW_BRIDGE_SERVICE_H

#include <stdint.h>

#include "PC_UART_Protocol.h"

ePC_UART_Error_t ePC_UART_FWBridgeService_Process(
    uint8_t uiCommand,
    const uint8_t *puiPayload,
    uint16_t uiPayloadLen);

#endif /* PC_UART_FW_BRIDGE_SERVICE_H */

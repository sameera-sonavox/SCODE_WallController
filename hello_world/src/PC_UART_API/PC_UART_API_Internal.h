#ifndef PC_UART_API_INTERNAL_H
#define PC_UART_API_INTERNAL_H

#include <stdint.h>

#include "PC_UART_Protocol.h"

/* Internal entry point shared by the UART receive thread and firmware-side
 * protocol validation. Application modules should use PC_UART_API.h only.
 */
ePC_UART_Error_t ePC_UART_API_DispatchFrame(
    uint8_t uiCommand,
    const uint8_t *puiPayload,
    uint16_t uiPayloadLen);

#endif /* PC_UART_API_INTERNAL_H */

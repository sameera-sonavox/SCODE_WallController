#ifndef PC_UART_BULK_FILE_SERVICE_H
#define PC_UART_BULK_FILE_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

#include "PC_UART_Protocol.h"

ePC_UART_Error_t ePC_UART_BulkFileService_Process(
    uint8_t uiCommand,
    const uint8_t *puiPayload,
    uint16_t uiPayloadLen,
    bool *pbOperationComplete);

bool bPC_UART_BulkFileService_IsSessionActive(void);

#endif /* PC_UART_BULK_FILE_SERVICE_H */

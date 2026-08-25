#ifndef PC_UART_PROTOCOL_H
#define PC_UART_PROTOCOL_H

#include <stdint.h>

/* Common PC-to-MCU UART frame: "BLUP" + version + command + sequence +
 * payload length + payload + CRC16-CCITT. Multi-byte fields are big-endian.
 */
#define PC_UART_PROTOCOL_VERSION                         1U
#define PC_UART_PROTOCOL_HEADER_LENGTH                  10U
#define PC_UART_PROTOCOL_BULK_MAX_DATA_LENGTH           256U
#define PC_UART_PROTOCOL_BULK_START_FIXED_LENGTH        8U
#define PC_UART_PROTOCOL_BULK_DATA_FIXED_LENGTH         8U
#define PC_UART_PROTOCOL_MAX_PAYLOAD_LENGTH             \
    (PC_UART_PROTOCOL_BULK_DATA_FIXED_LENGTH +          \
     PC_UART_PROTOCOL_BULK_MAX_DATA_LENGTH)

#define PC_UART_PROTOCOL_ACK                            0x06U
#define PC_UART_PROTOCOL_NACK                           0x15U

typedef enum
{
    ePC_UART_Operation_Idle = 0,
    ePC_UART_Operation_FirmwareBridge = 1,
    ePC_UART_Operation_LocalBulkFile = 2,
    eNUMBER_OF_PC_UART_OPERATIONS
} ePC_UART_Operation_t;

typedef enum
{
    ePC_UART_Command_SelectOperation = 0x01,
    ePC_UART_Command_ReleaseOperation = 0x02,

    /* Values 20 through 26 remain the existing bootloader commands and are
     * valid only while FirmwareBridge is selected.
     */

    ePC_UART_Command_BulkStart = 0x40,
    ePC_UART_Command_BulkData = 0x41,
    ePC_UART_Command_BulkEnd = 0x42,
} ePC_UART_Command_t;

typedef enum
{
    /* Preserve the first five values used by the existing firmware GUI. */
    ePC_UART_Error_None = 0,
    ePC_UART_Error_InvalidVersion,
    ePC_UART_Error_InvalidLength,
    ePC_UART_Error_FrameCRCMismatch,
    ePC_UART_Error_CANTxFail,

    ePC_UART_Error_InvalidCommand,
    ePC_UART_Error_InvalidOperation,
    ePC_UART_Error_OperationBusy,
    ePC_UART_Error_ServiceUnavailable,
    ePC_UART_Error_QueueFull,
    ePC_UART_Error_InvalidFrame,
    ePC_UART_Error_BulkStartFailed,
    ePC_UART_Error_BulkTransferFailed,
    ePC_UART_Error_BulkTimeout,
    ePC_UART_Error_BulkCRCMismatch,
    ePC_UART_Error_BulkSizeMismatch,
    eNUMBER_OF_PC_UART_ERRORS
} ePC_UART_Error_t;

/* Bulk START payload:
 *   [0]      file type (eFileType_t)
 *   [1..4]   complete file size
 *   [5..6]   complete-file CRC16-CCITT
 *   [7]      file-name length N
 *   [8..]    N file-name bytes, without a trailing NUL
 *
 * Bulk DATA payload:
 *   [0..3]   monotonically increasing frame ID, starting at zero
 *   [4..5]   data length N (1..256)
 *   [6..7]   CRC16-CCITT of the N data bytes
 *   [8..]    N data bytes
 *
 * Bulk END has an empty payload.
 */

#endif /* PC_UART_PROTOCOL_H */

#ifndef WALLCONTROLLER_COMM_TYPES_H
#define WALLCONTROLLER_COMM_TYPES_H

#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <zephyr/kernel.h>
#include "CAN/NXP_CAN_API.h"

/* Preserve the shared wire values. IDs 20 and 24 belonged to AMP-only
 * commands and are not dispatched by the wall controller. */
typedef enum
{
    eSysMgmt_CMD_None = 0,
    eSysMgmt_CMD_Reserved_20 = 20,
    eSysMgmt_CMD_ACK_NACK = 21,
    eSysMgmt_CMD_Ret_InvalidMsgFormat = 22,
    eSysMgmt_CMD_Undefined = 23,
    eSysMgmt_CMD_Reserved_24 = 24,
    eNUMBER_OF_SYS_MANAGEMENT_COMMANDs = 25
} eT_SysMgmtCommand_t;

typedef enum
{
    eSysMgmt_Error_None = 0,
    eSysMgmt_Error_InvalidCommand,
    eSysMgmt_Error_InvalidFrameLength,
    eSysMgmt_Error_InvalidPayLoadLength,
    eSysMgmt_Error_PayLoadLengthMismatch,
    eSysMgmt_Error_PayLoadLength_NotDefined,
    eSysMgmt_Error_InvalidTranscId,
    eSysMgmt_Error_InvalidPointer,
    eSysMgmt_Error_NoPendingTransaction,
    eSysMgmt_Error_InvalidACKType,
    eSysMgmt_Error_NoWaitForACK,
    eSysMgmt_Error_InvalidRev_ForACK,
    eSysMgmt_Error_InvalidHostFeedback,
    eSysMgmt_Error_Inconsistent_ACKError,
    eSysMgmt_Error_NoFreeTransaction,
    eSysMgmt_Error_TransactionStateWrtie,
    eSysMgmt_Error_TransactionMaxRetryReach,
    eSysMgmt_Error_InvalidCANTxMessage,
    eSysMgmt_Error_TransactionLocked,
    eSysMgmt_Error_InvalidRetransmitRequest,
    eSysMgmt_Error_InvalidLoadSetting,
    eSysMgmt_Error_InvalidPowerSetting,
    eSysMgmt_Error_DestMutexTimeOut,
    eSysMgmt_Error_NullPointerRef,
    eNUMBER_OF_SYS_MANAGEMENT_ERROR_CODEs
} eT_SysMgmtErrorCode_t;

typedef enum
{
    eTransType_None = 0,
    eTransType_Receive,
    eTransType_Transmit,
    eNUMBER_OF_TRANSACTION_TYPEs
} eT_TransactionType_t;

typedef enum
{
    eTransState_Free,
    eTransState_Queued,
    eTransState_Invalid,
    eTransState_WaitingFor_TxCompletion,
    eTransState_WaitingFor_ACK,
    eTransState_Dest_ACK,
    eTransState_Failed,
    eTransState_PrepareRetransmit,
    eNUMBER_OF__TRANSACTION_STATEs
} eT_TransactionState_t;

typedef struct
{
    eT_TransactionType_t eType;
    _Atomic eT_TransactionState_t eState;
    _Atomic eT_SysMgmtCommand_t eCommand;
    eT_SysMgmtErrorCode_t eErrorCode;
    bool bIsACKRequired;
    struct k_mutex kMutex_TransactionClaim;
    uint64_t uiTrigTime_ms;
    uint64_t uiStartTime_ms;
    uint8_t uiRetryCount;
    uint8_t uiMaxRetryCount;
    uint8_t uiRevision;
    uint32_t uiGeneration;
} sT_CmdTransaction_t;

typedef struct
{
    eT_SysMgmtErrorCode_t eErrorCode;
    sT_CmdTransaction_t *pstTransaction;
} sT_TransManager_SubsricptionResult_t;

typedef struct
{
    uint32_t uiGeneration;
    eT_SysMgmtCommand_t eCommand;
    uint8_t uiTransactionIndex;
    uint8_t uiRevision;
} sT_CAN_TxContext_t;

typedef struct
{
    eT_SysMgmtCommand_t eCMD;
    bool bIsMsgOk;
    bool bIsACKReq;
    eT_SysMgmtErrorCode_t eErrorCode;
    uint8_t uiLen;
    uint8_t uiaData[CAN_MSG_MAX_SIZE];
} sT_SysMgmt_CtrlMsg_t;

#endif /* WALLCONTROLLER_COMM_TYPES_H */

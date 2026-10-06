#ifndef WALLCONTROLLER_COMM_MANAGER_H
#define WALLCONTROLLER_COMM_MANAGER_H

#include "WallController_Comm_Types.h"

#define TRANSACTION_BACKOFF_TIME_ms          50U
#define TRANSACTION_MUTEX_LOCK_WAIT_TIME_ms  2U

/* Application callbacks replace only the AMP-specific command/state-machine
 * handoffs. Register once before initializing the communication manager. */
typedef struct
{
    int (*iGetPayloadLength)(eT_SysMgmtCommand_t eCMD);
    void (*vDispatchCommand)(sT_SysMgmt_CtrlMsg_t *pstMessage);
    bool (*bRequestRetransmit)(sT_CmdTransaction_t *pstTransaction);
    void (*vTransactionResult)(sT_CmdTransaction_t *pstTransaction);
    void (*vTransmissionFailure)(eT_SysMgmtCommand_t eCMD);
    void (*vCANPermanentFailure)(void);
} sT_WallController_CommCallbacks_t;

extern bool bInit_WallController_Comm_Manager(
    const sT_WallController_CommCallbacks_t *pstCallbacks);
extern void vDeInit_WallController_Comm_Manager(void);
extern bool bIs_WallControllerCANPermanentFailure(void);
extern void vNotify_MessageTxComplete(sT_CAN_TxCompletion_t stTTxResult);
extern void vSend_ACK(uint32_t uiDestID, uint8_t uiCommand, uint8_t uiRev);

extern sT_TransManager_SubsricptionResult_t stSubscribe_To_TransactionManager(
    sT_CAN_TXMsg_t *pstTxMessage, bool bIsACKReq,
    uint8_t uiMaxRetryCount, uint64_t uiTrigTime_ms);
extern eT_SysMgmtErrorCode_t eSet_TransactionState_Extern(
    sT_CmdTransaction_t *pstTransaction, eT_TransactionState_t eState);
extern eT_TransactionState_t eGet_TransactionState_Extern(
    sT_CmdTransaction_t *pstTransaction);
extern sT_CmdTransaction_t *pstFindAndLockTransaction_ByCMDId_Extern(
    eT_SysMgmtCommand_t eCMD);
extern bool bManage_TransmissionError(eT_CAN_TxSubmitResult eTxResult,
                                      sT_CmdTransaction_t *pstTransaction);
extern void vSetTransaction_Free(sT_CmdTransaction_t *pstTransaction);
extern void vUpdate_CommandRevision(eT_SysMgmtCommand_t eCMD, uint8_t uiNewVal);
extern uint8_t uiGet_CommandRevision(eT_SysMgmtCommand_t eCMD);
extern bool bGet_TransactionIndex(const sT_CmdTransaction_t *pstTransaction,
                                  uint8_t *puiIndex);

#endif /* WALLCONTROLLER_COMM_MANAGER_H */

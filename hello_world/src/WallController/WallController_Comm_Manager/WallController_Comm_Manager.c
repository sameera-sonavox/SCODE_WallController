#include "WallController_Comm_Manager.h"
#include "../../CAN_Controller/CAN_Controller.h"

#include <zephyr/kernel.h>
#include "GenericMacro.h"

K_THREAD_STACK_DEFINE(thread_CANSysMgmtRxThread, CAN_SYSMGMT_THREAD_STACK_SIZE_BYTEs);
static struct k_thread stCANSysMgmtRxThread_t;
static k_tid_t kThread_SysMgmtId;

static bool bIsComManager_Running = false;
_Atomic bool bIsCriticalFailNoti_Subsribed = false;
sT_CmdTransaction_t staCMDTransactions[MAX_NUMBER_OF_TRANSACTION_TRACKINGs] = {0};
static _Atomic uint8_t uiaRevision[eNUMBER_OF_SYS_MANAGEMENT_COMMANDs] = {0};
static _Atomic bool bCANPermanentFailure = false;
static sT_WallController_CommCallbacks_t stCallbacks = {0};

static void vThread_SysManagementMsgHandler( void *p1, void *p2, void *p3 );
static void vHandle_SysMgmt_Messages( struct can_frame * pstTCANMsg, int imsgErr );
static eT_SysMgmtErrorCode_t eValidate_PayLoadLength(struct can_frame * pstTCANMsg,
                                                     sT_SysMgmt_CtrlMsg_t *pstTSysMgmtMsg_t);
static void vSend_NACK(uint32_t uiDestID, eT_SysMgmtErrorCode_t eErrorCode, uint8_t uiCommand, uint8_t uiRev);
static void vHandle_SysMgmt_MessageTracking(void);
static bool bNotify_ControllerToReTransmit(sT_CmdTransaction_t *pstTransaction);
static void vNotify_TransmissionFailure(eT_SysMgmtCommand_t eCMD);
static int iGet_CmdPayLoadLen(eT_SysMgmtCommand_t eCMD);

static void vSubscribe_ToCANController_CriticalFailures( void );
static void vUnsubsribe_FromCANController_CriticalFailures( void );
static void vCANBus_CriticalFailureCallback(eT_CAN_BUSState eBusState);
static inline bool bIsFailureNotifySubscribed( void );

void vDispatch_Message(sT_SysMgmt_CtrlMsg_t stTMessage);
static void vSubmit_To_TransManager_ACKMonitor(sT_SysMgmt_CtrlMsg_t *pstTMessage);
static void vSubmit_To_HandleInvalidMsgFormat(sT_SysMgmt_CtrlMsg_t *pstTMessage);
static void vReset_TransactionState(sT_CmdTransaction_t *pstTransaction);

static void vNotify_Controller_ForHostFeedback(sT_CmdTransaction_t *pstTransaction);
static void vNotify_Controller_ForHostNACK(sT_CmdTransaction_t *pstTransaction);
static bool bMessage_FormatError(eT_SysMgmtErrorCode_t eErrCode);
static void vNotify_MsgFormatError_ToHost(uint32_t uiDestID, eT_SysMgmtCommand_t eCommand, uint8_t uiRev, eT_SysMgmtErrorCode_t eErrCode);
static void vNotify_Controller_TransactionAbort(sT_CmdTransaction_t *pstTransaction);

static inline eT_SysMgmtErrorCode_t eSet_TransactionState(sT_CmdTransaction_t *pstTransaction, eT_TransactionState_t eState);
static inline sT_CmdTransaction_t *pstClaimFreeTransaction( void );
static inline sT_CmdTransaction_t *pstFindAndLockTransaction_ByCMDId(eT_SysMgmtCommand_t eCMD, eT_SysMgmtErrorCode_t *pstErrorCode);
static inline eT_TransactionState_t eGetTransactionState(sT_CmdTransaction_t *pstTRansaction);
static inline eT_SysMgmtCommand_t eGet_TransactionCommand(sT_CmdTransaction_t *pstTRansaction);
static inline void vSet_TransactionCommand(sT_CmdTransaction_t *pstTRansaction, eT_SysMgmtCommand_t eCMD);

bool bInit_WallController_Comm_Manager(const sT_WallController_CommCallbacks_t *pstCallbacks)
{
    if(bIsComManager_Running)
    {
        return true;
    }

    stCallbacks = (pstCallbacks != NULL) ? *pstCallbacks : (sT_WallController_CommCallbacks_t){0};
    atomic_store(&bCANPermanentFailure, false);
    for(uint8_t i = 0; i < MAX_NUMBER_OF_TRANSACTION_TRACKINGs; i++)
    {
        staCMDTransactions[i].eCommand = eSysMgmt_CMD_None;
        staCMDTransactions[i].eErrorCode = eSysMgmt_Error_None;
        eSet_TransactionState(&staCMDTransactions[i], eTransState_Free);
        staCMDTransactions[i].eType = eTransType_None;
        staCMDTransactions[i].uiMaxRetryCount = 0U;
        staCMDTransactions[i].uiRevision = 0U;
        staCMDTransactions[i].uiGeneration = 0U;
        staCMDTransactions[i].uiStartTime_ms = 0U;
        staCMDTransactions[i].uiTrigTime_ms = 0U;
        staCMDTransactions[i].bIsACKRequired = false;

        k_mutex_init(&staCMDTransactions[i].kMutex_TransactionClaim);
    }

    
    kThread_SysMgmtId = k_thread_create(&stCANSysMgmtRxThread_t, thread_CANSysMgmtRxThread, 
                                        K_THREAD_STACK_SIZEOF(thread_CANSysMgmtRxThread),
                                        vThread_SysManagementMsgHandler,
                                        NULL, NULL, NULL, CAN_SYSMGMT_THREAD_PRIORITY,
                                        0, K_NO_WAIT);

    bIsCriticalFailNoti_Subsribed = false;
    bIsComManager_Running = true;
    return true;
}

void vDeInit_WallController_Comm_Manager( void )
{
    if(!bIsComManager_Running)
    {
        return;
    }

    vUnsubsribe_FromCANController_CriticalFailures();

    k_thread_abort(kThread_SysMgmtId);
    bIsComManager_Running = false;    
}


static void vThread_SysManagementMsgHandler( void *p1, void *p2, void *p3 )
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    struct can_frame stTCANMsg;

    while(1)
    {
        if(bIsCANStackInitialized())
        {
            vSubscribe_ToCANController_CriticalFailures();
        }

        int ret = iGet_CANSysMgmt_Message(&stTCANMsg, K_MSEC(20));
        if(ret == 0)
        {
            vHandle_SysMgmt_Messages(&stTCANMsg, ret);
        }

        vHandle_SysMgmt_MessageTracking();      
    }
}

static void vSubscribe_ToCANController_CriticalFailures( void )
{
    bool bIsSubsribed = atomic_exchange_explicit(&bIsCriticalFailNoti_Subsribed, true, memory_order_acq_rel);
    if(bIsSubsribed)
    {
        return;
    }

    vSubscribe_ToCANBusCriticalFailure( vCANBus_CriticalFailureCallback );
}

static void vUnsubsribe_FromCANController_CriticalFailures( void )
{
    vUnSubscribe_FromCANBusCriticalFailure();
    atomic_store_explicit(&bIsCriticalFailNoti_Subsribed, false, memory_order_release);
}

static inline bool bIsFailureNotifySubscribed( void )
{
    bool bissubscribed = atomic_load_explicit(&bIsCriticalFailNoti_Subsribed, memory_order_acquire);
    return bissubscribed;
}

static void vCANBus_CriticalFailureCallback(eT_CAN_BUSState eBusState)
{
    if(!bIsFailureNotifySubscribed())
    {
        FHALT("Invalid Call");
        return;
    }

    if(eBusState != eCAN_BUS_Permanent_Fault)
    {
        return;
    }
    
    atomic_store(&bCANPermanentFailure, true);
    if(stCallbacks.vCANPermanentFailure != NULL)
    {
        stCallbacks.vCANPermanentFailure();
    }
    //Should it clear the transactions?
    //Should it avoid accepting any transaction?
}

bool bIs_WallControllerCANPermanentFailure(void)
{
    return atomic_load(&bCANPermanentFailure);
}

void vNotify_MessageTxComplete( sT_CAN_TxCompletion_t stTTxResult )
{
    sT_CAN_TxContext_t stTContext = {0};
    memcpy(&stTContext, stTTxResult.stTUserData.uiaCANUserData, sizeof(sT_CAN_TxContext_t));
    //printf("Tx Completed @Result : %d & AppErrorCode: %d", stTTxResult.iDriverError, stTTxResult.eResult);

    if(stTContext.uiGeneration == 0U ||
       stTContext.uiTransactionIndex >= MAX_NUMBER_OF_TRANSACTION_TRACKINGs)
    {
        return;
    }

    eT_SysMgmtErrorCode_t eError = eSysMgmt_Error_None;
    sT_CmdTransaction_t *pstTransaction = &staCMDTransactions[stTContext.uiTransactionIndex];
    k_mutex_lock(&pstTransaction->kMutex_TransactionClaim, K_FOREVER);

    if(eGetTransactionState(pstTransaction) != eTransState_WaitingFor_TxCompletion ||
       eGet_TransactionCommand(pstTransaction) != stTContext.eCommand ||
       pstTransaction->uiGeneration != stTContext.uiGeneration ||
       pstTransaction->uiRevision != stTContext.uiRevision)
    {
        k_mutex_unlock(&pstTransaction->kMutex_TransactionClaim);
        FWARN("Stale CAN Tx completion ignored @Slot: %d, CMD: %d, Rev: %d",
              stTContext.uiTransactionIndex, stTContext.eCommand, stTContext.uiRevision);
        return;
    }

    switch(stTTxResult.eResult)
    {
        case eCAN_TxComplete_Success:
            if(!pstTransaction->bIsACKRequired)
            {  
                k_mutex_unlock(&pstTransaction->kMutex_TransactionClaim);
                vSetTransaction_Free(pstTransaction);
                return;
            }
            else
            {
                pstTransaction->uiStartTime_ms = k_uptime_get();
                eError = eSet_TransactionState(pstTransaction, eTransState_WaitingFor_ACK);
                
                if(eError != eSysMgmt_Error_None)
                {
                    k_mutex_unlock(&pstTransaction->kMutex_TransactionClaim);
                    vSetTransaction_Free(pstTransaction);
                    vNotify_TransmissionFailure(stTContext.eCommand);
                    FHALT("Transaction State Couldn't be set @State: %d", eTransState_WaitingFor_ACK);
                    return;
                }
            }            
            break;
        case eCAN_TxComplete_MailboxTimeout:
        case eCAN_TxComplete_CompletionTimeout:
        case eCAN_TxComplete_ArbitrationLost:
        case eCAN_TxComplete_ControllerStopped:
        case eCAN_TxComplete_BusOff:
            pstTransaction->uiStartTime_ms = k_uptime_get();
            eError = eSet_TransactionState(pstTransaction, eTransState_Queued);
            if(eError != eSysMgmt_Error_None)
            {
                k_mutex_unlock(&pstTransaction->kMutex_TransactionClaim);
                vSetTransaction_Free(pstTransaction);
                vNotify_TransmissionFailure(stTContext.eCommand);
                FHALT("Transaction State Couldn't be set @State: %d", eTransState_Queued);
                return;
            }            
            break;
        case eCAN_TxComplete_InvalidFrame:
        case eCAN_TxComplete_Unsupported:
        case eCAN_TxComplete_HardwareError:
        case eCAN_TxComplete_UnknownError:
            k_mutex_unlock(&pstTransaction->kMutex_TransactionClaim);
            vSetTransaction_Free(pstTransaction);
            vNotify_TransmissionFailure(stTContext.eCommand);            
            return;
        default:
            if(stTTxResult.iDriverError < 0)
            {
                FHALT("Undefined Driver Error: %d", stTTxResult.iDriverError);
                if(!pstTransaction->bIsACKRequired)
                {  
                    k_mutex_unlock(&pstTransaction->kMutex_TransactionClaim);
                    vSetTransaction_Free(pstTransaction);
                }
                else
                {
                    k_mutex_unlock(&pstTransaction->kMutex_TransactionClaim);
                    vSetTransaction_Free(pstTransaction);
                    vNotify_TransmissionFailure(stTContext.eCommand);
                }
                
                return;               
            }
            break;       
    }
    
    k_mutex_unlock(&pstTransaction->kMutex_TransactionClaim);
}

static void vNotify_TransmissionFailure(eT_SysMgmtCommand_t eCMD)
{
    if(stCallbacks.vTransmissionFailure != NULL)
    {
        stCallbacks.vTransmissionFailure(eCMD);
        return;
    }
    FHALT("Transmission Failure Mechanism Not Implemented for Command : %d", eCMD);
}

static void vHandle_SysMgmt_MessageTracking(void)
{
    for(uint8_t i = 0; i < MAX_NUMBER_OF_TRANSACTION_TRACKINGs; i++)
    {
        bool bRetransmit = false;
        sT_CmdTransaction_t *pstTransaction = &staCMDTransactions[i];

        if(k_mutex_lock(&pstTransaction->kMutex_TransactionClaim, K_NO_WAIT) != 0)
        {
            continue;
        }

        eT_TransactionState_t eState = eGetTransactionState(pstTransaction);
        if(eState != eTransState_WaitingFor_ACK && eState != eTransState_Queued)
        {
            k_mutex_unlock(&pstTransaction->kMutex_TransactionClaim);
            continue;
        }

        uint64_t uiTime_ms = k_uptime_get();
        if(bIsTimeOut(staCMDTransactions[i].uiStartTime_ms, uiTime_ms, staCMDTransactions[i].uiTrigTime_ms))
        {
            eSet_TransactionState(pstTransaction, eTransState_PrepareRetransmit);
            bRetransmit = true;  
        }
        k_mutex_unlock(&pstTransaction->kMutex_TransactionClaim);

        if(bRetransmit)
        {            
            bool bIsDispatch = bNotify_ControllerToReTransmit(&staCMDTransactions[i]);

            if(!bIsDispatch)
            {
                bool bTransactionMarkedFailed = false;

                if(k_mutex_lock(&pstTransaction->kMutex_TransactionClaim, K_FOREVER) == 0)
                {
                    if(eGetTransactionState(pstTransaction) == eTransState_PrepareRetransmit)
                    {
                        pstTransaction->eErrorCode = eSysMgmt_Error_InvalidRetransmitRequest;
                        eSet_TransactionState(pstTransaction, eTransState_Failed);
                        bTransactionMarkedFailed = true;
                    }

                    k_mutex_unlock(&pstTransaction->kMutex_TransactionClaim);
                }

                if(bTransactionMarkedFailed)
                {
                    vNotify_Controller_ForHostFeedback(pstTransaction);
                }
            }
        }
    }
}

static bool bNotify_ControllerToReTransmit(sT_CmdTransaction_t *pstTransaction)
{
    if(pstTransaction == NULL)
    {
        FHALT("Null Pointer reference");
        return false;
    }

    if(stCallbacks.bRequestRetransmit != NULL)
    {
        return stCallbacks.bRequestRetransmit(pstTransaction);
    }
    FHALT("Command Delegation to Controller not implemented for Command : %d", eGet_TransactionCommand(pstTransaction));
    return false;
}

static void vHandle_SysMgmt_Messages( struct can_frame * pstTCANMsg, int imsgErr )
{   
    if(imsgErr != 0 || pstTCANMsg->id != (uint32_t)iGetCAN_RxFilterID(eCAN_RxFilter_ID_SysMgt))
    {
        return;
    }

    sT_SysMgmt_CtrlMsg_t stTSysMgmtMsg_t = {0};

    uint32_t uiDestID = uiGetCAN_DestID(eCAN_DestNode_RxFilt_SysMgmt);
    if(uiDestID == UINT32_MAX)
    {
        FHALT("Dest ID not definedd for DestFilter: %d", eCAN_DestNode_RxFilt_SysMgmt);
        return;
    }

    eT_SysMgmtErrorCode_t eErrorCode = eValidate_PayLoadLength(pstTCANMsg, &stTSysMgmtMsg_t);
    if(eErrorCode != eSysMgmt_Error_None)
    {
        bool bTerminal = (can_dlc_to_bytes(pstTCANMsg->dlc) >= 1U &&
                           pstTCANMsg->data[0] == eSysMgmt_CMD_Ret_InvalidMsgFormat);//If the Command is a response of 'eSysMgmt_CMD_Ret_InvalidMsgFormat'
        if(!bTerminal)                                                              //We should not return another NACK to create a loop
        {
            vSend_NACK(uiDestID, eErrorCode, stTSysMgmtMsg_t.eCMD, 0);//Need to check here
        }        
        return;
    }

    memcpy(stTSysMgmtMsg_t.uiaData, &pstTCANMsg->data[2], stTSysMgmtMsg_t.uiLen);
    vDispatch_Message(stTSysMgmtMsg_t);
}

void vDispatch_Message(sT_SysMgmt_CtrlMsg_t stTMessage)
{
    switch(stTMessage.eCMD)
    {
        case eSysMgmt_CMD_ACK_NACK://Nack or ACK received from the Host
            vSubmit_To_TransManager_ACKMonitor(&stTMessage);
            break;
        case eSysMgmt_CMD_Ret_InvalidMsgFormat:
            vSubmit_To_HandleInvalidMsgFormat(&stTMessage);
            break;
        default:
            if(stCallbacks.vDispatchCommand != NULL)
            {
                stCallbacks.vDispatchCommand(&stTMessage);
                return;
            }
            FHALT("Command[%d] not implemented.", stTMessage.eCMD);
            return;
    }
}

static void vSubmit_To_HandleInvalidMsgFormat(sT_SysMgmt_CtrlMsg_t *pstTMessage)
{
    if(pstTMessage == NULL)
    {
        FHALT("Null Pointer Reference");
        return;        
    }
    eT_SysMgmtErrorCode_t eErrCode = eSysMgmt_Error_None;

    eT_SysMgmtCommand_t eCMD = (eT_SysMgmtCommand_t)pstTMessage->uiaData[0];//Get the source command ID here
    if(eCMD == eSysMgmt_CMD_None || eCMD >= eNUMBER_OF_SYS_MANAGEMENT_COMMANDs)
    {
        FHALT("Invalid Src Command: %d", eCMD);
        return;        
    }

    sT_CmdTransaction_t *pstTransaction = pstFindAndLockTransaction_ByCMDId(eCMD, &eErrCode);
    if(pstTransaction == NULL)
    {
        FHALT("Null Pointer Reference");
        return;
    }

    if(pstTransaction->uiRevision != pstTMessage->uiaData[1])
    {
        uint8_t uiRevision = pstTransaction->uiRevision;
        k_mutex_unlock(&pstTransaction->kMutex_TransactionClaim);
        FWARN("CMD[%d] : ACK Rev. Mismatch(Expected: %d, Received: %d)", 
                eCMD, uiRevision, pstTMessage->uiaData[1]);
        return; 
    }

    eT_TransactionState_t eState = eGetTransactionState(pstTransaction);
    if(eState != eTransState_WaitingFor_ACK)
    {
        k_mutex_unlock(&pstTransaction->kMutex_TransactionClaim);
        FHALT("Sys Current State: %d @CMD: %d", eState, eCMD);
        return;          
    }

    eErrCode = (pstTMessage->uiaData[2] < eNUMBER_OF_SYS_MANAGEMENT_ERROR_CODEs)?
               (eT_SysMgmtErrorCode_t)pstTMessage->uiaData[2]: eNUMBER_OF_SYS_MANAGEMENT_ERROR_CODEs; 
    if(eErrCode == eNUMBER_OF_SYS_MANAGEMENT_ERROR_CODEs || 
       eErrCode == eSysMgmt_Error_None)
    {
        k_mutex_unlock(&pstTransaction->kMutex_TransactionClaim);
        FHALT("Invalid Error Code: %d @CMD: %d", pstTMessage->uiaData[2], eCMD);
        return;        
    }

    pstTransaction->eErrorCode = eErrCode;
    vNotify_Controller_TransactionAbort(pstTransaction);
}

static void vSubmit_To_TransManager_ACKMonitor(sT_SysMgmt_CtrlMsg_t *pstTMessage)
{
    uint32_t uiDestID = uiGetCAN_DestID(eCAN_DestNode_RxFilt_SysMgmt);
    if(pstTMessage == NULL)
    {
        FHALT("Null Pointer Reference");
        return;
    }

    eT_SysMgmtErrorCode_t eErrCode = eSysMgmt_Error_None;

    eT_SysMgmtCommand_t eCMD = (eT_SysMgmtCommand_t)pstTMessage->uiaData[0];//Get the source command ID here
    if(eCMD == eSysMgmt_CMD_None || eCMD >= eNUMBER_OF_SYS_MANAGEMENT_COMMANDs)
    {
        vSend_NACK(uiDestID, eSysMgmt_Error_InvalidCommand, eSysMgmt_CMD_Undefined, pstTMessage->uiaData[1]);
        FHALT("Invalid Src Command: %d", eCMD);
        return;        
    }

    sT_CmdTransaction_t *pstTransaction = pstFindAndLockTransaction_ByCMDId(eCMD, &eErrCode);
    if(pstTransaction == NULL)
    {
        FHALT("Null Pointer Reference");
        return;
    }

    eT_SysMgmtCommand_t eTempCMD = eGet_TransactionCommand(pstTransaction);
    if(eTempCMD != eCMD)
    {
        k_mutex_unlock(&pstTransaction->kMutex_TransactionClaim);
        FHALT("Transaction command mismatch. Expected: %d, Received: %d", eTempCMD, eCMD);
        return;
    }

    eT_TransactionState_t eTempState = eGetTransactionState(pstTransaction);
    if(eTempState != eTransState_WaitingFor_ACK)
    {
        k_mutex_unlock(&pstTransaction->kMutex_TransactionClaim);
        FHALT("Sys Current State: %d @CMD: %d", eTempState, eCMD);
        return;        
    }

    uint8_t uiRecvRevision = pstTMessage->uiaData[1];

    eT_CANACK_Ctrl eACKControl = (pstTMessage->uiaData[2] < eNUMBER_OF_CAN_ACKTYPEs && pstTMessage->uiaData[2] >= eCAN_ACK)?
                                 (eT_CANACK_Ctrl)pstTMessage->uiaData[2]: eNUMBER_OF_CAN_ACKTYPEs;
    if(eACKControl == eNUMBER_OF_CAN_ACKTYPEs)
    {
        k_mutex_unlock(&pstTransaction->kMutex_TransactionClaim);
        FHALT("Invalid ACK Type: %d @CMD: %d", pstTMessage->uiaData[2], eCMD);
        return;        
    }

    uint8_t uiTempRev = pstTransaction->uiRevision;
    if(uiTempRev != uiRecvRevision)
    {
        k_mutex_unlock(&pstTransaction->kMutex_TransactionClaim);        
        FWARN("CMD[%d] : ACK Rev. Mismatch(Expected: %d, Received: %d)", 
                eCMD, uiTempRev, uiRecvRevision);
        return;        
    }

    eErrCode = (pstTMessage->uiaData[3] < eNUMBER_OF_SYS_MANAGEMENT_ERROR_CODEs)?
               (eT_SysMgmtErrorCode_t)pstTMessage->uiaData[3]: eNUMBER_OF_SYS_MANAGEMENT_ERROR_CODEs;    
    if(eErrCode == eNUMBER_OF_SYS_MANAGEMENT_ERROR_CODEs)
    {
        k_mutex_unlock(&pstTransaction->kMutex_TransactionClaim);
        FHALT("Invalid Error Code: %d @CMD: %d", pstTMessage->uiaData[3], eCMD);
        return;        
    }

    if ((eACKControl == eCAN_ACK && eErrCode != eSysMgmt_Error_None) ||
        (eACKControl == eCAN_NACK && eErrCode == eSysMgmt_Error_None))
    {
        k_mutex_unlock(&pstTransaction->kMutex_TransactionClaim);
        vNotify_MsgFormatError_ToHost(uiDestID, eCMD, uiRecvRevision, eSysMgmt_Error_Inconsistent_ACKError);
        FWARN("Inconsistent ACK/NACK and error code");
        return;
    }
    if(bMessage_FormatError(eErrCode))
    {
        vNotify_MsgFormatError_ToHost(uiDestID, eCMD, uiRecvRevision, eErrCode);
        pstTransaction->eErrorCode = eErrCode;
        vNotify_Controller_TransactionAbort(pstTransaction);
        return;
    }

    pstTransaction->eErrorCode = eErrCode;
    if(eACKControl == eCAN_ACK)
    {
        eSet_TransactionState(pstTransaction, eTransState_Dest_ACK);
        k_mutex_unlock(&pstTransaction->kMutex_TransactionClaim);
        
        vNotify_Controller_ForHostFeedback(pstTransaction);
    }
    else
    {
        vNotify_Controller_ForHostNACK(pstTransaction);
    }
}

static bool bMessage_FormatError(eT_SysMgmtErrorCode_t eErrCode)
{
    switch (eErrCode)
    {
        case eSysMgmt_Error_InvalidCommand:
        case eSysMgmt_Error_InvalidFrameLength:
        case eSysMgmt_Error_InvalidPayLoadLength:
        case eSysMgmt_Error_PayLoadLengthMismatch:
        case eSysMgmt_Error_PayLoadLength_NotDefined:
        case eSysMgmt_Error_InvalidTranscId:
        case eSysMgmt_Error_NoPendingTransaction:
        case eSysMgmt_Error_NoWaitForACK:
        case eSysMgmt_Error_InvalidRev_ForACK:
        case eSysMgmt_Error_InvalidHostFeedback:
            FHALT("Destination reported Invalid Message Format @Error: %d", eErrCode);
            return true;    
        default:
            return false;
    }
}

static void vNotify_Controller_TransactionAbort(sT_CmdTransaction_t *pstTransaction)
{
    if(pstTransaction == NULL)
    {
        FHALT("Null Pointer Reference");
        return;
    }

    eSet_TransactionState(pstTransaction, eTransState_Failed);
    k_mutex_unlock(&pstTransaction->kMutex_TransactionClaim);

    vNotify_Controller_ForHostFeedback(pstTransaction);
}

static void vNotify_MsgFormatError_ToHost(uint32_t uiDestID, eT_SysMgmtCommand_t eCommand, uint8_t uiRev, eT_SysMgmtErrorCode_t eErrCode)
{
    sT_CAN_TXMsg_t stTHostMsg = {0};

    uint8_t uiaData[5] = {0};
    uiaData[0] = eSysMgmt_CMD_Ret_InvalidMsgFormat;
    uiaData[1] = 3U;
    uiaData[2] = eCommand;
    uiaData[3] = uiRev;
    uiaData[4] = eErrCode;

    stTHostMsg.uiID = uiDestID;
    stTHostMsg.uiLen = 5U;
    stTHostMsg.puiData = uiaData;
    
    vSend_CANMessage(&stTHostMsg, NULL);
    if(stTHostMsg.eTxResult != eCAN_TxSubmit_Queued)
    {
        FHALT("MsgFormat Err. Notification Failed @Result: %d", stTHostMsg.eTxResult);        
    }

}

static void vNotify_Controller_ForHostFeedback(sT_CmdTransaction_t *pstTransaction)
{
    if(pstTransaction == NULL)
    {
        FHALT("Null Pointer Reference");
        return;
    }

    if(stCallbacks.vTransactionResult != NULL)
    {
        stCallbacks.vTransactionResult(pstTransaction);
        return;
    }
    FHALT("Command[%d] Not Implemented and Transaction will be freed", eGet_TransactionCommand(pstTransaction));
    vSetTransaction_Free(pstTransaction);
}

static void vNotify_Controller_ForHostNACK(sT_CmdTransaction_t *pstTransaction)
{
    if(pstTransaction->uiRetryCount < pstTransaction->uiMaxRetryCount)
    {
        switch(pstTransaction->eErrorCode)
        {
            case eSysMgmt_Error_InvalidCommand:
            case eSysMgmt_Error_InvalidFrameLength:
            case eSysMgmt_Error_InvalidPayLoadLength:
            case eSysMgmt_Error_PayLoadLengthMismatch:
            case eSysMgmt_Error_PayLoadLength_NotDefined:
            case eSysMgmt_Error_NoPendingTransaction:
            case eSysMgmt_Error_InvalidACKType:
            case eSysMgmt_Error_NoWaitForACK:
            case eSysMgmt_Error_InvalidRev_ForACK:
            case eSysMgmt_Error_Inconsistent_ACKError:
                   break;
            default:
                    k_mutex_unlock(&pstTransaction->kMutex_TransactionClaim);
                    return;
        }
        
    }

    eSet_TransactionState(pstTransaction, eTransState_Failed);
    k_mutex_unlock(&pstTransaction->kMutex_TransactionClaim);

    vNotify_Controller_ForHostFeedback(pstTransaction);
}

sT_TransManager_SubsricptionResult_t stSubscribe_To_TransactionManager(sT_CAN_TXMsg_t *pstTxMessage,
                                                                       bool bIsACKReq, 
                                                                       uint8_t uiMaxRetryCount,
                                                                       uint64_t uiTrigTime_ms)
{
    sT_TransManager_SubsricptionResult_t stTSubscriptionResult = {
        .eErrorCode = eSysMgmt_Error_None,
        .pstTransaction = NULL
    };

    if(pstTxMessage == NULL)
    {
        FHALT("Null Pointer Reference");
        stTSubscriptionResult.eErrorCode = eSysMgmt_Error_InvalidPointer;
        return stTSubscriptionResult;
    }

    if(pstTxMessage->uiLen < 1U || pstTxMessage->puiData == NULL)
    {
        FHALT("Null Pointer Reference");
        stTSubscriptionResult.eErrorCode = eSysMgmt_Error_InvalidCANTxMessage;
        return stTSubscriptionResult;        
    }

    eT_SysMgmtCommand_t eCMD = (eT_SysMgmtCommand_t)pstTxMessage->puiData[0];
    if(eCMD < eSysMgmt_CMD_Reserved_20 || eCMD >= eNUMBER_OF_SYS_MANAGEMENT_COMMANDs)
    {
        FHALT("Invalid Command: %d", eCMD);
        stTSubscriptionResult.eErrorCode = eSysMgmt_Error_InvalidCommand;
        return stTSubscriptionResult;
    }

    sT_CmdTransaction_t *pstTransaction = pstFindAndLockTransaction_ByCMDId(eCMD, &stTSubscriptionResult.eErrorCode);
    if(pstTransaction == NULL && stTSubscriptionResult.eErrorCode == eSysMgmt_Error_TransactionLocked)
    {
        return stTSubscriptionResult;
    }

    if(pstTransaction == NULL)
    {
        pstTransaction = pstClaimFreeTransaction();
        if(pstTransaction == NULL)
        {
            stTSubscriptionResult.eErrorCode = eSysMgmt_Error_NoFreeTransaction;
            return stTSubscriptionResult;
        }
        
        stTSubscriptionResult.pstTransaction = pstTransaction;

        pstTransaction->uiGeneration++;
        if(pstTransaction->uiGeneration == 0U)
        {
            pstTransaction->uiGeneration = 1U;
        }

        pstTransaction->eType = eTransType_Transmit;
        vSet_TransactionCommand(pstTransaction, eCMD);

        pstTransaction->bIsACKRequired = bIsACKReq;
        pstTransaction->uiRevision = uiGet_CommandRevision(eCMD);
        pstTransaction->uiRetryCount = 0;
        pstTransaction->uiMaxRetryCount = uiMaxRetryCount;
        pstTransaction->uiTrigTime_ms = uiTrigTime_ms;
        pstTransaction->eErrorCode = eSysMgmt_Error_None;
        
        stTSubscriptionResult.eErrorCode = eSet_TransactionState(pstTransaction, eTransState_WaitingFor_TxCompletion);
        if(stTSubscriptionResult.eErrorCode != eSysMgmt_Error_None)
        {
            k_mutex_unlock(&pstTransaction->kMutex_TransactionClaim);
            stTSubscriptionResult.eErrorCode = eSysMgmt_Error_TransactionStateWrtie;
            FHALT("Transaction @CMD[%d] State couldn't be set. Error : %d", eCMD, stTSubscriptionResult.eErrorCode);
            return stTSubscriptionResult;
        }

        k_mutex_unlock(&pstTransaction->kMutex_TransactionClaim);
    }
    else
    {
        eT_TransactionState_t eCurrentState = eGetTransactionState(pstTransaction);
        if(eCurrentState != eTransState_PrepareRetransmit)
        {
            k_mutex_unlock(&pstTransaction->kMutex_TransactionClaim);
            stTSubscriptionResult.eErrorCode = eSysMgmt_Error_InvalidRetransmitRequest;
            return stTSubscriptionResult;
        }

        stTSubscriptionResult.pstTransaction = pstTransaction;
        pstTransaction->uiRetryCount++;
        if(pstTransaction->uiRetryCount > pstTransaction->uiMaxRetryCount)
        {
            stTSubscriptionResult.eErrorCode = eSysMgmt_Error_TransactionMaxRetryReach;
            pstTransaction->eErrorCode = eSysMgmt_Error_TransactionMaxRetryReach;
            eSet_TransactionState(pstTransaction, eTransState_Failed);
            k_mutex_unlock(&pstTransaction->kMutex_TransactionClaim);
            return stTSubscriptionResult;
        }

        pstTransaction->uiRevision = uiGet_CommandRevision(eCMD);
        pstTransaction->eErrorCode = eSysMgmt_Error_None;

        stTSubscriptionResult.eErrorCode = eSet_TransactionState(pstTransaction, eTransState_WaitingFor_TxCompletion);
        if(stTSubscriptionResult.eErrorCode != eSysMgmt_Error_None)
        {
            stTSubscriptionResult.eErrorCode = eSysMgmt_Error_TransactionStateWrtie;
            pstTransaction->eErrorCode = eSysMgmt_Error_TransactionStateWrtie;
            k_mutex_unlock(&pstTransaction->kMutex_TransactionClaim);
            FHALT("Transaction @CMD[%d] State couldn't be set. Error : %d", eCMD, stTSubscriptionResult.eErrorCode);
            return stTSubscriptionResult;
        }

        k_mutex_unlock(&pstTransaction->kMutex_TransactionClaim);
    }

    return stTSubscriptionResult;
}

bool bManage_TransmissionError(eT_CAN_TxSubmitResult eTxResult, sT_CmdTransaction_t *pstTransaction)
{
    if(pstTransaction == NULL)
    {
        FHALT("Null Pointer Reference");
        return false;
    }

    switch (eTxResult)
    {
        case eCAN_TxSubmit_BusUnavailable:
        case eCAN_TxSubmit_QueueFull:
            if(k_mutex_lock(&pstTransaction->kMutex_TransactionClaim, K_MSEC(TRANSACTION_MUTEX_LOCK_WAIT_TIME_ms)) == 0)
            {
                if(eSet_TransactionState(pstTransaction, eTransState_Invalid) == eSysMgmt_Error_None)
                {
                    pstTransaction->uiTrigTime_ms += TRANSACTION_BACKOFF_TIME_ms;
                    pstTransaction->uiStartTime_ms = k_uptime_get();
                    eSet_TransactionState(pstTransaction, eTransState_Queued);
                    k_mutex_unlock(&pstTransaction->kMutex_TransactionClaim);
                    return true;
                }
                k_mutex_unlock(&pstTransaction->kMutex_TransactionClaim);
            }
            return false;
        case eCAN_TxSubmit_InvalidMsg:
            vReset_TransactionState(pstTransaction);
            return false;    
        default:
            return true;
    }
}

void vUpdate_CommandRevision(eT_SysMgmtCommand_t eCMD, uint8_t uiNewVal)
{
    if(eCMD < eSysMgmt_CMD_Reserved_20 || eCMD >= eNUMBER_OF_SYS_MANAGEMENT_COMMANDs)
    {
        FHALT("Invalid Command : %d", eCMD);
        return;
    }

    uiNewVal = (uiNewVal == 0U)? 1U: uiNewVal;
    atomic_store_explicit(&uiaRevision[eCMD], uiNewVal, memory_order_release);
}

uint8_t uiGet_CommandRevision(eT_SysMgmtCommand_t eCMD)
{
    if(eCMD < eSysMgmt_CMD_Reserved_20 || eCMD >= eNUMBER_OF_SYS_MANAGEMENT_COMMANDs)
    {
        FHALT("Invalid Command : %d", eCMD);
        return 1U;
    }

    uint8_t uiRev = atomic_load_explicit(&uiaRevision[eCMD], memory_order_acquire);
    return uiRev;     
}

void vSetTransaction_Free(sT_CmdTransaction_t *pstTransaction)
{
    if(pstTransaction == NULL)
    {
        FHALT("Null Pointer Reference");
        return;        
    }

    vReset_TransactionState(pstTransaction);
}

bool bGet_TransactionIndex(const sT_CmdTransaction_t *pstTransaction, uint8_t *puiIndex)
{
    if(pstTransaction == NULL || puiIndex == NULL)
    {
        return false;
    }

    for(uint8_t i = 0U; i < MAX_NUMBER_OF_TRANSACTION_TRACKINGs; i++)
    {
        if(pstTransaction == &staCMDTransactions[i])
        {
            *puiIndex = i;
            return true;
        }
    }

    return false;
}

static void vReset_TransactionState(sT_CmdTransaction_t *pstTransaction)
{
    if(pstTransaction == NULL)
    {
        FHALT("Null Pointer Reference");
        return;
    }

    k_mutex_lock(&pstTransaction->kMutex_TransactionClaim, K_FOREVER);

    eT_SysMgmtErrorCode_t eErrorCode = eSet_TransactionState(pstTransaction, eTransState_Invalid);
    if(eErrorCode != eSysMgmt_Error_None)
    {
        k_mutex_unlock(&pstTransaction->kMutex_TransactionClaim);
        FHALT("Transaction State cannot be set");
        return;
    }

    atomic_store_explicit(&pstTransaction->eCommand, eSysMgmt_CMD_None, memory_order_release);
    pstTransaction->bIsACKRequired = false;
    pstTransaction->eErrorCode = eSysMgmt_Error_None;
    pstTransaction->eType = eNUMBER_OF_TRANSACTION_TYPEs;
    pstTransaction->uiMaxRetryCount = 0U;
    pstTransaction->uiRetryCount = 0U;
    pstTransaction->uiRevision = 0U;
    pstTransaction->uiStartTime_ms = 0U;
    pstTransaction->uiTrigTime_ms = 0U;

    eSet_TransactionState(pstTransaction, eTransState_Free);
    k_mutex_unlock(&pstTransaction->kMutex_TransactionClaim);
}

static inline eT_SysMgmtErrorCode_t eSet_TransactionState(sT_CmdTransaction_t *pstTransaction, eT_TransactionState_t eState)
{
    if(pstTransaction == NULL)
    {
        FHALT("NULL Pointer Reference");
        return eSysMgmt_Error_InvalidPointer;
    }

    atomic_store_explicit(&pstTransaction->eState, eState, memory_order_release);
    return eSysMgmt_Error_None;
}

eT_SysMgmtErrorCode_t eSet_TransactionState_Extern(sT_CmdTransaction_t *pstTransaction, eT_TransactionState_t eState)
{
    if(pstTransaction == NULL)
    {
        FHALT("Null Pointer Reference");
        return eSysMgmt_Error_InvalidPointer;
    }

    k_mutex_lock(&pstTransaction->kMutex_TransactionClaim, K_FOREVER);
    eT_SysMgmtErrorCode_t eErrorCode = eSet_TransactionState(pstTransaction, eState);
    k_mutex_unlock(&pstTransaction->kMutex_TransactionClaim);
    return eErrorCode;
}

static inline sT_CmdTransaction_t *pstClaimFreeTransaction( void )
{
    for(uint8_t i = 0; i < MAX_NUMBER_OF_TRANSACTION_TRACKINGs; i++)
    {
        sT_CmdTransaction_t *pstTransaction = &staCMDTransactions[i];

        if(k_mutex_lock(&pstTransaction->kMutex_TransactionClaim, K_NO_WAIT) != 0)
        {
            continue;
        }

        eT_TransactionState_t eState = eGetTransactionState(pstTransaction);
        if(eState == eTransState_Free)
        {
            return pstTransaction;
        }

        k_mutex_unlock(&pstTransaction->kMutex_TransactionClaim);
    }

    return NULL;

}

static inline sT_CmdTransaction_t *pstFindAndLockTransaction_ByCMDId(eT_SysMgmtCommand_t eCMD, eT_SysMgmtErrorCode_t *pErrorCode)
{
    if(pErrorCode != NULL)
    {
        *pErrorCode = eSysMgmt_Error_None;
    }

    for(uint8_t i = 0; i < MAX_NUMBER_OF_TRANSACTION_TRACKINGs; i++)
    {
        sT_CmdTransaction_t *pstTransaction = &staCMDTransactions[i];

        eT_TransactionState_t eState = eGetTransactionState(pstTransaction);
        if(eState == eTransState_Free || eState == eTransState_Invalid)
        {
            continue;
        }

        if(eGet_TransactionCommand(pstTransaction) != eCMD)
        {
            continue;
        }

        if(k_mutex_lock(&pstTransaction->kMutex_TransactionClaim, K_MSEC(TRANSACTION_MUTEX_LOCK_WAIT_TIME_ms)) != 0)
        {
            if(pErrorCode != NULL)
            {
                *pErrorCode = eSysMgmt_Error_TransactionLocked;
            }            
            continue;
        }

        eState = eGetTransactionState(pstTransaction);
        if((eState != eTransState_Free && eState != eTransState_Invalid) &&
            eGet_TransactionCommand(pstTransaction) == eCMD)
        {
            if(pErrorCode != NULL)
            {
                *pErrorCode = eSysMgmt_Error_None;
            }         
            return pstTransaction;
        }

        k_mutex_unlock(&pstTransaction->kMutex_TransactionClaim);
    }
    return NULL;
}

sT_CmdTransaction_t *pstFindAndLockTransaction_ByCMDId_Extern(eT_SysMgmtCommand_t eCMD)
{
    eT_SysMgmtErrorCode_t eErrorCode = eSysMgmt_Error_None;
    sT_CmdTransaction_t *pstTransaction = pstFindAndLockTransaction_ByCMDId(eCMD, &eErrorCode);

    if(pstTransaction != NULL)
    {
        k_mutex_unlock(&pstTransaction->kMutex_TransactionClaim);
    }
    if(eErrorCode != eSysMgmt_Error_None)
    {
        return NULL;
    }
    return pstTransaction;

}

static inline eT_SysMgmtCommand_t eGet_TransactionCommand(sT_CmdTransaction_t *pstTRansaction)
{
    if(pstTRansaction == NULL)
    {
        return eSysMgmt_CMD_None;
    }

    eT_SysMgmtCommand_t eCMD = atomic_load_explicit(&pstTRansaction->eCommand, memory_order_acquire);
    return eCMD;
}

static inline void vSet_TransactionCommand(sT_CmdTransaction_t *pstTRansaction, eT_SysMgmtCommand_t eCMD)
{
    if(pstTRansaction == NULL || (eCMD < eSysMgmt_CMD_Reserved_20 || eCMD >= eNUMBER_OF_SYS_MANAGEMENT_COMMANDs))
    {
        FHALT("Invalid Arguement");
        return;
    }
    atomic_store_explicit(&pstTRansaction->eCommand, eCMD, memory_order_release);
}

static inline eT_TransactionState_t eGetTransactionState(sT_CmdTransaction_t *pstTRansaction)
{
    eT_TransactionState_t eState = atomic_load_explicit(&pstTRansaction->eState, memory_order_acquire);
    return eState;
}

eT_TransactionState_t eGet_TransactionState_Extern(sT_CmdTransaction_t *pstTransaction)
{
    return eGetTransactionState(pstTransaction);
}

static eT_SysMgmtErrorCode_t eValidate_PayLoadLength(struct can_frame * pstTCANMsg,
                                                     sT_SysMgmt_CtrlMsg_t *pstTSysMgmtMsg_t)
{

    uint8_t uiFrameLen = can_dlc_to_bytes(pstTCANMsg->dlc);//CAN Frame Length
    if(uiFrameLen < 2U)
    {
        FHALT("Invalid Frame Length: %d", uiFrameLen);
        return eSysMgmt_Error_InvalidFrameLength;        
    }
    
    uint8_t uiHostDefined_PayloadLen = pstTCANMsg->data[1];
    
    if(pstTCANMsg->data[0] < eSysMgmt_CMD_Reserved_20 ||
       pstTCANMsg->data[0] >= eNUMBER_OF_SYS_MANAGEMENT_COMMANDs)
    {
        FHALT("Invalid Command: %d", pstTCANMsg->data[0]);
        return eSysMgmt_Error_InvalidCommand;
    }

    pstTSysMgmtMsg_t->eCMD = (eT_SysMgmtCommand_t)pstTCANMsg->data[0];

    if(uiFrameLen < 2U || 
       uiFrameLen < (2U + uiHostDefined_PayloadLen))
    {        
        FHALT("Invalid FrameLength: %d @Command: %d", uiFrameLen, pstTSysMgmtMsg_t->eCMD);
        return eSysMgmt_Error_InvalidFrameLength;        
    }
    if(uiHostDefined_PayloadLen > (CAN_MSG_MAX_SIZE - 2U))
    {
        FHALT("Invalid PayloadLength: %d @Command: %d", uiHostDefined_PayloadLen, pstTSysMgmtMsg_t->eCMD);
        return eSysMgmt_Error_InvalidPayLoadLength; 
    }

    int iExpectedPayLen = iGet_CmdPayLoadLen(pstTSysMgmtMsg_t->eCMD);
    if(iExpectedPayLen < 0)
    {
        return eSysMgmt_Error_PayLoadLength_NotDefined;
    }

    if((uint8_t)iExpectedPayLen != uiHostDefined_PayloadLen)
    {        
        FHALT("Invalid PayloadLength: %d (Expected: %d) @Command: %d", 
                uiHostDefined_PayloadLen, iExpectedPayLen, pstTSysMgmtMsg_t->eCMD);
        return eSysMgmt_Error_PayLoadLengthMismatch; 
    }

    pstTSysMgmtMsg_t->uiLen = (uint8_t)iExpectedPayLen;
    return eSysMgmt_Error_None;

}

static int iGet_CmdPayLoadLen(eT_SysMgmtCommand_t eCMD)
{
    switch(eCMD)
    {
        case eSysMgmt_CMD_ACK_NACK:
            return 4;
        case eSysMgmt_CMD_Ret_InvalidMsgFormat:
            return 3;
        default:
            return (stCallbacks.iGetPayloadLength != NULL) ?
                stCallbacks.iGetPayloadLength(eCMD) : -1;
    }
}

static void vSend_NACK(uint32_t uiDestID, eT_SysMgmtErrorCode_t eErrorCode, uint8_t uiCommand, uint8_t uiRev)
{
    vSend_ACK_NACK(uiDestID, eCAN_NACK, 
                   eErrorCode, uiCommand, uiRev);
    
}

void vSend_ACK(uint32_t uiDestID, uint8_t uiCommand, uint8_t uiRev)
{
    vSend_ACK_NACK(uiDestID, eCAN_ACK, 
                   eSysMgmt_Error_None, uiCommand, uiRev);
    
}

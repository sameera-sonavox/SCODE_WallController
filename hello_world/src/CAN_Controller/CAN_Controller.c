#include "CAN_Controller.h"

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "GenericMacro.h"
#include "../Bootloader_Controller/Bootloader_Ctrl.h"
#include "../Bootloader_Controller/Bootloader_TypeDef.h"
#include "../PC_UART_API/PC_UART_API.h"
#include "../WallController/WallController_Comm_Manager/WallController_Comm_Manager.h"

K_MSGQ_DEFINE(msgq_CANSysMgmtRx, sizeof(struct can_frame),
              CAN_SYSMGMT_MSG_QUEUE_MAX_MESSAGES, 4);
K_MSGQ_DEFINE(msgq_CANBootloaderRx, sizeof(struct can_frame),
              BOOTLOADER_MSG_QUEUE_MAX_MESSAGEs, 4);

K_THREAD_STACK_DEFINE(thread_CANBootRxThread, BOOTLOADER_THREAD_STACK_SIZE_BYTEs);
static struct k_thread stCANBootRxThread_t;
static sT_CANConfig_t stTCANConfig = {0};
static struct k_spinlock spinlock_CANFailureCallback;

static void vCAN_BusError_Callback(eT_CAN_BUSState eBusState,
                                   struct can_bus_err_cnt stBusErrCount);
static void vCAN_BusRecovery_Failed_Callback(eT_CANBus_Recovery_State_t eRecoveryState);
static void vCAN_TxCompletion_Callback(sT_CAN_TxCompletion_t stCompletion);
static void vCAN_HostBootloaderResponse_Callback(const struct device *dev,
                                                  struct can_frame *pstFrame,
                                                  void *pUserData);
static void vThread_BootloaderMsgHandler(void *p1, void *p2, void *p3);
static void vHandle_Bootloader_Messages(struct can_frame *pstTCANMsg);
static void vFormat_Bootloader_Msg(sT_Bootloader_CtrlMsg_t *pstBtlMsg,
                                   struct can_frame *pstCANMsg);
static void vUpdate_BtlMsgACK_Requirement(sT_Bootloader_CtrlMsg_t *pstBtlMsg);
static void vCAN_SendBtl_ACK_NACK(const sT_Bootloader_CtrlMsg_t *pstBtlMsg);
static void vProcess_Bootloader_Messages(sT_Bootloader_CtrlMsg_t *pstBtlMsg);
static bool bGet_BootloaderExpectedPayloadLen(eT_Bootloader_Command eCMD,
                                               uint8_t *puiExpectedLen);

void vSend_CANMessage(sT_CAN_TXMsg_t *pstTMsg, sT_CmdTransaction_t *pstTransaction)
{
    if(pstTMsg == NULL)
    {
        FHALT("Null CAN TX message pointer");
        return;
    }

    memset(&pstTMsg->stTUserData, 0, sizeof(pstTMsg->stTUserData));
    if(atomic_load_explicit(&stTCANConfig.IsCANStopped, memory_order_acquire))
    {
        pstTMsg->eTxResult = eCAN_TxSubmit_BusUnavailable;
        FHALT("CAN Bus is in permanent fault. No Messages can be sent");
        return;
    }

    if(pstTransaction != NULL)
    {
        uint8_t uiTransactionIndex = 0U;
        if(!bGet_TransactionIndex(pstTransaction, &uiTransactionIndex))
        {
            pstTMsg->eTxResult = eCAN_TxSubmit_InvalidMsg;
            FHALT("Invalid transaction pointer for CAN Tx context");
            return;
        }

        k_mutex_lock(&pstTransaction->kMutex_TransactionClaim, K_FOREVER);
        sT_CAN_TxContext_t stTContext = {
            .uiGeneration = pstTransaction->uiGeneration,
            .eCommand = pstTransaction->eCommand,
            .uiTransactionIndex = uiTransactionIndex,
            .uiRevision = pstTransaction->uiRevision
        };
        k_mutex_unlock(&pstTransaction->kMutex_TransactionClaim);
        memcpy(pstTMsg->stTUserData.uiaCANUserData, &stTContext, sizeof(stTContext));
    }

    pstTMsg->eTxResult = eFlexCAN_SendMsg(pstTMsg);
}

bool bInit_CANController(void)
{
    stTCANConfig.eBusRecoveryType = eCAN_BUSRECTYPE_ManualRecover;
    stTCANConfig.busErrCallback = &vCAN_BusError_Callback;
    stTCANConfig.busFaultLatchCallback = &vCAN_BusRecovery_Failed_Callback;
    stTCANConfig.txFrameCompleteCallback = &vCAN_TxCompletion_Callback;

    stTCANConfig.staRxFiltConfigs[0].eID = eCAN_RxFilter_ID_SysMgt;
    stTCANConfig.staRxFiltConfigs[0].eRxType = eCAN_RxType_MsgQueue;
    stTCANConfig.staRxFiltConfigs[0].eFilterIdType = eCAN_RxFilterType_Standard;
    stTCANConfig.staRxFiltConfigs[0].pstMsgQueue = &msgq_CANSysMgmtRx;
    stTCANConfig.staRxFiltConfigs[0].pUserData = NULL;

    stTCANConfig.staRxFiltConfigs[1].eID = eCAN_RxFilter_ID_Bootloader;
    stTCANConfig.staRxFiltConfigs[1].eRxType = eCAN_RxType_MsgQueue;
    stTCANConfig.staRxFiltConfigs[1].eFilterIdType = eCAN_RxFilterType_Standard;
    stTCANConfig.staRxFiltConfigs[1].pstMsgQueue = &msgq_CANBootloaderRx;
    stTCANConfig.staRxFiltConfigs[1].pUserData = NULL;

    stTCANConfig.staRxFiltConfigs[2].eID = eCAN_RxFilter_ID_HostBootloader;
    stTCANConfig.staRxFiltConfigs[2].eRxType = eCAN_RxType_Callback;
    stTCANConfig.staRxFiltConfigs[2].eFilterIdType = eCAN_RxFilterType_Standard;
    stTCANConfig.staRxFiltConfigs[2].rxCallback = &vCAN_HostBootloaderResponse_Callback;
    stTCANConfig.staRxFiltConfigs[2].pUserData = NULL;

    if(iFlexCAN_Init(&stTCANConfig, eNUMBER_OF_CAN_RX_CONFIGs) < 0)
        return false;

    k_thread_create(&stCANBootRxThread_t, thread_CANBootRxThread,
                    K_THREAD_STACK_SIZEOF(thread_CANBootRxThread),
                    vThread_BootloaderMsgHandler,
                    NULL, NULL, NULL, BOOTLOADER_THREAD_PRIORITY,
                    0, K_NO_WAIT);
    return true;
}

int iGet_CANSysMgmt_Message(struct can_frame *pstTCANMsg, k_timeout_t timeout)
{
    return k_msgq_get(&msgq_CANSysMgmtRx, pstTCANMsg, timeout);
}

void vSend_ACK_NACK(uint32_t uiDestID, eT_CANACK_Ctrl eACKNACK,
                    eT_SysMgmtErrorCode_t eErrorCode, uint8_t uiCommand, uint8_t uiRev)
{
    uint8_t uiaData[6] = {
        eSysMgmt_CMD_ACK_NACK, 4U, uiCommand, uiRev,
        (uint8_t)eACKNACK, (uint8_t)eErrorCode
    };
    sT_CAN_TXMsg_t stTMsg = {0};
    stTMsg.uiID = uiDestID;
    stTMsg.puiData = uiaData;
    stTMsg.uiLen = sizeof(uiaData);
    vSend_CANMessage(&stTMsg, NULL);
    if(stTMsg.eTxResult != eCAN_TxSubmit_Queued)
    {
        FHALT("CAN Transfer Error : %d", stTMsg.eTxResult);
    }
}

static void vCAN_HostBootloaderResponse_Callback(const struct device *dev,
                                                  struct can_frame *pstFrame,
                                                  void *pUserData)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(pUserData);
    vPC_UART_API_ForwardCANFrame(pstFrame);
}

static void vThread_BootloaderMsgHandler(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    struct can_frame stTCANMsg;
    while(1)
    {
        if(k_msgq_get(&msgq_CANBootloaderRx, &stTCANMsg, K_FOREVER) != 0)
            continue;

        if(stTCANMsg.id == (uint32_t)iGetCAN_RxFilterID(eCAN_RxFilter_ID_Bootloader))
            vHandle_Bootloader_Messages(&stTCANMsg);
    }
}

static void vHandle_Bootloader_Messages(struct can_frame *pstTCANMsg)
{
    sT_Bootloader_CtrlMsg_t stBTLCtrlMsg = {0};
    vFormat_Bootloader_Msg(&stBTLCtrlMsg, pstTCANMsg);
    if(!stBTLCtrlMsg.bIsMsgOk)
    {
        vCAN_SendBtl_ACK_NACK(&stBTLCtrlMsg);
        return;
    }
    vProcess_Bootloader_Messages(&stBTLCtrlMsg);
}

static void vProcess_Bootloader_Messages(sT_Bootloader_CtrlMsg_t *pstBtlMsg)
{
    switch(pstBtlMsg->eCMD)
    {
        case eBootloader_CMD_FWUpReq:
        case eBootloader_CMD_FWUpMsg:
        case eBootloader_CMD_GetLostPacketInfo:
        case eBootloader_CMD_FWUpEnd:
        case eBootloader_CMD_FWUpPause:
        case eBootloader_CMD_FWUpStop:
        case eBootloader_CMD_AuthStart:
        case eBootloader_CMD_UpdateAuthResponse:
        case eBootloader_CMD_RecoverFromError:
            vUpdateBootloader(pstBtlMsg);
            break;
        case eBootloader_CMD_RetLostPacketInfo:
            vHandle_HostAcknowledgements(pstBtlMsg);
            break;
        default:
            FHALT("Invalid Bootloader Command");
            vSet_BootloaderMsgStatus(pstBtlMsg, false,
                                     eBootloader_Error_InvalidCommand);
            break;
    }

    if(pstBtlMsg->bIsACKReq)
        vCAN_SendBtl_ACK_NACK(pstBtlMsg);
}

static void vFormat_Bootloader_Msg(sT_Bootloader_CtrlMsg_t *pstBtlMsg,
                                   struct can_frame *pstCANMsg)
{
    if(pstBtlMsg == NULL || pstCANMsg == NULL)
    {
        FHALT("Null bootloader message pointer");
        return;
    }
    if(!bIsBootloader_Initialized())
    {
#if !defined(APP_STANDALONE)
        FHALT("%s -> Bootloader not initialized", __func__);
#endif
        vSet_BootloaderMsgStatus(pstBtlMsg, false,
                                 eBootloader_Error_NotInitialized);
        return;
    }

    uint8_t uiLen = can_dlc_to_bytes(pstCANMsg->dlc);
    if(uiLen < 1U)
    {
        FHALT("%s -> Invalid Payload Length", __func__);
        vSet_BootloaderMsgStatus(pstBtlMsg, false,
                                 eBootloader_Error_InvalidPayloadLength);
        return;
    }

    eT_Bootloader_Command eCMD = (eT_Bootloader_Command)pstCANMsg->data[0];
    pstBtlMsg->eCMD = eCMD;
    if(eCMD < eBootloader_CMD_FWUpReq || eCMD >= eNUMBER_OF_BOOTLOADER_COMMANDs)
    {
        FHALT("%s -> Invalid Bootloader Command", __func__);
        vSet_BootloaderMsgStatus(pstBtlMsg, false,
                                 eBootloader_Error_InvalidCommand);
        return;
    }

    uint8_t uiExpectedPayloadLen = 0U;
    if(!bGet_BootloaderExpectedPayloadLen(eCMD, &uiExpectedPayloadLen))
    {
        FHALT("%s -> Invalid Bootloader Command", __func__);
        vSet_BootloaderMsgStatus(pstBtlMsg, false,
                                 eBootloader_Error_InvalidCommand);
        return;
    }
    if((uiLen - 1U) < uiExpectedPayloadLen)
    {
        FHALT("%s -> Invalid Payload Length", __func__);
        vSet_BootloaderMsgStatus(pstBtlMsg, false,
                                 eBootloader_Error_InvalidPayloadLength);
        return;
    }

    pstBtlMsg->uiLen = uiExpectedPayloadLen;
    if(uiExpectedPayloadLen > 0U)
        memcpy(pstBtlMsg->uiaData, &pstCANMsg->data[1], uiExpectedPayloadLen);
    vUpdate_BtlMsgACK_Requirement(pstBtlMsg);
}

static void vUpdate_BtlMsgACK_Requirement(sT_Bootloader_CtrlMsg_t *pstBtlMsg)
{
    switch(pstBtlMsg->eCMD)
    {
        case eBootloader_CMD_FWUpReq:
        case eBootloader_CMD_FWUpEnd:
        case eBootloader_CMD_FWUpPause:
        case eBootloader_CMD_FWUpStop:
        case eBootloader_CMD_RecoverFromError:
            pstBtlMsg->bIsACKReq = true;
            vSet_BootloaderMsgStatus(pstBtlMsg, true, eBootloader_Error_None);
            break;
        case eBootloader_CMD_FWUpMsg:
        case eBootloader_CMD_GetLostPacketInfo:
        case eBootloader_CMD_RetLostPacketInfo:
            pstBtlMsg->bIsACKReq = false;
            vSet_BootloaderMsgStatus(pstBtlMsg, true, eBootloader_Error_None);
            break;
        default:
            FHALT("Invalid Bootloader Command");
            vSet_BootloaderMsgStatus(pstBtlMsg, false,
                                     eBootloader_Error_InvalidCommand);
            break;
    }
}

static void vCAN_SendBtl_ACK_NACK(const sT_Bootloader_CtrlMsg_t *pstBtlMsg)
{
    if(pstBtlMsg == NULL)
    {
        FHALT("Null pointer reference");
        return;
    }

    uint8_t uiaData[3] = {
        (uint8_t)pstBtlMsg->eCMD,
        (uint8_t)(pstBtlMsg->bIsMsgOk ? eCAN_ACK : eCAN_NACK),
        (uint8_t)pstBtlMsg->eErrorCode
    };
    sT_CAN_TXMsg_t stMsg = {
        .uiID = CAN_HOST_NODE_BOOTLOADER_ID,
        .uiLen = sizeof(uiaData),
        .puiData = uiaData
    };
    vSend_CANMessage(&stMsg, NULL);
    if(stMsg.eTxResult != eCAN_TxSubmit_Queued)
        FHALT("Bootloader response CAN submission failed: %d", stMsg.eTxResult);
}

static bool bGet_BootloaderExpectedPayloadLen(eT_Bootloader_Command eCMD,
                                               uint8_t *puiExpectedLen)
{
    if(puiExpectedLen == NULL)
        return false;

    switch(eCMD)
    {
        case eBootloader_CMD_FWUpReq:
            *puiExpectedLen = 10U;
            return true;
        case eBootloader_CMD_FWUpMsg:
            *puiExpectedLen = FW_IMG_PACKET_DATA_LENGTH;
            return true;
        case eBootloader_CMD_FWUpEnd:
        case eBootloader_CMD_FWUpPause:
        case eBootloader_CMD_FWUpStop:
        case eBootloader_CMD_GetLostPacketInfo:
        case eBootloader_CMD_RecoverFromError:
            *puiExpectedLen = 0U;
            return true;
        case eBootloader_CMD_RetLostPacketInfo:
            *puiExpectedLen = 2U;
            return true;
        default:
            return false;
    }
}

static void vCAN_BusError_Callback(eT_CAN_BUSState eBusState,
                                   struct can_bus_err_cnt stBusErrCount)
{
    FWARN("CAN Bus State: %d, TxErr: %u, RxErr: %u",
          eBusState, stBusErrCount.tx_err_cnt, stBusErrCount.rx_err_cnt);
}

static void vCAN_BusRecovery_Failed_Callback(eT_CANBus_Recovery_State_t eRecoveryState)
{
    can_bus_criticalfailure_subscribeCallback_t pvNotifyCallbackFn;
    k_spinlock_key_t key = k_spin_lock(&spinlock_CANFailureCallback);
    atomic_store_explicit(&stTCANConfig.IsCANStopped, true, memory_order_release);
    pvNotifyCallbackFn = stTCANConfig.busCriticalFailureAppCallback;
    k_spin_unlock(&spinlock_CANFailureCallback, key);

    if(pvNotifyCallbackFn != NULL)
        pvNotifyCallbackFn(eCAN_BUS_Permanent_Fault);
    FHALT("CAN Bus FaultLatch: %d", eRecoveryState);
}

static void vCAN_TxCompletion_Callback(sT_CAN_TxCompletion_t stCompletion)
{
    vNotify_MessageTxComplete(stCompletion);
    if(stCompletion.eResult != eCAN_TxComplete_Success)
        FWARN("CAN TX completion failed: %d, driver error: %d",
              stCompletion.eResult, stCompletion.iDriverError);
}

void vSubscribe_ToCANBusCriticalFailure(
    can_bus_criticalfailure_subscribeCallback_t pvNotifyCallbackFn)
{
    bool bReplayCriticalFailure;
    k_spinlock_key_t key = k_spin_lock(&spinlock_CANFailureCallback);
    stTCANConfig.busCriticalFailureAppCallback = pvNotifyCallbackFn;
    bReplayCriticalFailure =
        atomic_load_explicit(&stTCANConfig.IsCANStopped, memory_order_acquire) &&
        pvNotifyCallbackFn != NULL;
    k_spin_unlock(&spinlock_CANFailureCallback, key);

    if(bReplayCriticalFailure)
        pvNotifyCallbackFn(eCAN_BUS_Permanent_Fault);
}

void vUnSubscribe_FromCANBusCriticalFailure(void)
{
    k_spinlock_key_t key = k_spin_lock(&spinlock_CANFailureCallback);
    stTCANConfig.busCriticalFailureAppCallback = NULL;
    k_spin_unlock(&spinlock_CANFailureCallback, key);
}

#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/sys/reboot.h>

#include "CAN/NXP_CAN_API.h"
#include "Bootloader_Ctrl.h"

#if defined(DEBUG_BOOTLOADER)
    #define Bootloader_Print                    printk
#else
    #define Bootloader_Print(...)
#endif

sT_Bootloader_Mgmt_t stTBtlMgmt_t = {0};
const struct flash_area *flashArea;
static struct k_work_delayable stBtlStateTimeOut_Work_t;
static struct k_work_delayable stBtlTxTimeOut_Work_t;

static struct k_mutex kMutex_BootloaderSession;
static struct k_mutex kMutex_BootloaderDispatch;
static int64_t iBootloaderStateDeadline_ms;
static bool bBootloaderStateTimeoutArmed;

#define IS_FWUpdate_InProgress()                (stTBtlMgmt_t.eBtlState == eBootloader_State_FWImgWrite_InProg)
#define sTFWImg                                 stTBtlMgmt_t.stTFWImgCtrl_t
#define sTTxLostPacketInfo                      sTFWImg.stTxLostPacketInfo

#define vSet_FlashWriteFlag()                   sTFWImg.bIsFlashWritInProgress = true
#define vClear_FlashWriteFlag()                 sTFWImg.bIsFlashWritInProgress = false
#define bIsFlashWriteInProgress()               sTFWImg.bIsFlashWritInProgress

#define vSet_LostPacketsDetected()              sTFWImg.bLostPacketDetected = true
#define vClear_LostPacketsDetected()            sTFWImg.bLostPacketDetected = false
#define bIsLostPacketsDetected()                sTFWImg.bLostPacketDetected

#define vSet_LostPacketsState()                 sTFWImg.bLostPacketStateTriggered = true
#define vClear_LostPacketsState()               sTFWImg.bLostPacketStateTriggered = false
#define bIsLostPacketsStateTriggered()          sTFWImg.bLostPacketStateTriggered

#define vSet_FirstLostPacketId(uiPacketId)      sTFWImg.uiFirstLostPacketId = uiPacketId

static void vInit_Bootloader_SecurityLayer( void );
static inline void vSet_BootloaderAuth_Flag( void );
static inline void vClear_BootloaderAuth_Flag( void );
static inline bool bIsBootloader_Authenticated( void );

static void vInit_TxLostPacketInfo( void );
static void vReinit_TxLostPacketInfo( void );
static void vSet_BootloaderState( eT_Bootloader_State eState );
static eT_Bootloader_State eGet_BootloaderState( void );
static __attribute__((unused))void vErase_FlashArea( sT_Bootloader_CtrlMsg_t * pstTBootMsg );
static bool bIsFirstEntry( void );

static void vExecute_Bootloader_IdleState( sT_Bootloader_CtrlMsg_t * pstTBootMsg );
static void vExecute_Bootloader_FWImgWrite_State( sT_Bootloader_CtrlMsg_t * pstTBootMsg );
static void vExecute_Bootloader_FWImgWrite_Completed( sT_Bootloader_CtrlMsg_t * pstTBootMsg );
static void vExecute_Bootloader_Reboot( sT_Bootloader_CtrlMsg_t * pstTBootMsg );
static void vExecute_Bootloader_ErrorState( sT_Bootloader_CtrlMsg_t * pstTBootMsg );

static void vAbort_BootloaderSession( void );
static void vAbort_BootloaderSession_Locked( void );
static void vUpdate_FWImage( sT_Bootloader_CtrlMsg_t * pstTBootMsg );
static void vFinalize_FWImageWrite( sT_Bootloader_CtrlMsg_t * pstTBootMsg );
static void vStop_FWUpgradeProcess( sT_Bootloader_CtrlMsg_t * pstTBootMsg );
static void vInit_BootloaderManager_ForNewFWImage( sT_Bootloader_CtrlMsg_t * pstTBootMsg );
static void vRequest_MCUbootImageUpgrade( sT_Bootloader_CtrlMsg_t *pstTBootMsg );
static void vReinit_BootloaderStateMachine( void );

//Missing Packet Handling
static void vHandle_PacketId_Mismatch( sT_Bootloader_CtrlMsg_t * pstTBootMsg, uint16_t uipacketId, bool *bContinue );
static bool bCanHandleMissingPackets( sT_Bootloader_CtrlMsg_t * pstTBootMsg, uint16_t uipacketId );
static void vHandle_LostPacketRecovery( sT_Bootloader_CtrlMsg_t * pstTBootMsg, uint16_t uiPacketId );
static void vValidate_FWImgCRC_FromFLash_AtLostPacketState( sT_Bootloader_CtrlMsg_t * pstTBootMsg, bool *bShouldMarkCompleted, eT_Bootloader_ErrorCode *eErrorCode );
static uint8_t uiGet_PendingLostPacketCount( void );
static void vGet_LostPacketInfo( sT_Bootloader_CtrlMsg_t * pstTBootMsg );
static void vEnter_LostPacketRecovery_Locked( void );

static void vBtlState_TimeOutHandler( struct k_work *work );
static void vRefresh_BootloaderStateTimeOut(uint32_t uiTimeOut_ms);
static void vRefresh_BootloaderStateTimeOut_Locked(uint32_t uiTimeOut_ms);
static void vInvalidate_BootloaderStateTimeOut_Locked( void );
static void vCancel_BootloaderStateTimeOut( void );
static bool bSchedule_LostPacketTimeouts( void );
void vBtlTxTimeOutHandler( struct k_work *work );

//Bootloader Tx Error handling
void vProcess_Bootloader_TxError( void );
void vProcess_Bootloader_RetryTx( void );
void vHandle_LostPacketInfo_TxResponse( eT_CANACK_Ctrl eAck, uint8_t uiFrameSeq );
void vSet_TxState( eT_Bootloader_TxState eState );
eT_Bootloader_TxState eGet_TxState( void );
bool bIs_LostPacketTx_Idle( void );
void vHandle_ACK_ForLostPacketInfo( sT_Bootloader_CtrlMsg_t * pstTBootMsg );

static uint16_t u16CRC16_CCITT_Update(uint16_t crc, const uint8_t *data, uint32_t len);

void vUpdateBootloader( sT_Bootloader_CtrlMsg_t * pstTBootMsg)
{
    if(pstTBootMsg == NULL)
    {
        FHALT("Null pointer reference");
        return;
    }

    /*
     * CAN commands and asynchronous controller notifications can arrive from
     * different threads.  Only one of them may execute the Bootloader state
     * machine at a time.
     */
    k_mutex_lock(&kMutex_BootloaderDispatch, K_FOREVER);

    switch(eGet_BootloaderState())
    {
        case eBootloader_State_Inactive:
            FHALT("Bootloader Not Initialized");
            break;
        case eBootloader_State_Idle:
            vExecute_Bootloader_IdleState(pstTBootMsg);
            break;
        case eBootloader_State_FWImgWrite_InProg:
            vExecute_Bootloader_FWImgWrite_State(pstTBootMsg);
            break;
        case eBootloader_State_FWImgWrite_Completed:
            vExecute_Bootloader_FWImgWrite_Completed(pstTBootMsg);
            break;
        case eBootloader_State_Reboot:
            vExecute_Bootloader_Reboot(pstTBootMsg);
            break;
        case eBootloader_State_Error:
            vExecute_Bootloader_ErrorState(pstTBootMsg);
            break;
        default:
            FHALT("Invalid Bootloader State");
            break;   
    }

    k_mutex_unlock(&kMutex_BootloaderDispatch);
}

static void vExecute_Bootloader_ErrorState( sT_Bootloader_CtrlMsg_t * pstTBootMsg )
{
    if(pstTBootMsg == NULL)
    {
        FHALT("Null Pointer Reference");
        return;
    }

    if(bIsFirstEntry())
    {
        FHALT("Bootloader is in Error State");
    }

    eT_Bootloader_Command eCMD = pstTBootMsg->eCMD;

    switch (eCMD)
    {
        case eBootloader_CMD_RecoverFromError:
            vReinit_BootloaderStateMachine();
            vSet_BootloaderMsgStatus(pstTBootMsg, true, eBootloader_Error_None);
            break;
        default:
            vSet_BootloaderMsgStatus(pstTBootMsg, false, eBootloader_Error_InvalidCommand);
            FHALT("Unsupported Command (CMD : %d) at Bootloader Error State", eCMD);
            break;
    }
}

static void vExecute_Bootloader_Reboot( sT_Bootloader_CtrlMsg_t * pstTBootMsg )
{
    vCancel_BootloaderStateTimeOut();
    //It is better to indicate to the host via a dedicated HW pin here
    //to be done
    Bootloader_Print("System rebooting for new FW update...\n\r");
    sys_reboot(SYS_REBOOT_COLD);
}

static void vExecute_Bootloader_FWImgWrite_Completed( sT_Bootloader_CtrlMsg_t * pstTBootMsg )
{
    if(pstTBootMsg == NULL)
    {
        FHALT("Null pointer reference");
        return;        
    }
    
    if(bIsFirstEntry())
    {
        vCancel_BootloaderStateTimeOut();
        if(flashArea != NULL)
        {
            flash_area_close(flashArea);
            flashArea = NULL;
        }
        Bootloader_Print("Firmware image complete; requesting MCUboot upgrade\n\r");
    }

    switch (pstTBootMsg->eCMD)
    {
        case eBootloader_CMD_FWUpEnd:
            vRequest_MCUbootImageUpgrade(pstTBootMsg);
            break;
        default:
            FHALT("Invalid Bootloader Command : %d", pstTBootMsg->eCMD);
            break;
    }


}

static void vRequest_MCUbootImageUpgrade( sT_Bootloader_CtrlMsg_t *pstTBootMsg )
{
    if(pstTBootMsg == NULL)
    {
        FHALT("Null pointer reference");
        return;
    }

    uint8_t uiRetryCount = 0U;
    while(uiRetryCount < BOOTLOADER_MAX_REQUEST_UPGRADE_TURNs)
    {
        int ret = boot_request_upgrade(BOOT_UPGRADE_TEST);
        if(ret == 0)
        {
            break;
        }

        FHALT("FW Image Upgrade Request Fail. Retrying : %d", uiRetryCount);
        uiRetryCount++;
        k_msleep(10);
    }

    if(uiRetryCount >= BOOTLOADER_MAX_REQUEST_UPGRADE_TURNs)
    {
        vSet_BootloaderMsgStatus(pstTBootMsg, false,
                                 eBootloader_Error_UpgradeRequestFail);
        vAbort_BootloaderSession();
        return;
    }

    vSet_BootloaderMsgStatus(pstTBootMsg, true, eBootloader_Error_None);
    vSet_BootloaderState(eBootloader_State_Reboot);
    vRefresh_BootloaderStateTimeOut(TIMEOUT_REBOOT_CONFIRMATION_ms);
}

static void vExecute_Bootloader_FWImgWrite_State( sT_Bootloader_CtrlMsg_t * pstTBootMsg )
{
    if(!bIsBootloader_Initialized())
    {
        FHALT("Bootloader not initialized");
        return;
    }
    if(pstTBootMsg == NULL)
    {
        FHALT("Null pointer reference");
        return;
    }

    switch(pstTBootMsg->eCMD)
    {
        case eBootloader_CMD_FWUpStop:
            vStop_FWUpgradeProcess(pstTBootMsg);
            break;
        case eBootloader_CMD_FWUpEnd:
            vFinalize_FWImageWrite(pstTBootMsg);
            break;
            break;
        case eBootloader_CMD_FWUpMsg:
            vUpdate_FWImage(pstTBootMsg);
            break;
        case eBootloader_CMD_GetLostPacketInfo:
            vGet_LostPacketInfo(pstTBootMsg);
            break;
        case eBootloader_CMD_FWUpPause:
        default:
            vSet_BootloaderMsgStatus(pstTBootMsg, false, eBootloader_Error_InvalidCommand);
            FHALT("Invalid ID at FW Image Write State");
            break;
    }

}

void vHandle_HostAcknowledgements( sT_Bootloader_CtrlMsg_t * pstTBootMsg )
{
    if(pstTBootMsg == NULL)
    {
        FHALT("Null pointer reference");
        return;
    }

    switch (pstTBootMsg->eCMD)
    {
        case eBootloader_CMD_RetLostPacketInfo:
            vHandle_ACK_ForLostPacketInfo(pstTBootMsg);
            break;        
        default:
            vSet_BootloaderMsgStatus(pstTBootMsg, false, eBootloader_Error_InvalidCommand);
            break;
    }
}

void vHandle_ACK_ForLostPacketInfo( sT_Bootloader_CtrlMsg_t * pstTBootMsg )
{
    if(pstTBootMsg == NULL)
    {
        FHALT("Null pointer reference");
        return;
    }
    if(pstTBootMsg->eCMD != eBootloader_CMD_RetLostPacketInfo)
    {
        FHALT("Invalid Command for handling ACK of Lost Packet Info Message");
        vSet_BootloaderMsgStatus(pstTBootMsg, false, eBootloader_Error_InvalidCommand);
        return;
    }
    
    pstTBootMsg->bIsACKReq = false; //This message is only sent by the bootloader and should not require ACK. Setting this to false to avoid any unintended ACK response from the host
    vHandle_LostPacketInfo_TxResponse(pstTBootMsg->uiaData[0], pstTBootMsg->uiaData[1]);
}

void vHandle_LostPacketInfo_TxResponse( eT_CANACK_Ctrl eAck, uint8_t uiFrameSeq )
{
    if(eAck < eCAN_ACK || eAck >= eNUMBER_OF_CAN_ACKTYPEs)
    {
        FHALT("Invalid ACK Type : %d", eAck);
        return;
    }

    k_mutex_lock(&sTTxLostPacketInfo.mutex_LockLostPacket, K_FOREVER);
    if(sTTxLostPacketInfo.stTCANTxMsg_t.puiData == NULL ||
       sTTxLostPacketInfo.stTCANTxMsg_t.uiLen < 3U)
    {
        k_mutex_unlock(&sTTxLostPacketInfo.mutex_LockLostPacket);
        FHALT("Invalid Data in CAN tx Message");
        return;        
    }
    
    if(eGet_TxState() == eBootloader_Tx_Idle)
    {
        k_mutex_unlock(&sTTxLostPacketInfo.mutex_LockLostPacket);
        FHALT("Received ACK/NACK for Lost Packet Info Message while Tx State is Idle");
        return;
    }

    uint8_t uiExpectedFrameSeq = sTTxLostPacketInfo.stTCANTxMsg_t.puiData[2];
    if(uiFrameSeq != uiExpectedFrameSeq)
    {
        k_mutex_unlock(&sTTxLostPacketInfo.mutex_LockLostPacket);        
        FHALT("Invalid ACK Frame Seq. Expected: %d, Received: %d", uiExpectedFrameSeq, uiFrameSeq);
        return;
    }
    
    if(eAck == eCAN_ACK)
    {
        k_work_cancel_delayable(&stBtlTxTimeOut_Work_t);
        sTTxLostPacketInfo.uiRetryCount = 0;
        vSet_TxState(eBootloader_Tx_Idle);        
        k_mutex_unlock(&sTTxLostPacketInfo.mutex_LockLostPacket);

        Bootloader_Print("ACK received for Lost Packet Info Message Tx\n\r");
    }
    else
    {
        vSet_TxState(eBootloader_Tx_RetryInProgress);
        k_mutex_unlock(&sTTxLostPacketInfo.mutex_LockLostPacket);

        vProcess_Bootloader_RetryTx();
        Bootloader_Print("NACK received for Lost Packet Info Message Tx\n\r");
    }
}

static void vGet_LostPacketInfo( sT_Bootloader_CtrlMsg_t * pstTBootMsg )
{
    sT_CAN_TXMsg_t stTLostPacketInfoMsg = {0};
    uint8_t uiaLostPacketInfoData[CAN_MSG_MAX_SIZE] = {0};
    uint8_t uiCount = 0, uiIndex = 0;

    if(pstTBootMsg == NULL)
    {
        FHALT("Null pointer reference");
        return;
    }
    if(pstTBootMsg->eCMD != eBootloader_CMD_GetLostPacketInfo)
    {
        FHALT("Invalid Command for Get Lost Packet Info");
        vSet_BootloaderMsgStatus(pstTBootMsg, false, eBootloader_Error_InvalidCommand);
        return;
    }

    k_mutex_lock(&sTTxLostPacketInfo.mutex_LockLostPacket, K_FOREVER);
    if(eGet_TxState() != eBootloader_Tx_Idle)
    {
        k_mutex_unlock(&sTTxLostPacketInfo.mutex_LockLostPacket);
        vSet_BootloaderMsgStatus(pstTBootMsg, false, eBootloader_Error_UpgradeRequestFail);
        FHALT("Previous Lost Packet Info Tx is still in progress. Cannot process new request");
        return;
    }

    vSet_TxState(eBootloader_Tx_InProgress);
    uint8_t pendingLostPacketCount = uiGet_PendingLostPacketCount();

    stTLostPacketInfoMsg.uiID = FW_IMAGE_HOST_DEVICE_ID;
    stTLostPacketInfoMsg.puiData = uiaLostPacketInfoData;
    stTLostPacketInfoMsg.puiData[0] = eBootloader_CMD_RetLostPacketInfo;
    stTLostPacketInfoMsg.puiData[1] = pendingLostPacketCount;
    uiIndex = 4;

    if(pendingLostPacketCount != 0)
    {
        while(uiCount < sTFWImg.uiLostPacketCount && (uiIndex + 1) < CAN_MSG_MAX_SIZE)
        {
            if(sTFWImg.staLostPacketIDs[uiCount].bIsHandled)
            {
                uiCount++;
                continue;
            }
            stTLostPacketInfoMsg.puiData[uiIndex] = sTFWImg.staLostPacketIDs[uiCount].uiPacketId >> 8;
            stTLostPacketInfoMsg.puiData[uiIndex + 1] = sTFWImg.staLostPacketIDs[uiCount].uiPacketId & 0xFF;
            uiIndex += 2;
            uiCount++;            
        }
    }

    stTLostPacketInfoMsg.puiData[2] = sTTxLostPacketInfo.uiFrameSeq;
    stTLostPacketInfoMsg.puiData[3] = uiIndex - 4; //Number of bytes of packet ID info in the message
    stTLostPacketInfoMsg.uiLen = uiIndex;

    vSend_CANMessage(&stTLostPacketInfoMsg, NULL);

    if(stTLostPacketInfoMsg.eTxResult != eCAN_TxSubmit_Queued)
    {
        FHALT("Failed to send Lost Packet Info Message");

        vSet_TxState(eBootloader_Tx_RetryInProgress);
        memcpy(sTTxLostPacketInfo.uiaTxData, stTLostPacketInfoMsg.puiData, stTLostPacketInfoMsg.uiLen);
        memcpy(&sTTxLostPacketInfo.stTCANTxMsg_t, &stTLostPacketInfoMsg, sizeof(sT_CAN_TXMsg_t));
        sTTxLostPacketInfo.stTCANTxMsg_t.puiData = sTTxLostPacketInfo.uiaTxData;
        sTTxLostPacketInfo.uiRetryCount++;
        k_mutex_unlock(&sTTxLostPacketInfo.mutex_LockLostPacket);

        (void)bSchedule_LostPacketTimeouts();
        return;
    }

    sTTxLostPacketInfo.uiFrameSeq++;
    memcpy(sTTxLostPacketInfo.uiaTxData, stTLostPacketInfoMsg.puiData, stTLostPacketInfoMsg.uiLen);
    memcpy(&sTTxLostPacketInfo.stTCANTxMsg_t, &stTLostPacketInfoMsg, sizeof(sT_CAN_TXMsg_t));
    sTTxLostPacketInfo.stTCANTxMsg_t.puiData = sTTxLostPacketInfo.uiaTxData;
    k_mutex_unlock(&sTTxLostPacketInfo.mutex_LockLostPacket);

    (void)bSchedule_LostPacketTimeouts();
}

void vBtlTxTimeOutHandler( struct k_work *work )
{
    k_mutex_lock(&sTTxLostPacketInfo.mutex_LockLostPacket, K_FOREVER);
    if(eGet_TxState() != eBootloader_Tx_RetryInProgress && eGet_TxState() != eBootloader_Tx_InProgress)
    {
        k_mutex_unlock(&sTTxLostPacketInfo.mutex_LockLostPacket);
        k_work_cancel_delayable(&stBtlTxTimeOut_Work_t);
        FHALT("Invalid Tx State for Lost Packet Info Message Retry");
        return;
    }

    if(sTTxLostPacketInfo.uiRetryCount >= BOOTLOADER_TX_Retry_MAX_COUNT)
    {
        k_mutex_unlock(&sTTxLostPacketInfo.mutex_LockLostPacket);
        FHALT("Failed to send Lost Packet Info Message after (%d) retries. Aborting Lost Packet Recovery Process",
              BOOTLOADER_TX_Retry_MAX_COUNT);
        vProcess_Bootloader_TxError();
        return;
    }

    vSet_TxState(eBootloader_Tx_RetryInProgress);
    k_mutex_unlock(&sTTxLostPacketInfo.mutex_LockLostPacket);
    vProcess_Bootloader_RetryTx();
}

void vProcess_Bootloader_TxError( void)
{
    eT_Bootloader_State eState = eGet_BootloaderState();

    switch (eState)
    {
        case eBootloader_State_FWImgWrite_InProg:
            k_work_cancel_delayable(&stBtlTxTimeOut_Work_t);
            vAbort_BootloaderSession();
            break;        
        default:
            FHALT("Unhandled Bootloader State for Tx Error. State: %d", eState);
            break;
    }
}

void vProcess_Bootloader_RetryTx( void )
{
    k_mutex_lock(&sTTxLostPacketInfo.mutex_LockLostPacket, K_FOREVER);

    if (sTTxLostPacketInfo.eTxState != eBootloader_Tx_RetryInProgress)
    {
        k_mutex_unlock(&sTTxLostPacketInfo.mutex_LockLostPacket);
        return;
    }
    if(sTTxLostPacketInfo.uiRetryCount >= BOOTLOADER_TX_Retry_MAX_COUNT)
    {
        k_mutex_unlock(&sTTxLostPacketInfo.mutex_LockLostPacket);
        FHALT("Exceeded maximum retry count for Lost Packet Info Message");
        vProcess_Bootloader_TxError();
        return;
    }
    
    sT_CAN_TXMsg_t stTMsg = {0};
    memcpy(&stTMsg, &sTTxLostPacketInfo.stTCANTxMsg_t, sizeof(sT_CAN_TXMsg_t));

    vSend_CANMessage(&stTMsg, NULL);

    if(stTMsg.eTxResult != eCAN_TxSubmit_Queued)
    {
        FHALT("Failed to send Lost Packet Info Message");
    }

    sTTxLostPacketInfo.uiRetryCount++;
    vSet_TxState(eBootloader_Tx_RetryInProgress);
    k_mutex_unlock(&sTTxLostPacketInfo.mutex_LockLostPacket);

    (void)bSchedule_LostPacketTimeouts();
}

void vSet_TxState( eT_Bootloader_TxState eState )
{
    sTTxLostPacketInfo.eTxState = eState;
}

eT_Bootloader_TxState eGet_TxState( void )
{
    return sTTxLostPacketInfo.eTxState;
}

bool bIs_LostPacketTx_Idle( void )
{
    return (sTTxLostPacketInfo.eTxState == eBootloader_Tx_Idle);
}

static uint8_t uiGet_PendingLostPacketCount( void )
{
    uint8_t count = 0;

    for(uint8_t i = 0; i < sTFWImg.uiLostPacketCount; i++)
    {
        if(!sTFWImg.staLostPacketIDs[i].bIsHandled)
            count++;
    }

    return count;
}

static void vStop_FWUpgradeProcess( sT_Bootloader_CtrlMsg_t * pstTBootMsg )
{
    if(pstTBootMsg == NULL)
    {
        return;
    }

    vSet_BootloaderMsgStatus(pstTBootMsg, true, eBootloader_Error_None);
    vAbort_BootloaderSession();
}

static void vEnter_LostPacketRecovery_Locked( void )
{
    k_mutex_lock(&sTTxLostPacketInfo.mutex_LockLostPacket, K_FOREVER);

    vSet_TxState(eBootloader_Tx_Idle);
    sTTxLostPacketInfo.uiFrameSeq = 0;
    sTTxLostPacketInfo.uiRetryCount = 0;
    memset(sTTxLostPacketInfo.uiaTxData, 0, sizeof(sTTxLostPacketInfo.uiaTxData));
    memset(&sTTxLostPacketInfo.stTCANTxMsg_t, 0, sizeof(sT_CAN_TXMsg_t));

    k_mutex_unlock(&sTTxLostPacketInfo.mutex_LockLostPacket);
    vRefresh_BootloaderStateTimeOut_Locked(TIMEOUT_LOST_PACKET_RECOVERY_PROCESS_ms);
}

static void vFinalize_FWImageWrite( sT_Bootloader_CtrlMsg_t * pstTBootMsg )
{
    bool bShouldMarkCompleted = true;
    bool bCancelWork = false;
    bool bProcessCompletedState = false;
    eT_Bootloader_ErrorCode eErrorCode = eBootloader_Error_None;

    if(pstTBootMsg == NULL)
    {
        FHALT("Null Pointer reference");
        return;
    }

    k_mutex_lock(&kMutex_BootloaderSession, K_FOREVER);

    if(stTBtlMgmt_t.eBtlState != eBootloader_State_FWImgWrite_InProg)
    {
        vSet_BootloaderMsgStatus(pstTBootMsg, false, eBootloader_Error_UpdateInProgress);
        goto exit;
    }

    uint32_t uiExpectedPacketCount = (stTBtlMgmt_t.uiFWImgSize + DATA_PAYLOAD_PER_ONE_IMAGE_FRAME - 1U) / DATA_PAYLOAD_PER_ONE_IMAGE_FRAME;

    if(uiExpectedPacketCount > UINT16_MAX)
    {
        vSet_BootloaderMsgStatus(pstTBootMsg, false, eBootloader_Error_ImageTooLarge);
        vAbort_BootloaderSession_Locked();
        bCancelWork = true;
        goto exit;
    }

    if((uint32_t)sTFWImg.uiNextId < uiExpectedPacketCount)
    {
        if(!bCanHandleMissingPackets(pstTBootMsg, uiExpectedPacketCount))
        {
            bCancelWork = true;
            goto exit;
        }
    }

    if(bIsLostPacketsStateTriggered())
    { 
        if(uiGet_PendingLostPacketCount() > 0)
        {
            FWARN("Cannot mark completed, because FW Lost Packets are pending...");
            vSet_BootloaderMsgStatus(pstTBootMsg, false, eBootloader_Error_MissingPacketsPending);
            vEnter_LostPacketRecovery_Locked();
            goto exit;
        }
        if(sTFWImg.uiReceivedByteCount != stTBtlMgmt_t.uiFWImgSize)
        {
            FHALT("Recovered image size mismatch. Expected: %u, Received: %u",
                   stTBtlMgmt_t.uiFWImgSize,
                   sTFWImg.uiReceivedByteCount);

            vSet_BootloaderMsgStatus(pstTBootMsg, false, eBootloader_Error_ImageIncomplete);
            vAbort_BootloaderSession_Locked();
            bCancelWork = true;
            goto exit;
        }      
    }
    else
    {
        if(sTFWImg.uiReceivedByteCount != stTBtlMgmt_t.uiFWImgSize)
        {
            FHALT("FW Image is still not completed to mark it completed!!!");
            vSet_BootloaderMsgStatus(pstTBootMsg, false, eBootloader_Error_ImageIncomplete);
            vAbort_BootloaderSession_Locked();
            bCancelWork = true;
            goto exit;
        }
        if(sTFWImg.uiRunningCRC != stTBtlMgmt_t.uiCRC)
        {
            FHALT("CRC Mismatch (Calculated: %d, Received: %d)", sTFWImg.uiRunningCRC, stTBtlMgmt_t.uiCRC);
            vSet_BootloaderMsgStatus(pstTBootMsg, false, eBootloader_Error_CrcMismatch);
            vAbort_BootloaderSession_Locked();
            bCancelWork = true;
            goto exit;
        }

        stTBtlMgmt_t.eBtlState = eBootloader_State_FWImgWrite_Completed;
        stTBtlMgmt_t.bIsFirstEntry = true;
        vInvalidate_BootloaderStateTimeOut_Locked();
        bProcessCompletedState = true;
        goto exit;
    }

    vValidate_FWImgCRC_FromFLash_AtLostPacketState(pstTBootMsg, &bShouldMarkCompleted, &eErrorCode);
    if (!bShouldMarkCompleted)
    {
        FHALT("FW Image validation is not successful at Lost Packet State");
        vSet_BootloaderMsgStatus(pstTBootMsg, false, eErrorCode);
        vAbort_BootloaderSession_Locked();
        bCancelWork = true;
        goto exit;
    }

    stTBtlMgmt_t.eBtlState = eBootloader_State_FWImgWrite_Completed;
    stTBtlMgmt_t.bIsFirstEntry = true;
    vInvalidate_BootloaderStateTimeOut_Locked();
    bProcessCompletedState = true;

exit:
    k_mutex_unlock(&kMutex_BootloaderSession);

    if(bCancelWork)
    {
        (void)k_work_cancel_delayable(&stBtlStateTimeOut_Work_t);
        (void)k_work_cancel_delayable(&stBtlTxTimeOut_Work_t);
        return;
    }

    if(bProcessCompletedState)
    {
        (void)k_work_cancel_delayable(&stBtlStateTimeOut_Work_t);
        vExecute_Bootloader_FWImgWrite_Completed(pstTBootMsg);
    }
}


static void vUpdate_FWImage( sT_Bootloader_CtrlMsg_t * pstTBootMsg )
{
    if(pstTBootMsg == NULL)
    {
        return;
    }

    if(pstTBootMsg->uiLen != FW_IMG_PACKET_DATA_LENGTH)
    {
        FHALT("Invalid Packet Length : %d", pstTBootMsg->uiLen);
        vSet_BootloaderMsgStatus(pstTBootMsg, false, eBootloader_Error_InvalidPayloadLength);
        return;
    }

    bool bIsFirstPacket = false, bShouldContinue = true;
    bool bCancelWork = false;
    int ret = 0;
    uint8_t uiIndex = 0;
    uint8_t uiaWriteBlock[FW_IMG_WRITE_BYTE_LENGTH];
    uint32_t uiOffset = 0, uiWriteLen = 0, uiAvailableFlashSize = 0, uiImageSize = 0;
    uint16_t uipacketId = (uint16_t)(pstTBootMsg->uiaData[0] << 8) | (uint16_t)pstTBootMsg->uiaData[1];
    uiIndex = 2;

    k_mutex_lock(&kMutex_BootloaderSession, K_FOREVER);

    if(stTBtlMgmt_t.eBtlState != eBootloader_State_FWImgWrite_InProg)
    {
        vSet_BootloaderMsgStatus(pstTBootMsg, false, eBootloader_Error_UpdateInProgress);
        goto exit;
    }

    if(bIsFlashWriteInProgress())
    {
        FHALT("Flash Update Session Active. Abort current update");
        vSet_BootloaderMsgStatus(pstTBootMsg, false, eBootloader_Error_UpdateInProgress);
        goto exit;
    }

    if(stTBtlMgmt_t.uiFlashAreaSize <= FW_IMAGE_SECONDARY_SLOT_WRITE_OFFSET)
    {
        FHALT("Secondary slot is smaller than MCUboot write offset");
        vSet_BootloaderMsgStatus(pstTBootMsg, false, eBootloader_Error_FlashBoundsExceeded);
        vAbort_BootloaderSession_Locked();
        bCancelWork = true;
        goto exit;
    }
    uiAvailableFlashSize = stTBtlMgmt_t.uiFlashAreaSize - FW_IMAGE_SECONDARY_SLOT_WRITE_OFFSET;

    if(sTFWImg.uiLastId == 0 && sTFWImg.uiNextId == 0)
    {
        /* The first received packet may be later than ID 0. Keep the flash
         * area open for it and let the gap handler record the missing IDs.
         */
        bIsFirstPacket = true;
    }

    if(sTFWImg.uiNextId != uipacketId)
    {
        vHandle_PacketId_Mismatch(pstTBootMsg, uipacketId, &bShouldContinue);
        if(!bShouldContinue)
        {
            bCancelWork = (stTBtlMgmt_t.eBtlState == eBootloader_State_Error);
            goto exit;
        }
    }
    
    vSet_FlashWriteFlag();
    sTFWImg.uiLastId = uipacketId;
    if(uipacketId >= sTFWImg.uiNextId)
        sTFWImg.uiNextId = uipacketId + 1;

    if(bIsFirstPacket)
    {
        ret = flash_area_open(FLASH_AREA_FW_IMAGE_STORE_ID, &flashArea);
        if(ret != 0)
        {
            FHALT("Flash Area Couldn't be opened");
            vSet_BootloaderMsgStatus(pstTBootMsg, false, eBootloader_Error_FlashAreaOpenFail);
            vAbort_BootloaderSession_Locked();
            bCancelWork = true;
            goto exit;
        }
    }

    for(; uiIndex < pstTBootMsg->uiLen; uiIndex += FW_IMG_WRITE_BYTE_LENGTH)
    {
        uiOffset = (uipacketId * DATA_PAYLOAD_PER_ONE_IMAGE_FRAME) + (uiIndex - 2);//sTFWImg.uiReceivedByteCount;
        if(uiOffset > uiAvailableFlashSize)
        {
            FHALT("FW Bytes exceed available flash area");
            vSet_BootloaderMsgStatus(pstTBootMsg, false, eBootloader_Error_FlashBoundsExceeded);
            vAbort_BootloaderSession_Locked();
            bCancelWork = true;
            goto exit;
        }
        if(uiOffset >= stTBtlMgmt_t.uiFWImgSize)
        {
            break;
        }

        if(uiOffset + FW_IMG_WRITE_BYTE_LENGTH <= stTBtlMgmt_t.uiFWImgSize)
            uiWriteLen = FW_IMG_WRITE_BYTE_LENGTH;
        else
            uiWriteLen = stTBtlMgmt_t.uiFWImgSize - uiOffset;

        if(uiOffset + FW_IMG_WRITE_BYTE_LENGTH > uiAvailableFlashSize)
        {
            FHALT("FW Bytes exceed available flash area");
            vSet_BootloaderMsgStatus(pstTBootMsg, false, eBootloader_Error_FlashBoundsExceeded);
            vAbort_BootloaderSession_Locked();
            bCancelWork = true;
            goto exit;
        }

        memset(uiaWriteBlock, 0xFF, sizeof(uiaWriteBlock));
        memcpy(uiaWriteBlock, &pstTBootMsg->uiaData[uiIndex], uiWriteLen);

        if(!bIsLostPacketsStateTriggered())
            sTFWImg.uiRunningCRC = u16CRC16_CCITT_Update(sTFWImg.uiRunningCRC, &pstTBootMsg->uiaData[uiIndex], uiWriteLen);

        uiImageSize = FW_IMAGE_SECONDARY_SLOT_WRITE_OFFSET + uiOffset;
        ret = flash_area_write(flashArea, uiImageSize, uiaWriteBlock, sizeof(uiaWriteBlock));
        if(ret != 0)
        {
            FHALT("Flash Area write fail");
            vSet_BootloaderMsgStatus(pstTBootMsg, false, eBootloader_Error_FlashAreaWriteFail);
            vAbort_BootloaderSession_Locked();
            bCancelWork = true;
            goto exit;
        }
        sTFWImg.uiReceivedByteCount += uiWriteLen;
        if(sTFWImg.uiReceivedByteCount >= stTBtlMgmt_t.uiFWImgSize)
        {
            break;
        }
    }

    vClear_FlashWriteFlag();
    vHandle_LostPacketRecovery(pstTBootMsg, uipacketId);
    vRefresh_BootloaderStateTimeOut_Locked(TIMEOUT_FW_UPDATE_NextPacket_ms);

exit:
    k_mutex_unlock(&kMutex_BootloaderSession);

    if(bCancelWork)
    {
        (void)k_work_cancel_delayable(&stBtlStateTimeOut_Work_t);
        (void)k_work_cancel_delayable(&stBtlTxTimeOut_Work_t);
    }
}

static void vHandle_LostPacketRecovery( sT_Bootloader_CtrlMsg_t * pstTBootMsg, uint16_t uiPacketId )
{
    if(!bIsLostPacketsStateTriggered())
        return;

    int i = 0;
    for(i = 0; i < sTFWImg.uiLostPacketCount; i++)
    {
        if(sTFWImg.staLostPacketIDs[i].uiPacketId == uiPacketId)
            break;
    }
    if(i == sTFWImg.uiLostPacketCount)
    {
        return;
    }
    if(sTFWImg.staLostPacketIDs[i].bIsHandled)
    {
        FHALT("Packet Id: %d is already handled as recovered packet", uiPacketId);
        return;
    }

    sTFWImg.staLostPacketIDs[i].bIsHandled = true;
    sTFWImg.uiRecoveredPacketCount++;
    vClear_LostPacketsDetected();
}

static void vValidate_FWImgCRC_FromFLash_AtLostPacketState( sT_Bootloader_CtrlMsg_t * pstTBootMsg, bool *bShouldMarkCompleted, 
                                                            eT_Bootloader_ErrorCode *eErrorCode )
{
    if(!bIsLostPacketsStateTriggered())
    {
        *bShouldMarkCompleted = false;
        *eErrorCode = eBootloader_Error_UndefinedBehavior;
        return;
    }
    if(sTFWImg.uiRecoveredPacketCount < sTFWImg.uiLostPacketCount)
    {
        *bShouldMarkCompleted = false;
        *eErrorCode = eBootloader_Error_UndefinedBehavior;
        return;
    }

    int ret = 0, iretryCount = 0;
    uint8_t uiaBuffer[DATA_PAYLOAD_PER_ONE_IMAGE_FRAME] = {0};
    uint32_t uiReadOffset = DATA_PAYLOAD_PER_ONE_IMAGE_FRAME * (sTFWImg.uiFirstLostPacketId) + 
                            FW_IMAGE_SECONDARY_SLOT_WRITE_OFFSET;
    uint32_t uiBytesToRead = stTBtlMgmt_t.uiFWImgSize - (DATA_PAYLOAD_PER_ONE_IMAGE_FRAME * (sTFWImg.uiFirstLostPacketId));

    while(uiBytesToRead > 0)
    {
        uint32_t uiValidBytes = (uiBytesToRead >= sizeof(uiaBuffer)) ? sizeof(uiaBuffer) : uiBytesToRead;
        ret = flash_area_read(flashArea, uiReadOffset, uiaBuffer, uiValidBytes);
        if(ret != 0)
        {
            FHALT("Flash Area read fail @ Offset: %d(RetryCount: %d)", uiReadOffset, iretryCount);
            iretryCount++;
            if(iretryCount >= 3)
            {
                *bShouldMarkCompleted = false;
                *eErrorCode = eBootloader_Error_FlashAreaReadFail;
                return;
            }
            
            k_msleep(5);
            continue;
        }
        sTFWImg.uiRunningCRC = u16CRC16_CCITT_Update(sTFWImg.uiRunningCRC, uiaBuffer, uiValidBytes);
        uiReadOffset += uiValidBytes;
        uiBytesToRead -= uiValidBytes;
    }
    
    if(sTFWImg.uiRunningCRC != stTBtlMgmt_t.uiCRC)
    {
        *eErrorCode = eBootloader_Error_CrcMismatch;
        *bShouldMarkCompleted = false;
    }
    else
    {
        *eErrorCode = eBootloader_Error_None;
        *bShouldMarkCompleted = true;
    }
    vClear_LostPacketsState();
}

static void vHandle_PacketId_Mismatch( sT_Bootloader_CtrlMsg_t * pstTBootMsg, uint16_t uipacketId, bool *bContinue )
{
    if(uipacketId > sTFWImg.uiNextId)
    {
        if(!bCanHandleMissingPackets(pstTBootMsg, uipacketId))
        {
            *bContinue = false;
            return;
        }
        *bContinue = true;
        return;
    }

    int i = 0;
    for( i = 0; i < sTFWImg.uiLostPacketCount; i++)
    {
        if(sTFWImg.staLostPacketIDs[i].uiPacketId == uipacketId)
        {
            if(sTFWImg.staLostPacketIDs[i].bIsHandled == false)
            {
                *bContinue = true;
                return;
            }
            vSet_BootloaderMsgStatus(pstTBootMsg, false, eBootloader_Error_InvalidPacketId);
            *bContinue = false;
            return;
        }
    }

    FHALT("Undefined at PacketId: %d", uipacketId);
    vSet_BootloaderMsgStatus(pstTBootMsg, false, eBootloader_Error_InvalidPacketId);
    *bContinue = false;
}

static bool bCanHandleMissingPackets( sT_Bootloader_CtrlMsg_t * pstTBootMsg, uint16_t uipacketId )
{
    int iDiff = uipacketId - sTFWImg.uiNextId;

    if((sTFWImg.uiLostPacketCount + iDiff) > MAX_NUMBER_OF_ALLOWABLE_LOST_PACKETs)
    {
        vSet_BootloaderMsgStatus(pstTBootMsg, false, eBootloader_Error_InvalidPacketId);
        FHALT("Invalid Packet Id. Expected: %d, Received: %d", sTFWImg.uiNextId, uipacketId);
        vAbort_BootloaderSession_Locked();
        return false;        
    }

    int i = 0;
    uint8_t uiIndex = 0;
    for(i = 0; i < iDiff; i++)
    {
        uiIndex = sTFWImg.uiLostPacketCount;
        sTFWImg.staLostPacketIDs[uiIndex].uiPacketId = sTFWImg.uiNextId + i;
        sTFWImg.staLostPacketIDs[uiIndex].bIsHandled = false;
        sTFWImg.uiLostPacketCount++;
    }

    if(!bIsLostPacketsStateTriggered())
    {
        vSet_FirstLostPacketId(sTFWImg.uiNextId);
        vSet_LostPacketsState();
    }    
    sTFWImg.uiNextId += i;
    vSet_LostPacketsDetected();

    return true;
}

static uint16_t u16CRC16_CCITT_Update(uint16_t crc, const uint8_t *data, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++)
    {
        crc ^= ((uint16_t)data[i] << 8);

        for (uint8_t bit = 0; bit < 8; bit++)
        {
            if (crc & 0x8000)
            {
                crc = (crc << 1) ^ 0x1021;
            }
            else
            {
                crc <<= 1;
            }
        }
    }

    return crc;
}

void vConfirm_MCUbootImage( void )
{
	int ret = boot_is_img_confirmed();

	if(ret < 0)
	{
		printk("MCUboot image confirmation check failed: %d\n\r", ret);
		return;
	}

	if(ret != 0)
	{
		return;
	}

	ret = boot_write_img_confirmed();
	if(ret != 0)
	{
		printk("MCUboot image confirmation failed: %d\n\r", ret);
		return;
	}

	printk("MCUboot image confirmed\n\r");
}

static void vAbort_BootloaderSession_Locked( void )
{
    vInvalidate_BootloaderStateTimeOut_Locked();

    if(flashArea != NULL)
    {
        flash_area_close(flashArea);
        flashArea = NULL;
    }

    stTBtlMgmt_t.uiFWImgSize = 0;
    stTBtlMgmt_t.uiFWRev = 0;
    stTBtlMgmt_t.uiHWRev = 0;
    stTBtlMgmt_t.uiLibRev = 0;
    stTBtlMgmt_t.uiCRC = 0;
    
    sTFWImg.uiLastId = 0;
    sTFWImg.uiLostPacketCount = 0;
    sTFWImg.uiNextId = 0;
    sTFWImg.uiReceivedByteCount = 0;
    sTFWImg.uiRunningCRC = 0xFFFF;
    sTFWImg.bIsFlashWritInProgress = false;
    sTFWImg.uiFirstLostPacketId = 0;
    sTFWImg.uiRecoveredPacketCount = 0;
    memset(sTFWImg.staLostPacketIDs, 0, sizeof(sTFWImg.staLostPacketIDs));

    vClear_LostPacketsDetected();
    vClear_LostPacketsState();

    vReinit_TxLostPacketInfo();

    /* The caller already owns kMutex_BootloaderSession. */
    stTBtlMgmt_t.eBtlState = eBootloader_State_Error;
    stTBtlMgmt_t.bIsFirstEntry = false;
    FHALT("Bootloader is in Error State");
}

static void vReinit_BootloaderStateMachine( void )
{
    k_mutex_lock(&kMutex_BootloaderSession, K_FOREVER);

    vInvalidate_BootloaderStateTimeOut_Locked();

    if(flashArea != NULL)
    {
        flash_area_close(flashArea);
        flashArea = NULL;
    }

    stTBtlMgmt_t.uiFWImgSize = 0;
    stTBtlMgmt_t.uiFWRev = 0;
    stTBtlMgmt_t.uiHWRev = 0;
    stTBtlMgmt_t.uiLibRev = 0;
    stTBtlMgmt_t.uiCRC = 0;
    
    sTFWImg.uiLastId = 0;
    sTFWImg.uiLostPacketCount = 0;
    sTFWImg.uiNextId = 0;
    sTFWImg.uiReceivedByteCount = 0;
    sTFWImg.uiRunningCRC = 0xFFFF;
    sTFWImg.bIsFlashWritInProgress = false;
    sTFWImg.uiFirstLostPacketId = 0;
    sTFWImg.uiRecoveredPacketCount = 0;
    memset(sTFWImg.staLostPacketIDs, 0, sizeof(sTFWImg.staLostPacketIDs));

    vClear_LostPacketsDetected();
    vClear_LostPacketsState();

    vReinit_TxLostPacketInfo();

    stTBtlMgmt_t.eBtlState = eBootloader_State_Idle;
    stTBtlMgmt_t.bIsFirstEntry = true;

    k_mutex_unlock(&kMutex_BootloaderSession);

    (void)k_work_cancel_delayable(&stBtlStateTimeOut_Work_t);
    (void)k_work_cancel_delayable(&stBtlTxTimeOut_Work_t);
}

static void vInit_TxLostPacketInfo( void )
{
    k_mutex_init(&sTTxLostPacketInfo.mutex_LockLostPacket);
    vReinit_TxLostPacketInfo();
}

static void vReinit_TxLostPacketInfo( void )
{
    if(bIsBootloader_Initialized())
    {
        k_mutex_lock(&sTTxLostPacketInfo.mutex_LockLostPacket, K_FOREVER);
    }
    sTTxLostPacketInfo.eTxState = eBootloader_Tx_Idle;    
    memset(&sTTxLostPacketInfo.uiaTxData, 0, sizeof(sTTxLostPacketInfo.uiaTxData));
    sTTxLostPacketInfo.uiFrameSeq = 0U;
    sTTxLostPacketInfo.uiRetryCount = 0U;
    memset(&sTTxLostPacketInfo.stTCANTxMsg_t, 0, sizeof(sTTxLostPacketInfo.stTCANTxMsg_t));

    if(bIsBootloader_Initialized())
    {
        k_mutex_unlock(&sTTxLostPacketInfo.mutex_LockLostPacket);
    }
}

static void vAbort_BootloaderSession( void )
{
    k_mutex_lock(&kMutex_BootloaderSession, K_FOREVER);
    vAbort_BootloaderSession_Locked();
    k_mutex_unlock(&kMutex_BootloaderSession);

    (void)k_work_cancel_delayable(&stBtlStateTimeOut_Work_t);
    (void)k_work_cancel_delayable(&stBtlTxTimeOut_Work_t);
}

static void vSet_BootloaderState( eT_Bootloader_State eState )
{
    if(eState >= eNUMBER_OF_BOOTLOADER_STATEs)
    {
        FHALT("Invalid Bootloader State Requested");
        return;
    }
    bool bCancelStateTimeout = false;

    k_mutex_lock(&kMutex_BootloaderSession, K_FOREVER);

    stTBtlMgmt_t.eBtlState = eState;
    stTBtlMgmt_t.bIsFirstEntry = true;

    switch(eState)
    {
        case eBootloader_State_FWImgWrite_InProg:
            vRefresh_BootloaderStateTimeOut_Locked(TIMEOUT_FW_UPREQ_TO_FW_UPDATE_ms);
            break;
        case eBootloader_State_Error:
            bCancelStateTimeout = false;
            k_mutex_unlock(&kMutex_BootloaderSession);
            return;
        default:
            vInvalidate_BootloaderStateTimeOut_Locked();
            bCancelStateTimeout = true;
            break;
    }

    k_mutex_unlock(&kMutex_BootloaderSession);

    if(bCancelStateTimeout)
    {
        (void)k_work_cancel_delayable(&stBtlStateTimeOut_Work_t);
    }
}

static eT_Bootloader_State eGet_BootloaderState( void )
{
    k_mutex_lock(&kMutex_BootloaderSession, K_FOREVER);
    eT_Bootloader_State eState = stTBtlMgmt_t.eBtlState;
    k_mutex_unlock(&kMutex_BootloaderSession);
    return eState;
}

static bool bIsFirstEntry( void )
{
    bool bFirstEntry;

    k_mutex_lock(&kMutex_BootloaderSession, K_FOREVER);
    bFirstEntry = stTBtlMgmt_t.bIsFirstEntry;
    if(bFirstEntry)
    {
        stTBtlMgmt_t.bIsFirstEntry = false;
    }
    k_mutex_unlock(&kMutex_BootloaderSession);

    return bFirstEntry;
}

static void vExecute_Bootloader_IdleState( sT_Bootloader_CtrlMsg_t * pstTBootMsg )
{
    if(pstTBootMsg == NULL)
    {
        FHALT("Null Pointer Reference");
        return;
    }    

    if(!bIsBootloader_Initialized())
    {
        FHALT("Must call 'bInit_BootloaderController' before anything");
        vSet_BootloaderMsgStatus(pstTBootMsg, false, eBootloader_Error_NotInitialized);
        return;
    }
    if(IS_FWUpdate_InProgress())
    {
        FHALT("FW Update Already in Progress");
        vSet_BootloaderMsgStatus(pstTBootMsg, false, eBootloader_Error_UpdateInProgress);
        return;
    }    
    if(pstTBootMsg->uiLen != 10)
    {
        FHALT("Invalid Payload Length for CMD = %d", pstTBootMsg->eCMD);
        vSet_BootloaderMsgStatus(pstTBootMsg, false, eBootloader_Error_InvalidPayloadLength);
        return;
    }

    if(bIsFirstEntry())
    {
        Bootloader_Print("Bootloader in Idle State");
    }

    switch(pstTBootMsg->eCMD)
    {
        case eBootloader_CMD_None:
        case eBootloader_CMD_FWUpReq:
            vInit_BootloaderManager_ForNewFWImage(pstTBootMsg);
            break;
        case eBootloader_CMD_FWUpMsg:
        case eBootloader_CMD_FWUpEnd:
        case eBootloader_CMD_FWUpPause:
        case eBootloader_CMD_FWUpStop:
        case eBootloader_CMD_GetLostPacketInfo:
        case eBootloader_CMD_RetLostPacketInfo:
        case eBootloader_CMD_AuthStart:
        case eBootloader_CMD_UpdateAuthResponse:
        default:
            vSet_BootloaderMsgStatus(pstTBootMsg, false, eBootloader_Error_InvalidCommand);
            FHALT("Invalid Command : %d @Bootloader Idle State", pstTBootMsg->eCMD);
            break;
    }

}

static void vInit_BootloaderManager_ForNewFWImage( sT_Bootloader_CtrlMsg_t * pstTBootMsg )
{
    if(pstTBootMsg == NULL)
    {
        FHALT("Null Pointer Reference");
        return;
    }

    stTBtlMgmt_t.uiFWImgSize = ((uint32_t)pstTBootMsg->uiaData[0] << 16) | ((uint32_t)pstTBootMsg->uiaData[1] << 8) | pstTBootMsg->uiaData[2];
    if(stTBtlMgmt_t.uiFWImgSize == 0)
    {
        FHALT("Invalid FW Image (Size: %d) ", stTBtlMgmt_t.uiFWImgSize);
        vSet_BootloaderMsgStatus(pstTBootMsg, false, eBootloader_Error_InvalidImageSize);
        return;        
    }
    if(stTBtlMgmt_t.uiFlashAreaSize <= FW_IMAGE_SECONDARY_SLOT_WRITE_OFFSET)
    {
        FHALT("Secondary slot is smaller than MCUboot write offset");
        vSet_BootloaderMsgStatus(pstTBootMsg, false, eBootloader_Error_FlashBoundsExceeded);
        return;
    }

    const uint32_t uiAvailableFlashSize = stTBtlMgmt_t.uiFlashAreaSize - FW_IMAGE_SECONDARY_SLOT_WRITE_OFFSET;
    if(stTBtlMgmt_t.uiFWImgSize > uiAvailableFlashSize)
    {
        FHALT("FW Image (Size: %d) larger than available memory(Size: %d)", 
            stTBtlMgmt_t.uiFWImgSize, uiAvailableFlashSize);
        vSet_BootloaderMsgStatus(pstTBootMsg, false, eBootloader_Error_ImageTooLarge);
        return;
    }
    
    stTBtlMgmt_t.uiHWRev = pstTBootMsg->uiaData[3];
    stTBtlMgmt_t.uiLibRev = ((uint16_t)pstTBootMsg->uiaData[4] << 8) | pstTBootMsg->uiaData[5];
    stTBtlMgmt_t.uiFWRev = ((uint16_t)pstTBootMsg->uiaData[6] << 8) | pstTBootMsg->uiaData[7];
    stTBtlMgmt_t.uiCRC = ((uint16_t)pstTBootMsg->uiaData[8] << 8) | pstTBootMsg->uiaData[9];
    stTBtlMgmt_t.stTFWImgCtrl_t.uiLostPacketCount = 0;
    sTFWImg.uiRunningCRC = 0xFFFF;
    sTFWImg.bIsFlashWritInProgress = false;
    sTFWImg.uiLastId = 0;
    sTFWImg.uiLostPacketCount = 0;
    sTFWImg.uiNextId = 0;
    sTFWImg.uiReceivedByteCount = 0;
    sTFWImg.uiFirstLostPacketId = 0;
    sTFWImg.uiRecoveredPacketCount = 0;
    vClear_LostPacketsDetected();
    vClear_LostPacketsState();
    memset(sTFWImg.staLostPacketIDs, 0, sizeof(sTFWImg.staLostPacketIDs));

    vErase_FlashArea(pstTBootMsg);
    if(!pstTBootMsg->bIsMsgOk)
        return;
    vSet_BootloaderState(eBootloader_State_FWImgWrite_InProg);
    pstTBootMsg->bIsMsgOk = true;
}

static void vErase_FlashArea( sT_Bootloader_CtrlMsg_t * pstTBootMsg )
{
    int ret = flash_area_open(FLASH_AREA_FW_IMAGE_STORE_ID, &flashArea);
    if(ret != 0)
    {
        FHALT("Flash Area couldn't be opened with Err: %d", ret);
        vSet_BootloaderMsgStatus(pstTBootMsg, false, eBootloader_Error_FlashAreaOpenFail);
        return;
    }

    ret = flash_area_erase(flashArea, 0, flashArea->fa_size);
    if(ret != 0)
    {
        flash_area_close(flashArea);
        flashArea = NULL;
        FHALT("Flash Area couldn't be erased with Err: %d", ret);
        vSet_BootloaderMsgStatus(pstTBootMsg, false, eBootloader_Error_FlashAreaEraseFail);
        return;        
    }

    flash_area_close(flashArea);
    flashArea = NULL;
    vSet_BootloaderMsgStatus(pstTBootMsg, true, eBootloader_Error_None);
}

static void vRefresh_BootloaderStateTimeOut(uint32_t uiTimeOut_ms)
{
    k_mutex_lock(&kMutex_BootloaderSession, K_FOREVER);
    vRefresh_BootloaderStateTimeOut_Locked(uiTimeOut_ms);
    k_mutex_unlock(&kMutex_BootloaderSession);
}

static void vRefresh_BootloaderStateTimeOut_Locked(uint32_t uiTimeOut_ms)
{
    iBootloaderStateDeadline_ms = k_uptime_get() + (int64_t)uiTimeOut_ms;
    bBootloaderStateTimeoutArmed = true;

    (void)k_work_reschedule(&stBtlStateTimeOut_Work_t, K_MSEC(uiTimeOut_ms));
}

static bool bSchedule_LostPacketTimeouts( void )
{
    bool bScheduled = false;

    /*
     * Keep the lock order consistent with session abort:
     * Bootloader session first, then the lost-packet transaction.
     */
    k_mutex_lock(&kMutex_BootloaderSession, K_FOREVER);
    k_mutex_lock(&sTTxLostPacketInfo.mutex_LockLostPacket, K_FOREVER);

    if(stTBtlMgmt_t.eBtlState == eBootloader_State_FWImgWrite_InProg &&
       (sTTxLostPacketInfo.eTxState == eBootloader_Tx_InProgress ||
        sTTxLostPacketInfo.eTxState == eBootloader_Tx_RetryInProgress))
    {
        (void)k_work_reschedule(&stBtlTxTimeOut_Work_t,
                                K_MSEC(TIMEOUT_LOST_PACKET_TX_ms));
        vRefresh_BootloaderStateTimeOut_Locked(
            TIMEOUT_LOST_PACKET_RECOVERY_PROCESS_ms);
        bScheduled = true;
    }

    k_mutex_unlock(&sTTxLostPacketInfo.mutex_LockLostPacket);
    k_mutex_unlock(&kMutex_BootloaderSession);

    return bScheduled;
}

static void vInvalidate_BootloaderStateTimeOut_Locked( void )
{
    bBootloaderStateTimeoutArmed = false;
    iBootloaderStateDeadline_ms = 0;
}

static void vCancel_BootloaderStateTimeOut( void )
{
    k_mutex_lock(&kMutex_BootloaderSession, K_FOREVER);
    vInvalidate_BootloaderStateTimeOut_Locked();
    k_mutex_unlock(&kMutex_BootloaderSession);

    (void)k_work_cancel_delayable(&stBtlStateTimeOut_Work_t);
}

static void vBtlState_TimeOutHandler( struct k_work *work )
{
    ARG_UNUSED(work);

    bool bReboot = false;

    k_mutex_lock(&kMutex_BootloaderSession, K_FOREVER);

    if(!bBootloaderStateTimeoutArmed)
    {
        k_mutex_unlock(&kMutex_BootloaderSession);
        return;
    }

    int64_t iRemainingTime_ms = iBootloaderStateDeadline_ms - k_uptime_get();
    if(iRemainingTime_ms > 0)
    {
        (void)k_work_reschedule(&stBtlStateTimeOut_Work_t, K_MSEC(iRemainingTime_ms));
        k_mutex_unlock(&kMutex_BootloaderSession);
        return;
    }

    vInvalidate_BootloaderStateTimeOut_Locked();

    eT_Bootloader_State estate = stTBtlMgmt_t.eBtlState;
    if(estate == eBootloader_State_Reboot)
    {
        bReboot = true;
    } 
    else
    {        
        FHALT("FW Update Timeout Occurred at State: %d", estate);
        vAbort_BootloaderSession_Locked();
    }

    k_mutex_unlock(&kMutex_BootloaderSession);

    if(!bReboot)
    {
        (void)k_work_cancel_delayable(&stBtlTxTimeOut_Work_t);
    }

    if(bReboot)
    {
       sys_reboot(SYS_REBOOT_COLD); 
    }
}

bool bInit_BootloaderController( void )
{
    Bootloader_Print("Bootloader Initialized");
    k_mutex_init(&kMutex_BootloaderSession);
    k_mutex_init(&kMutex_BootloaderDispatch);
    iBootloaderStateDeadline_ms = 0;
    bBootloaderStateTimeoutArmed = false;

    stTBtlMgmt_t.eBtlState = eBootloader_State_Inactive;
    stTBtlMgmt_t.bIsInitialized = false;
    stTBtlMgmt_t.uiCRC = 0;
    stTBtlMgmt_t.uiFWImgSize = 0;
    stTBtlMgmt_t.uiFWRev = 0;
    stTBtlMgmt_t.uiHWRev = 0;
    stTBtlMgmt_t.uiLibRev = 0;
    stTBtlMgmt_t.uiFlashAreaSize = 0;
    
    sTFWImg.uiLastId = 0;
    sTFWImg.uiLostPacketCount = 0;
    sTFWImg.uiNextId = 0;
    sTFWImg.uiReceivedByteCount = 0;
    sTFWImg.uiRunningCRC = 0xFFFF;    
    sTFWImg.bIsFlashWritInProgress = false;
    sTFWImg.bLostPacketDetected = false;
    sTFWImg.uiFirstLostPacketId = 0;
    sTFWImg.uiRecoveredPacketCount = 0;
    vClear_LostPacketsDetected();
    vClear_LostPacketsState();
    memset(sTFWImg.staLostPacketIDs, 0, sizeof(sTFWImg.staLostPacketIDs));

    vInit_TxLostPacketInfo();

    int ret = flash_area_open(FLASH_AREA_FW_IMAGE_STORE_ID, &flashArea);
    if(ret != 0)
    {
        FHALT("Failed to open Slot1 flash region");
        return false;
    }
    stTBtlMgmt_t.uiFlashAreaSize = flashArea->fa_size;
    flash_area_close(flashArea);
    flashArea = NULL;

    vInit_Bootloader_SecurityLayer();

    k_work_init_delayable(&stBtlStateTimeOut_Work_t, vBtlState_TimeOutHandler);
    k_work_init_delayable(&stBtlTxTimeOut_Work_t, vBtlTxTimeOutHandler);
    stTBtlMgmt_t.eBtlState = eBootloader_State_Idle;
    stTBtlMgmt_t.bIsInitialized = true;
    printk("Bootloader is initialized and in Idle State, waiting for FW Update Request...\n\r");
    return true;
}

static void vInit_Bootloader_SecurityLayer( void )
{
    sT_BootLoader_AuthControl_t *pstBtlSecrity_Ctrl = &stTBtlMgmt_t.stTBTLAuthCtrl;

    vClear_BootloaderAuth_Flag();
    pstBtlSecrity_Ctrl->uiSessionStartTime_ms = 0U;
    memset(pstBtlSecrity_Ctrl->uiaNonce, 0, sizeof(pstBtlSecrity_Ctrl->uiaNonce));
}

static inline void vSet_BootloaderAuth_Flag( void )
{
    sT_BootLoader_AuthControl_t *pstBtlSecrity_Ctrl = &stTBtlMgmt_t.stTBTLAuthCtrl;
    atomic_store_explicit(&pstBtlSecrity_Ctrl->bIsBootFWUpdate_Authorized, true, memory_order_release);
}

static inline void vClear_BootloaderAuth_Flag( void )
{
    sT_BootLoader_AuthControl_t *pstBtlSecrity_Ctrl = &stTBtlMgmt_t.stTBTLAuthCtrl;
    atomic_store_explicit(&pstBtlSecrity_Ctrl->bIsBootFWUpdate_Authorized, false, memory_order_release);
}

static inline bool bIsBootloader_Authenticated( void )
{
    sT_BootLoader_AuthControl_t *pstBtlSecrity_Ctrl = &stTBtlMgmt_t.stTBTLAuthCtrl;
    bool bRes = atomic_load_explicit(&pstBtlSecrity_Ctrl->bIsBootFWUpdate_Authorized, memory_order_acquire);
    return bRes;
}

bool bIsBootloader_Initialized( void )
{
    return stTBtlMgmt_t.bIsInitialized;
}

bool bIsFW_ImageWrite_InProgress( void )
{
    return (stTBtlMgmt_t.eBtlState == eBootloader_State_FWImgWrite_InProg);
}

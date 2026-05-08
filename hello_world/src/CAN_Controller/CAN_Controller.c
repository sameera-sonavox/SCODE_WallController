#include "CAN_Controller.h"
#include <stdint.h>
#include <stdbool.h>
#include "../Lib/GenericMacro.h"
#include "../Bootloader_Controller/Bootloader_Ctrl.h"
#include "../Bootloader_Controller/Bootloader_TypeDef.h"

K_MSGQ_DEFINE(msgq_CANBootloaderRx, sizeof(struct can_frame), BOOTLOADER_MSG_QUEUE_MAX_MESSAGEs, 4);

K_THREAD_STACK_DEFINE(thread_CANBootRxThread, BOOTLOADER_THREAD_STACK_SIZE_BYTEs);
static struct k_thread stCANBootRxThread_t;

sT_CANConfig_t stTCANConfig;
void vCAN_RXCallback(const struct device *dev, struct can_frame *frame, void *user_data);
void vCAN_BusError_Callback(eT_CAN_BUSState eBusState, struct can_bus_err_cnt stBusErrCount);
void vCAN_BusRecovery_Failed_Callback(eT_CANBus_Recovery_State_t eRecoveryState);
void vThread_BootloaderMsgHandler( void *p1, void *p2, void *p3 );

void vFormat_Bootloader_Msg( sT_Bootloader_CtrlMsg_t * pstBtlMsg, struct can_frame * pstcanMsg );
void vUpdate_ACK_Requirement( sT_Bootloader_CtrlMsg_t * pstBtlMsg );
void vCAN_ACK_NACK( const sT_Bootloader_CtrlMsg_t * pstBtlMsg, struct can_frame * pstcanMsg );

void vSend_CANMessage( sT_CAN_TXMsg_t stTMsg )
{
    eT_CAN_TxResult eResult = eFlexCAN_SendMsg(&stTMsg);
}

void vInit_CANController( void )
{
    stTCANConfig.eBusRecoveryType = eCAN_BUSRECTYPE_ManualRecover;
    stTCANConfig.busErrCallback = &vCAN_BusError_Callback;
    stTCANConfig.busFaultLatchCallback = &vCAN_BusRecovery_Failed_Callback;

    stTCANConfig.staRxFiltConfigs[0].eID = eCAN_RxFilter_ID_SysMgt;
    stTCANConfig.staRxFiltConfigs[0].eRxType = eCAN_RxType_Callback;
    stTCANConfig.staRxFiltConfigs[0].eFilterIdType = eCAN_RxFilterType_Standard;
    
    if(stTCANConfig.staRxFiltConfigs[0].eRxType == eCAN_RxType_Callback)
        stTCANConfig.staRxFiltConfigs[0].rxCallback = &vCAN_RXCallback;
    stTCANConfig.staRxFiltConfigs[0].pUserData = NULL;

    stTCANConfig.staRxFiltConfigs[1].eID = eCAN_RxFilter_ID_Bootloader;
    stTCANConfig.staRxFiltConfigs[1].eRxType = eCAN_RxType_MsgQueue;
    stTCANConfig.staRxFiltConfigs[1].eFilterIdType = eCAN_RxFilterType_Standard;
    
    if(stTCANConfig.staRxFiltConfigs[1].eRxType == eCAN_RxType_Callback)
        stTCANConfig.staRxFiltConfigs[1].rxCallback = &vCAN_RXCallback;
    else if(stTCANConfig.staRxFiltConfigs[1].eRxType == eCAN_RxType_MsgQueue)
    {
        stTCANConfig.staRxFiltConfigs[1].pstMsgQueue = &msgq_CANBootloaderRx;
    }
    else{
        return;
    }
    stTCANConfig.staRxFiltConfigs[1].pUserData = NULL;

    int ret = iFlexCAN_Init(&stTCANConfig, eNUMBER_OF_CAN_RX_CONFIGs);
    if(ret < 0)
        return;

    k_thread_create(&stCANBootRxThread_t, thread_CANBootRxThread, 
                    K_THREAD_STACK_SIZEOF(thread_CANBootRxThread),
                    vThread_BootloaderMsgHandler,
                    NULL, NULL, NULL, BOOTLOADER_THREAD_PRIORITY,
                    0, K_NO_WAIT);
}

void vThread_BootloaderMsgHandler( void *p1, void *p2, void *p3 )
{
    struct can_frame stTCANMsg;
    sT_Bootloader_CtrlMsg_t stTBTLCtrlMsg_t = {0};

    while(1)
    {
        int ret = k_msgq_get(&msgq_CANBootloaderRx, &stTCANMsg, K_FOREVER);
        if(ret != 0)
            continue;
        if(stTCANMsg.id != eCAN_RxFilter_ID_Bootloader)
        {
            FHALT("Invalid Msg for Bootloader");
            continue;
        }
        
        vFormat_Bootloader_Msg(&stTBTLCtrlMsg_t, &stTCANMsg);
        if(!stTBTLCtrlMsg_t.bIsMsgOk)
        {
            vCAN_ACK_NACK( &stTBTLCtrlMsg_t, &stTCANMsg );
            memset(&stTBTLCtrlMsg_t, 0, sizeof(stTBTLCtrlMsg_t));
            continue;            
        }

        vUpdateBootloader( &stTBTLCtrlMsg_t );

        if(stTBTLCtrlMsg_t.bIsACKReq)
        {
            vCAN_ACK_NACK(&stTBTLCtrlMsg_t, &stTCANMsg);
        }

        memset(&stTBTLCtrlMsg_t, 0, sizeof(stTBTLCtrlMsg_t));
    }
}

void vFormat_Bootloader_Msg( sT_Bootloader_CtrlMsg_t * pstBtlMsg, struct can_frame * pstcanMsg )
{
    if(pstBtlMsg == NULL || pstcanMsg == NULL)
    {
        FHALT("%s -> Null Pointer Error", __func__);
        return;
    }

    if(!bIsBootloader_Initialized())
    {
        FHALT("%s -> Bootloader not initialized", __func__);
        vSet_BootloaderMsgStatus(pstBtlMsg, false, eBootloader_Error_NotInitialized);
        return;
    }
    
    eT_Bootloader_Command eCMD = (eT_Bootloader_Command)pstcanMsg->data[0];
    if(eCMD >= eNUMBER_OF_BOOTLOADER_COMMANDs || eCMD < eBootloader_CMD_FWUpReq)
    {
        FHALT("%s -> Invalid Bootloader Command", __func__);
        vSet_BootloaderMsgStatus(pstBtlMsg, false, eBootloader_Error_InvalidCommand);
        return;
    }

    uint8_t uiLen = can_dlc_to_bytes(pstcanMsg->dlc);
    if(uiLen < 1)
    {
        FHALT("%s -> Invalid Payload Length", __func__);
        vSet_BootloaderMsgStatus(pstBtlMsg, false, eBootloader_Error_InvalidPayloadLength);
        return;        
    }

    pstBtlMsg->eCMD = eCMD;    
    pstBtlMsg->uiLen = uiLen - 1;
    memcpy(pstBtlMsg->uiaData, &pstcanMsg->data[1], pstBtlMsg->uiLen);
    vUpdate_ACK_Requirement(pstBtlMsg);
}

void vUpdate_ACK_Requirement( sT_Bootloader_CtrlMsg_t * pstBtlMsg )
{
    switch (pstBtlMsg->eCMD)
    {
        case eBootloader_CMD_FWUpReq:
        case eBootloader_CMD_FWUpEnd:
        case eBootloader_CMD_FWUpPause:
        case eBootloader_CMD_FWUpStop:
            pstBtlMsg->bIsACKReq = true;
            vSet_BootloaderMsgStatus(pstBtlMsg, true, eBootloader_Error_None);
            break;
        case eBootloader_CMD_FWUpMsg:
            pstBtlMsg->bIsACKReq = false;
            vSet_BootloaderMsgStatus(pstBtlMsg, true, eBootloader_Error_None);
            break;       
        default:
            FHALT("Invalid Bootloader Command");
            vSet_BootloaderMsgStatus(pstBtlMsg, false, eBootloader_Error_InvalidCommand);
            break;
    }
}

void vCAN_ACK_NACK( const sT_Bootloader_CtrlMsg_t * pstBtlMsg, struct can_frame * pstcanMsg )
{

}

void vCAN_RXCallback(const struct device *dev, struct can_frame *frame, void *user_data)
{
    //printk("%s-> Id: %d\n\r", __func__, frame->id);
}

void vCAN_BusError_Callback(eT_CAN_BUSState eBusState, struct can_bus_err_cnt stBusErrCount)
{
    printk("CAN Bus Error: %d\n\r", eBusState);
}

void vCAN_BusRecovery_Failed_Callback(eT_CANBus_Recovery_State_t eRecoveryState)
{
    printk("CAN Bus FaultLatch: %d\n\r", eRecoveryState);
}

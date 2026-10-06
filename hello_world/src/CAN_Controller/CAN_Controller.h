#ifndef CAN_CONTROLLER_H
#define CAN_CONTROLLER_H

#include "CAN/NXP_CAN_ProjDef.h"
#include "CAN/NXP_CAN_API.h"
#include "CAN_ControllerTypes.h"
#include "CAN_PacketPayloads.h"
#include "../WallController/WallController_Comm_Manager/WallController_Comm_Types.h"

#define CAN_SYSMGMT_MSG_QUEUE_MAX_MESSAGES        16U
#define CAN_SYSMGMT_THREAD_STACK_SIZE_BYTEs       1024U
#define CAN_SYSMGMT_THREAD_PRIORITY               4U

#define BOOTLOADER_MSG_QUEUE_MAX_MESSAGEs          16U
#define BOOTLOADER_THREAD_STACK_SIZE_BYTEs         1024U
#define BOOTLOADER_THREAD_PRIORITY                 5U
#define BOOTLOADER_DATA_PACKET_LENGTH              16U

extern bool bInit_CANController(void);
extern void vSend_CANMessage(sT_CAN_TXMsg_t *pstTMsg, sT_CmdTransaction_t *pstTransaction);
extern int iGet_CANSysMgmt_Message(struct can_frame *pstTCANMsg, k_timeout_t timeout);
extern void vSend_ACK_NACK(uint32_t uiDestID, eT_CANACK_Ctrl eACKNACK,
                          eT_SysMgmtErrorCode_t eErrorCode, uint8_t uiCommand, uint8_t uiRev);
extern void vSubscribe_ToCANBusCriticalFailure(
    can_bus_criticalfailure_subscribeCallback_t pvNotifyCallbackFn);
extern void vUnSubscribe_FromCANBusCriticalFailure(void);

#endif /* CAN_CONTROLLER_H */

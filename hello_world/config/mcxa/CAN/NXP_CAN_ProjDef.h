#ifndef NXP_CAN_PROJDEF_H
#define NXP_CAN_PROJDEF_H

#include <stddef.h>
#include <stdatomic.h>
#include "NXP_CAN_Types.h"

#define DEBUG_CAN_DEV_INIT
//#define DEBUG_CAN_TX
#define DEBUG_CAN_MANUAL_RECOVER

#define CAN_BUS_RECOVERY_WORKFn_SCHEDULE_TIME_ms    200U
#define CAN_BUS_RECOVERY_TIME_ms                    100U
#define CAN_BUS_RECOVERY_BACKOFF_TIME_ms            100U
#define CAN_BUS_MAX_RECOVERY_TURNs                  5U
#define CAN_RECOVERY_THREAD_STACK_SIZE_BYTEs       1024U
#define CAN_RECOVERY_THREAD_PRIORITY               4U

#define CAN_TX_QUEUE_DEPTH                         8U
#define CAN_TX_TIMEOUT_MS                          100U
#define CAN_TX_COMPLETION_TIMEOUT_MS               1000U
#define CAN_MSG_MAX_SIZE                           64U
#define CAN_TX_THREAD_STACK_SIZE_BYTEs             1024U
#define CAN_TX_THREAD_PRIORITY                     4U

/* One wall controller is connected; these IDs have no hardware offset. */
#define CAN_HOST_NODE_BOOTLOADER_ID                0x200U
#define CAN_HOST_NODE_SYSMGMT_ID                   0x201U
#define CAN_RX_BOOTLOADER_ID                       0x300U
#define CAN_RX_SYS_MGMT_ID                         0x400U
#define CAN_RX_HOST_BOOTLOADER_ID                  CAN_HOST_NODE_BOOTLOADER_ID
#define MAX_NUMBER_OF_TRANSACTION_TRACKINGs        20U

typedef enum {
    eCAN_DestNode_RxFilt_Bootloader = 0,
    eCAN_DestNode_RxFilt_SysMgmt,
    eNUMBER_OF_CAN_HOST_RXIDs
} eT_CAN_DestFiltID_t;

typedef enum {
    eCAN_RxFilter_ID_SysMgt = 0,
    eCAN_RxFilter_ID_Bootloader,
    eCAN_RxFilter_ID_HostBootloader,
    eNUMBER_OF_CAN_RX_CONFIGs
} eT_CAN_RxFilter_ID_t;

typedef struct {
    eT_CAN_RxFilter_ID_t eID;
    eT_CAN_RxFilterIDType_t eFilterIdType;
    eT_CAN_RxType_t eRxType;
    union {
        can_rx_callback_t rxCallback;
        struct k_msgq *pstMsgQueue;
    };
    void *pUserData;
} sT_CAN_RXFilterConfig_t;

typedef struct {
    _Atomic bool IsCANStopped;
    can_bus_criticalfailure_subscribeCallback_t busCriticalFailureAppCallback;
    eT_CANBus_Recovery_Type_t eBusRecoveryType;
    can_criticalbus_state_callback_t busErrCallback;
    can_busfault_latch_callback_t busFaultLatchCallback;
    can_frame_txComplete_callback_t txFrameCompleteCallback;
    sT_CAN_RXFilterConfig_t staRxFiltConfigs[eNUMBER_OF_CAN_RX_CONFIGs];
} sT_CANConfig_t;

extern const uint32_t uiaCAN_TxConfigIDs[eNUMBER_OF_CAN_HOST_RXIDs];
extern uint32_t uiGetCAN_DestID(eT_CAN_DestFiltID_t eDestNode);
extern int iGetCAN_RxFilterID(eT_CAN_RxFilter_ID_t eRxFilterId);
extern const struct device *pstGetCAN_Device(void);

#endif /* NXP_CAN_PROJDEF_H */

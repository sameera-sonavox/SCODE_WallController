#ifndef NXP_CAN_PROJDEF_H
#define NXP_CAN_PROJDEF_H

#include "NXP_CAN_Types.h"

#define DEBUG_CAN_DEV_INIT
#define DEBUG_CAN_TX
#define DEBUG_CAN_MANUAL_RECOVER

//Bus Recovery
#define CAN_BUS_RECOVERY_WORKFn_SCHEDULE_TIME_ms    200
#define CAN_BUS_RECOVERY_TIME_ms                    100
#define CAN_BUS_RECOVERY_BACKOFF_TIME_ms            100
#define CAN_BUS_MAX_RECOVERY_TURNs                  5

//CAN Bus Node IDs
#define CAN_NODE_0_ID                               0x123

//CAN Rx Message Buffer Filter IDs
#define CAN_RX_SYS_MGMT_ID                          0x123
#define CAN_RX_BOOTLOADER_ID                        0x300

typedef enum{
    eCAN_DestNode_0 = 0,
    eNUMBER_OF_CAN_TX_CONFIGs
} eT_CAN_DestNode_t;

typedef enum{
    eCAN_RxFilter_ID_SysMgt = 0,
    eCAN_RxFilter_ID_Bootloader,
    eNUMBER_OF_CAN_RX_CONFIGs
} eT_CAN_RxFilter_ID_t;

typedef struct{
    eT_CAN_RxFilter_ID_t eID;
    eT_CAN_RxFilterIDType_t eFilterIdType;
    eT_CAN_RxType_t eRxType;

    union
    {
        can_rx_callback_t rxCallback;
        struct k_msgq *pstMsgQueue;
    };
    void *pUserData; //Used for either callback user data or message queue pointer

} sT_CAN_RXFilterConfig_t;

typedef struct{
    eT_CANBus_Recovery_Type_t eBusRecoveryType;
    can_criticalbus_state_callback_t busErrCallback;
    can_busfault_latch_callback_t busFaultLatchCallback;
    sT_CAN_RXFilterConfig_t staRxFiltConfigs[eNUMBER_OF_CAN_RX_CONFIGs];
} sT_CANConfig_t;

extern const uint32_t uiaCAN_TxConfigIDs[eNUMBER_OF_CAN_TX_CONFIGs];
extern const uint32_t uiaCAN_RxFilterIDs[eNUMBER_OF_CAN_RX_CONFIGs];

extern int iGetCAN_DestID(eT_CAN_DestNode_t eDestNode);
extern int iGetCAN_RxFilterID(eT_CAN_RxFilter_ID_t eRxFilterId);
extern const struct device * pstGetCAN_Device(void);


#endif /* NXP_CAN_PROJDEF_H */
#ifndef NXP_CAN_PROJDEF_H
#define NXP_CAN_PROJDEF_H

#include "NXP_CAN_Types.h"

#define DEBUG_CAN_DEV_INIT
#define DEBUG_CAN_TX

//CAN Bus Node IDs
#define CAN_NODE_0_ID               0x123

//CAN Rx Message Buffer Filter IDs
#define CAN_RX_SYS_MGMT_ID          0x200
#define CAN_RX_BOOTLOADER_ID        0x300

typedef enum{
    eCAN_DestNode_0 = 0,
    eNUMBER_OF_CAN_TX_CONFIGs
} eT_CAN_DestNode_t;

typedef enum{
    eCAN_RxFilter_ID_SysMgt = 0,
    eCAN_RxFilter_ID_Bootloader,
    eNUMBER_OF_CAN_RX_CONFIGs
} eT_CAN_RxFilter_ID_t;

typedef enum{
    eCAN_RxFilterType_Standard = 0,
    eCAN_RxFilterType_Extended,
    eNUMBER_OF_FILT_ID_TYPEs
} eT_CAN_RxFilterIDType_t;

typedef enum{
    eCAN_RxType_Callback = 0,
    eCAN_RxType_MsgQueue,
    eNUMBER_OF_CAN_RX_TYPES
} eT_CAN_RxType_t;

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

extern const uint32_t uiaCAN_TxConfigIDs[eNUMBER_OF_CAN_TX_CONFIGs];
extern const uint32_t uiaCAN_RxFilterIDs[eNUMBER_OF_CAN_RX_CONFIGs];

extern int iGetCAN_DestID(eT_CAN_DestNode_t eDestNode);
extern int iGetCAN_RxFilterID(eT_CAN_RxFilter_ID_t eRxFilterId);
extern const struct device * pstGetCAN_Device(void);


#endif /* NXP_CAN_PROJDEF_H */
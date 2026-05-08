#ifndef NXP_CAN_TYPES_H
#define NXP_CAN_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <zephyr/drivers/can.h>

typedef enum{
    eCAN_LPState_Disable=0,
    eCAN_LPState_Doze,
    eCAN_LPState_Stop,
    eNUMBER_OF_CAN_LPMODES
} eT_CAN_LPState_t;

typedef enum{
    eCAN_BUS_Uninitialized = 0,
    eCAN_BUS_Active,
    eCAN_BUS_Fault,
    eCAN_BUS_OnlyForCritical_Traffic,
    eCAN_BUS_Warning,
    eCAN_BUS_ReInitializing,
    eNUMBER_OF_CAN_BUS_STATEs
} eT_CAN_BUSState;

typedef enum{
    eCAN_RxType_Callback = 0,
    eCAN_RxType_MsgQueue,
    eNUMBER_OF_CAN_RX_TYPES
} eT_CAN_RxType_t;

typedef enum{
    eCAN_RxFilterType_Standard = 0,
    eCAN_RxFilterType_Extended,
    eNUMBER_OF_FILT_ID_TYPEs
} eT_CAN_RxFilterIDType_t;

typedef enum{
    eCAN_BusRecType_AutoRecover = 0,
    eCAN_BUSRECTYPE_ManualRecover,
    eNUMBER_OF_CANBUS_RECOVERY_TYPEs
} eT_CANBus_Recovery_Type_t;

typedef enum{
    eCAN_Recovery_Idle = 0,
    eCAN_Recovery_InProgress,
    eCAN_Recovery_LatchedFault,
    eNUMBER_OF_BUS_RECOVERY_STATEs
} eT_CANBus_Recovery_State_t;

typedef enum {
    eCAN_TxResult_Ok = 0,
    eCAN_TxResult_InvalidMsg,
    eCAN_TxResult_BusUnavailable,
    eCAN_TxResult_QueueFull,
    eCAN_TxResult_SendFailed,
} eT_CAN_TxResult;

typedef struct{
    int uiID;
    uint8_t uiLen;
    uint8_t * puiData;
} sT_CAN_TXMsg_t;

typedef void (*can_criticalbus_state_callback_t)(eT_CAN_BUSState eBusState, struct can_bus_err_cnt stBusErrCount);
typedef void (*can_busfault_latch_callback_t)(eT_CANBus_Recovery_State_t eRecoveryState);

#endif /* NXP_CAN_TYPES_H */
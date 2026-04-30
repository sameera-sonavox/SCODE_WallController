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
    eCAN_FuncState_Normal = 0,
    eCAN_FuncState_Freeze,
    eCAN_FuncState_LoopBack,
    eCAN_FuncState_ListenOnly,
    eCAN_FuncState_CANFDActive,
    eNUMBER_OF_CAN_FUNCSTATES
} eT_CAN_FuncState_t;

typedef struct{
    int uiID;
    uint8_t uiLen;
    uint8_t * puiData;
} sT_CAN_TXMsg_t;

#endif /* NXP_CAN_TYPES_H */
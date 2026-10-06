#ifndef CAN_CONTROLLER_TYPES_H
#define CAN_CONTROLLER_TYPES_H

#include <stdint.h>
#include <stdbool.h>

typedef enum{
    eCAN_ACK = 10,
    eCAN_NACK,
    eNUMBER_OF_CAN_ACKTYPEs
} eT_CANACK_Ctrl;

#endif

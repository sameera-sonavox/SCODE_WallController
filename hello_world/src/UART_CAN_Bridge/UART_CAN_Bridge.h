#ifndef UART_CAN_BRIDGE_H
#define UART_CAN_BRIDGE_H

#include <zephyr/drivers/can.h>

extern void vInit_UART_CAN_Bridge( void );
extern void vUART_CAN_Bridge_ForwardCANFrame( const struct can_frame *pstFrame );

#endif

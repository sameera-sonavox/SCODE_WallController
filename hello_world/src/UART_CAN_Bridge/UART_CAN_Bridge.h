#ifndef UART_CAN_BRIDGE_H
#define UART_CAN_BRIDGE_H

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/drivers/can.h>

typedef void (*UART_CAN_Bridge_DataRequestCallback_t)( void );

extern void vInit_UART_CAN_Bridge( void );
extern void vUART_CAN_Bridge_ForwardCANFrame( const struct can_frame *pstFrame );
extern bool bUART_CAN_Bridge_SendData( const uint8_t *puiData, uint16_t uiLen );
extern bool bUART_CAN_Bridge_SendDataWithPostDelay( const uint8_t *puiData, uint16_t uiLen, uint32_t uiDelayMs );
extern void vUART_CAN_Bridge_RegisterADCDataRequestCallback( UART_CAN_Bridge_DataRequestCallback_t pfCallback );

#endif

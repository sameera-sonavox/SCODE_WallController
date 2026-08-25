#ifndef PC_UART_API_H
#define PC_UART_API_H

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/drivers/can.h>

typedef void (*PC_UART_API_DataRequestCallback_t)( void );

extern void vInit_PC_UART_API( void );
extern void vPC_UART_API_ForwardCANFrame( const struct can_frame *pstFrame );
extern bool bPC_UART_API_SendData( const uint8_t *puiData, uint16_t uiLen );
extern bool bPC_UART_API_SendDataWithPostDelay( const uint8_t *puiData, uint16_t uiLen, uint32_t uiDelayMs );
extern void vPC_UART_API_RegisterADCDataRequestCallback( PC_UART_API_DataRequestCallback_t pfCallback );

#endif

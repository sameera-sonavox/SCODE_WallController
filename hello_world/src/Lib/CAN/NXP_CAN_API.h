#ifndef NXP_CAN_API_H
#define NXP_CAN_API_H

#include "NXP_CAN_Types.h"
#include "NXP_CAN_ProjDef.h"
#include "../GenericMacro.h"

#define CAN_TX_QUEUE_DEPTH      8
#define CAN_TX_TIMEOUT_MS       100
#define CAN_MSG_MAX_SIZE        64

extern int iFlexCAN_Init( sT_CANConfig_t *pstCANConfig, uint32_t uiNumRxFilters );
extern eT_CAN_TxResult eFlexCAN_SendMsg( sT_CAN_TXMsg_t *pstTxMsg);
extern void vReset_CANStack( void );
extern bool bIsCANStackInitialized( void );

#endif /* NXP_CAN_API_H */
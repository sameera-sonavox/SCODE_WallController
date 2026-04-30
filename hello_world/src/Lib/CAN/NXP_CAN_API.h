#ifndef NXP_CAN_API_H
#define NXP_CAN_API_H

#include "NXP_CAN_Types.h"
#include "NXP_CAN_ProjDef.h"

extern int iFlexCAN_Init( sT_CAN_RXFilterConfig_t *psRxFilterConfigs, uint32_t uiNumRxFilters );
extern bool bFlexCAN_SendMsg( sT_CAN_TXMsg_t *pstTxMsg);

#endif /* NXP_CAN_API_H */
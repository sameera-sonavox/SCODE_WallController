#ifndef NXP_SPI_LINKEDLIST_H
#define NXP_SPI_LINKEDLIST_H

#include "../API_Usage_Definition.h"

#if defined(USE_SPI)

#include "NXP_SPI_Types.h"

extern sT_SPISlave_Control_t* pstCreate_SPISlave(sT_SPISlave_Config_t *pstSlaveConfig);
extern bool bInsert_SPISlave_AtBeginning(sT_SPISlave_Control_t **ppstHead, sT_SPISlave_Config_t *pstSlaveConfig);
extern bool bInsert_SPISlave_AtEnd(sT_SPISlave_Control_t **ppstHead, sT_SPISlave_Config_t *pstSlaveConfig);
extern void vRelease_SPISLaves(sT_SPISlave_Control_t **ppstHead);
extern sT_SPISlave_Control_t *pstGetSlaveInfo(eSPI_Slave_Id_t eSlaveId, sT_SPISlave_Control_t *pstHead);

extern sT_SPIRxBuff_t *pstCreate_SPIRxBuffer(uint8_t uiId, eRxBuffer_State_t eState, size_t uisize);
extern bool bInsert_SPIRxBuffer_AtEnd(sT_SPIRxBuff_t **ppstHead, uint8_t uiId, eRxBuffer_State_t eState, size_t uisize);
extern sT_SPIRxBuff_t *pstGet_RxBuffer_byState(sT_SPIRxBuff_t *pstHead, eRxBuffer_State_t eState);
extern sT_SPIRxBuff_t *pstGet_RxReadyBuffer_byId(sT_SPIRxBuff_t *pstHead, uint8_t uiId);
extern void vSet_RxBufferState(uint8_t uiId, eRxBuffer_State_t eState, sT_SPIRxBuff_t *pstHead);
extern void vFree_SPIRxBuffers( sT_SPIRxBuff_t **ppstHead );

#endif
#endif /* NXP_SPI_LINKEDLIST_H */
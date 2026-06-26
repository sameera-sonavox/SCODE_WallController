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

#endif
#endif /* NXP_SPI_LINKEDLIST_H */
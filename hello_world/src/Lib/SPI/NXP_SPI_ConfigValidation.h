#ifndef NXP_SPI_CONFIGVALIDATION_H
#define NXP_SPI_CONFIGVALIDATION_H

#include "../API_Usage_Definition.h"

#if defined(USE_SPI)

#include "NXP_SPI_Types.h"

extern bool bValidate_SPI_Config( sT_SPIConfig_t *pstSPIConfig );
extern void vUnregister_SlaveDevice(eSPI_Slave_Id_t eSlaveId);
extern bool bIsTransfer_OnValidModule(sT_SPIMasterTransfer_t *pstTTransfer);
#endif

#endif
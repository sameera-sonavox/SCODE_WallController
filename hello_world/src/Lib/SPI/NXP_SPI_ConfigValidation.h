#ifndef NXP_SPI_CONFIGVALIDATION_H
#define NXP_SPI_CONFIGVALIDATION_H

#include "../API_Usage_Definition.h"

#if defined(USE_SPI)

#include "NXP_SPI_Types.h"

extern bool bValidate_SPI_Config( sT_SPIConfig_t *pstSPIConfig );

#endif

#endif
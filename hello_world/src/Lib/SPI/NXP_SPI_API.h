#ifndef NXP_SPI_API_H
#define NXP_SPI_API_H

#include "../API_Usage_Definition.h"

#if defined(USE_SPI)

#include "NXP_SPI_Types.h"
#include "NXP_SPI_ProjDef.h"

#define DEBUG_SPI_DEV_INIT

/**
 * @brief Initializes the requested SPI module. User should provide all the slave configurations required inside 'sT_SPIConfig_t' 
 * if multiple slaves shares the same SPI bus. Then the API will apply the relevant slave configurations dynamically during transfer calls.
 * @note The API automatically handles the dynamically allocated memory for the slaves. Once created and call 'vInit_SPI', the user should not worry about de-llocation.
 * 
 * @param pstSPIConfig Configurations for the intended SPI module together with configurations for slaves who shares the bus
 */
extern void vInit_SPI( sT_SPIConfig_t *pstSPIConfig );
extern bool bDeInit_SPI( eSPIModule_t eSPIModule );
extern bool bSPI_Transfer_InMasterMode(sT_SPITransfer_t stTTransfer);

#endif

#endif
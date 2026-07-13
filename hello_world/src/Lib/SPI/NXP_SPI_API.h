#ifndef NXP_SPI_API_H
#define NXP_SPI_API_H

#include "../API_Usage_Definition.h"

#if defined(USE_SPI)

#include "NXP_SPI_Types.h"
#include "NXP_SPI_ProjDef.h"
#include "NXP_SPI_LinkedList.h"

#define DEBUG_SPI_DEV_INIT
/* #define DEBUG_SPI_SLAVE_TX
#define DEBUG_SPI_SLAVE_RX
#define DEBUG_SPI_SLAVE_IRQ */

/**
 * @brief Initializes the requested SPI module. User should provide all the slave configurations required inside 'sT_SPIConfig_t' 
 * if multiple slaves shares the same SPI bus. Then the API will apply the relevant slave configurations dynamically during transfer calls.
 * @note The API automatically handles the dynamically allocated memory for the slaves. Once created and call 'vInit_SPI', the user should not worry about de-llocation.
 * 
 * @param pstSPIConfig Configurations for the intended SPI module together with configurations for slaves who shares the bus
 */
extern void vInit_SPI( sT_SPIConfig_t *pstSPIConfig );
extern bool bDeInit_SPI( eSPIModule_t eSPIModule );
extern bool bSPI_Transfer_InMasterMode(sT_SPIMasterTransfer_t stTTransfer);
extern const sT_SPISlave_Config_t *pstGetSlaveConfig(eSPIModule_t eModuleId, eSPI_Slave_Id_t eSlaveId);
//extern bool bSPI_Busy(eSPIModule_t eModuleId);

/**
 * @brief Releases the 'Ready' buffer passed by the API to Application, when API is configured in Callback Mode.
 * @note  The User Application must call this in the application layer, because it is the responsibility of application layer
 *        to release the Rx Buffers defined.
 * @param eModuleId SPI Module Id
 * @param uiBuffId  The Buffer Id passed by API to the Application via Callback
 */
extern bool bSPI_ReleasePeripheralMode_RxBuffer( eSPIModule_t eModuleId, uint8_t uiBuffId );

extern bool bSPI_PeripheralSendResponse(sT_SPIPreipheralResponse_t stTSlaveResponse);

#endif

#endif
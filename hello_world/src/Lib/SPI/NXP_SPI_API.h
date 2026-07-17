#ifndef NXP_SPI_API_H
#define NXP_SPI_API_H

#include "../API_Usage_Definition.h"

#if defined(USE_SPI)

#include "NXP_SPI_Types.h"
#include "NXP_SPI_ProjDef.h"
#include "NXP_SPI_LinkedList.h"

#define DEBUG_SPI_DEV_INIT

#define SPI_SLAVE_RDYMONITOR_THREAD_STACK_SIZE_BYTEs        512
#define SPI_SLAVE_RDYMONITOR_THREAD_PRIORITY                2
#define SPI_RDY_MONITOR_PERIOD_ms                           1
#define SPI_RDY_TIMEOUT_PERIOD_ms                           15
#define SPI_MASTER_RESOURCE_LOCK_TIMEOUT_ms                 10

/**
 * @brief Initializes the requested SPI module. User should provide all the slave configurations required inside 'sT_SPIConfig_t' 
 * if multiple slaves shares the same SPI bus. Then the API will apply the relevant slave configurations dynamically during transfer calls.
 * @note The API automatically handles the dynamically allocated memory for the slaves. Once created and call 'vInit_SPI', the user should not worry about de-llocation.
 * 
 * @param pstSPIConfig Configurations for the intended SPI module together with configurations for slaves who shares the bus
 */
extern void vInit_SPI( sT_SPIConfig_t *pstSPIConfig );
extern bool bDeInit_SPI( eSPIModule_t eSPIModule );

/**
 * @brief This function will initiate the intended transfer on the requested SPI bus. The API rquires the user to keep the slaves in the global level,
 * since 'sT_SPIMasterTransfer_t' contains the SPI module id. So it acts as the mapper between slave and SPI module.
 * 
 * @brief This function will initiate a transfer on the defined SPI bus with the requested slave. Always checks the return value of the transfer.
 * @param 'stTTransfer: Refer to the 'sT_SPIMasterTransfer_t' inside NXP_SPI_Types.h'
 * @return 'True': Transfer is successfull
 * @return 'False': Transfer is not successful. This can be due to several reasons. In such a situation, always checks the slave status using 'eGetSPI_MasterModeExt_SlaveState'.
 *                  If the state is 'eSlave_RdyState_Timeout', then it means the slave has not come to active state within the time defined by 'SPI_RDY_TIMEOUT_PERIOD_ms'.
 * @note  The user application is asynchronously notified by the API with the transfer result through the respective Slave Callback function.
 *        If the result is 'eTransfer_Timeout', then it means Slave is not in Ready/Active state. Then the next step has to be decided by the user, based on the application requirement.
 */
extern bool bSPI_Transfer_InMasterMode(sT_SPIMasterTransfer_t stTTransfer);

extern eSPI_ExternalSlave_State_t eGetSPI_MasterModeExt_SlaveState(eSPIModule_t eModuleId, eSPI_Slave_Id_t eSlaveId);
extern void vReset_SPIMasterMode_ExtSlaveState(eSPIModule_t eModuleId, eSPI_Slave_Id_t eSlaveId);

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

#ifndef SPI_CONTROLLER_H
#define SPI_CONTROLLER_H

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#include "../Lib/SPI/NXP_SPI_API.h"

extern void vConfigure_SPI( void );
extern bool bSPI_SendData(eSPI_Slave_Id_t eSlaveId, uint8_t *puiTxData, uint8_t uiLen);
extern bool bSPI_ReceiveData(eSPI_Slave_Id_t eSlaveId, uint8_t *puiCMD, uint8_t uiCMDLen, uint8_t uiRxLen);

#endif
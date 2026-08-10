#ifndef SPICONTROLLER_H
#define SPICONTROLLER_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "SPI/NXP_SPI_API.h"

extern bool bInit_LPSPI_ForExtFlash( void );
extern bool bSPI_Transceive(sT_SPIMasterTransfer_t *pstTSPITransfer);
bool bSPI_Write(sT_SPIMasterTransfer_t *pstTSPITransfer);

#endif//SPICONTROLLER_H
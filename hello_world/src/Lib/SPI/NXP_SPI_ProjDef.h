#ifndef NXP_SPI_PROJDEF_H
#define NXP_SPI_PROJDEF_H

typedef enum
{
    eSPI_Slave_0,
    eSPI_Slave_1,
    eNUMBER_OF_SPI_SLAVEs
} eSPI_Slave_Id_t;

#define SPI0_INTR_PRIORITY                          2
#define SPI1_INTR_PRIORITY                          5
#define SPI_MASTER_TRANSFER_TIMEOUT_ms              100

#define SPI_SLAVE_DEFAULT_OVERFLOW_FRAMECOUNT       5
#define SPI_SLAVE_RxCallBack_InvalidBuffId          0xFF

#define USE_SPI_0
//#define USE_SPI_1

#endif
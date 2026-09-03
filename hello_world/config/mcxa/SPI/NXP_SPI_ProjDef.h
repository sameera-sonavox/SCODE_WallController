#ifndef NXP_SPI_PROJDEF_H
#define NXP_SPI_PROJDEF_H


#include <zephyr/drivers/gpio.h>

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
#define SPI_FRAME_SIZE_BITS                         8U

//#define USE_SPI_0
#define USE_SPI_1

/**
 * @note If a SPI module is configured as slave, it is safe and standard to define a HW RDY pin.
 *       If you define a GPIO pin in an overlay as below( use exact naming conventions), then the API
 *       will use that pin automatically.
 */
#if defined(USE_SPI_0)
#define USE_SPI0_SLAVE_HW_RDY_GPIO
#endif

/**
 * @note If you define the SPI module as master, then define the HW ready signals for respective slaves here. Then 
 * individual slave configurations can reference them
 */

 //eSPI_Slave_0
 #define SPI0_SLAVE0_RDY_GPIO_NODE DT_ALIAS(spi0_slave0_hw_rdy)
 #if DT_NODE_HAS_STATUS(SPI0_SLAVE0_RDY_GPIO_NODE, okay)
    static const struct gpio_dt_spec stSPI0_Slave0_RdyGPIO = GPIO_DT_SPEC_GET(SPI0_SLAVE0_RDY_GPIO_NODE, gpios);
#else
    static const struct gpio_dt_spec stSPI0_Slave0_RdyGPIO = {0};
#endif

#endif//NXP_SPI_PROJDEF_H


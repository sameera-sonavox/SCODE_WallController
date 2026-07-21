#ifndef SPI_CONTROLLER_SLAVE_H
#define SPI_CONTROLLER_SLAVE_H

#ifdef USE_SPI

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#include "../Lib/SPI/NXP_SPI_API.h"

extern void vConfigure_SPISLave( void );

#endif
#endif
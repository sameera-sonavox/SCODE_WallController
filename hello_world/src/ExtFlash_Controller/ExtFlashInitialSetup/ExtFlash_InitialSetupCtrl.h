#ifndef EXTFLASH_INITIALSETUPCTRL_H
#define EXTFLASH_INITIALSETUPCTRL_H

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

#include "../Lib/SPI/NXP_SPI_API.h"
#include "../ExtFlash_ProjDef.h"

extern bool bExec_Flash_InitialSetup( void );
extern bool bExtFlash_WriteEnable( void );
extern bool bExtFlash_WriteDisable( void );
extern bool bGet_ExtFlash_Busy(bool *pbIsBusy);
extern bool bIsExtFlash_WEL_Cleared( void );

#endif//EXTFLASH_INITIALSETUPCTRL_H

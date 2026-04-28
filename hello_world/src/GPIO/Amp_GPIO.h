#ifndef AMP_GPIO_H
#define AMP_GPIO_H

#include "Amp_Types.h"
#include "../Lib/GenericMacro.h"

#define DEBUG_GPIO_CONFIG

#define SET_AMP_SD()    vSet_GPIO_OutputState(eAMP_SD, eOUTPUT_High)
#define CLEAR_AMP_SD()  vSet_GPIO_OutputState(eAMP_SD, eOUTPUT_Low)

void vInit_Amp_GPIO(void);
void vSet_GPIO_OutputState(eAmp_GPIO eGPIO,eGPIO_OutputState eOutputState);

#endif /* AMP_GPIO_H */

#ifndef EXTFLASH_CONTROLLERTYPES_H
#define EXTFLASH_CONTROLLERTYPES_H

#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>

typedef union
{
    struct
    {
        unsigned WIP : 1;//Write in Progress - device busy
        unsigned WEL : 1;//write-enable latch
        unsigned BPn : 4;//Block Protection
        unsigned QE : 1;//Quad Enable
        unsigned SRWD : 1;//Status Register Write Protection
    } bits;
    uint8_t uiRegVal;
} ExtFlash_StatusReg;

typedef union
{
    struct
    {
        unsigned Resv_0 : 1;//Reserved
        unsigned Resv_1 : 1;//Reserved
        unsigned Resv_2 : 1;//Reserved
        unsigned TB : 1;//Top/Bottom Select (0: Top, 1: Bottom)
        unsigned Resv_4 : 1;//Reserved
        unsigned Resv_5 : 1;//Reserved
        unsigned DC : 1;//Dummy Cycle
        unsigned Resv_7 : 1;//Reserved
    } bits;
    uint8_t uiRegVal;
} ExtFlash_ConfigReg1;

typedef union
{
    struct
    {
        unsigned Resv_0 : 1;//Reserved
        unsigned LH_SW : 1;//L/H Switch (0: Ultra Low Power, 1: High Performance)
        unsigned Resv_2 : 1;//Reserved
        unsigned Resv_3 : 1;//Reserved
        unsigned Resv_4 : 1;//Reserved
        unsigned Resv_5 : 1;//Reserved
        unsigned Resv_6 : 1;//Reserved
        unsigned Resv_7 : 1;//Reserved
    } bits;
    uint8_t uiRegVal;
} ExtFlash_ConfigReg2;

typedef struct
{
    bool bIsExtFlashSetupSuccess;
} sT_ExtFlash_Control_t;

#endif//EXTFLASH_CONTROLLERTYPES_H

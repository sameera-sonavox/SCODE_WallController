#ifndef NXP_CPU_LOADMON_H
#define NXP_CPU_LOADMON_H

#include <stdint.h>

typedef enum
{
    eGetTime_T0,
    eGetTime_T1,
    eNUMBER_OF_TIME_LIMITs
} eT_GetTime_Type;

typedef enum
{
    eTime_uS,
    eTime_mS,
    eTime_S,
    eNUMBER_OF_TIME_TYPEs
} eTime_Type;

extern void vPrint_CPU_Load( const char *pcLabel );
extern void vGet_Execution_Time_uS( const char *pIdentifier, eT_GetTime_Type eTimePeriodType, eTime_Type eTimeType );

#endif

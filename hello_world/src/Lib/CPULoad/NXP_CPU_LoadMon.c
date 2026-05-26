
#include <zephyr/kernel.h>
#include <zephyr/timing/timing.h>

#include "..\GenericMacro.h"
#include "NXP_CPU_LoadMon.h"

uint32_t uiaTimeHolder[eNUMBER_OF_TIME_LIMITs] = {0};
static uint64_t uPrevTotalCycles;
static uint64_t uPrevNonIdleCycles;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdouble-promotion"

void vGet_Execution_Time_uS( const char *pIdentifier, eT_GetTime_Type eTimePeriodType, eTime_Type eTimeType )
{
    if(eTimePeriodType < eGetTime_T0 || eTimePeriodType >= eNUMBER_OF_TIME_LIMITs)
    {
        FHALT("Invalid Time Type");
        return;
    }

    uiaTimeHolder[eTimePeriodType] = k_cycle_get_32();
    if(eTimePeriodType == eGetTime_T0)
        return;
    
    uint32_t elapsedTime_us = k_cyc_to_us_floor32(uiaTimeHolder[eGetTime_T1] - uiaTimeHolder[eGetTime_T0]);

    switch(eTimeType)
    {
        case eTime_uS:
            printf("\n\r");
            printf("**************************************\n\r");
            printf("[%s] CPU Utilize Time %u us\n\r", pIdentifier, elapsedTime_us);
            printf("**************************************\n\r");
            printf("\n\r");
            break;
        case eTime_mS:
            printf("\n\r");
            printf("**************************************\n\r");
            printf("[%s] CPU Utilize Time %u.%03u ms\n\r", pIdentifier, elapsedTime_us / 1000U, elapsedTime_us % 1000U);
            printf("**************************************\n\r");
            printf("\n\r");
            break;
        case eTime_S:
            printf("\n\r");
            printf("**************************************\n\r");
            printf("[%s] CPU Utilize Time %u.%03u sec\n\r", pIdentifier, elapsedTime_us / 1000000U, elapsedTime_us % 1000000U);
            printf("**************************************\n\r");
            printf("\n\r");
            break;
        default:
            FHALT("Invalid Time Type");
            break;
    }

}

void vPrint_CPU_Load( const char *pcLabel )
{
    uint64_t total_cycles;
    uint64_t non_idle_cycles;
    uint64_t delta_total;
    uint64_t delta_non_idle;
    uint32_t cpu_load_permille;
    k_thread_runtime_stats_t total_stats;

    if(k_thread_runtime_stats_all_get(&total_stats) != 0)
    {
        FHALT("Failed to read CPU runtime statistics");
        return;
    }

    total_cycles = total_stats.execution_cycles;
    non_idle_cycles = total_stats.total_cycles;

    delta_total = total_cycles - uPrevTotalCycles;
    delta_non_idle = non_idle_cycles - uPrevNonIdleCycles;

    if(delta_total == 0U)
    {
        return;
    }

    cpu_load_permille = (uint32_t)((delta_non_idle * 1000U) / delta_total);

    printf("[CPU] %s load = %u.%u %%\n\r",
           pcLabel,
           cpu_load_permille / 10U,
           cpu_load_permille % 10U);

    uPrevTotalCycles = total_cycles;
    uPrevNonIdleCycles = non_idle_cycles;
}


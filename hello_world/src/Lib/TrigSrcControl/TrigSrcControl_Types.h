#ifndef TRIGSRCCONTROL_TYPES_H
#define TRIGSRCCONTROL_TYPES_H

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    eTrigSrcGroup_None = 0,
    eTrigSrcGroup_CTIMER,
    eTrigSrcGroup_LPTIMER,
    eTrigSrcGroup_AOI,
    eTrigSrcGroup_GPIO,
    eTrigSrcGroup_CPU,
    eTrigSrcGroup_ADC,
    eNUMBER_OF_TRIG_SRC_GROUPs
} eTrigSrcGroup_t;

typedef enum
{
    eTrigSrc_CTIMER0_MAT0 = 0,
    eTrigSrc_CTIMER0_MAT1,
    eTrigSrc_CTIMER1_MAT0,
    eTrigSrc_CTIMER1_MAT1,
    eTrigSrc_CTIMER2_MAT0,
    eTrigSrc_CTIMER2_MAT1,
    eTrigSrc_CTIMER3_MAT0,
    eTrigSrc_CTIMER3_MAT1,
    eTrigSrc_CTIMER4_MAT0,
    eTrigSrc_CTIMER4_MAT1,
    eNUMBER_OF_CTIMER_TRIG_SRCs
} eTrigSrc_CTimer_t;

typedef enum
{
    eTrigConsumer_ADC0_Slot0 = 0,
    eTrigConsumer_ADC0_Slot1,
    eTrigConsumer_ADC0_Slot2,
    eTrigConsumer_ADC0_Slot3,
    eTrigConsumer_ADC1_Slot0,
    eTrigConsumer_ADC1_Slot1,
    eTrigConsumer_ADC1_Slot2,
    eTrigConsumer_ADC1_Slot3,
    eTrigConsumer_DAC0_WaveGen,
    eTrigConsumer_PWM_CTimer0,
    eTrigConsumer_PWM_CTimer1,
    eTrigConsumer_PWM_CTimer2,
    eTrigConsumer_PWM_CTimer3,
    eTrigConsumer_PWM_CTimer4,
    eNUMBER_OF_TRIG_CONSUMERs
} eTrigSrcConsumer_t;

typedef enum
{
    eTrigShareMode_SharedFixed = 0,
    eTrigShareMode_Exclusive,
    eNUMBER_OF_TRIG_SHARE_MODEs
} eTrigSrcShareMode_t;

typedef enum
{
    eTrigSrc_AOI_0 = 0,
    eTrigSrc_AOI_1,
    eNUMBER_OF_DAC_AOI_TRIG_SRCs
} eTrigSrc_AOI_t;

typedef enum
{
    eTrigSrc_WUU = 0,
    eTrigSrc_ARM_TXEV,
    eNUMBER_OF_DAC_CPU_TRIG_SRCs
} eTrigSrc_CPU_t;

typedef struct
{
    bool bIsConfigured;
    bool bIsRunning;
    eTrigSrcShareMode_t eShareMode;
    uint32_t uiFrequency_Hz;
    uint32_t uiConsumerMask;
} sT_TrigSrcStatus_t;

#endif

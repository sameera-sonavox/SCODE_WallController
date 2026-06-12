#include <string.h>
#include <zephyr/kernel.h>

#include "fsl_clock.h"
#include "fsl_ctimer.h"

#include "TrigSrcControl.h"
#include "../GenericMacro.h"

typedef struct
{
    CTIMER_Type *pstBase;
    uint8_t uiInstance;
    ctimer_match_t eMatchChannel;
    clock_attach_id_t eClockAttach;
    clock_div_name_t eClockDiv;
} sT_CTimerHWMap_t;

typedef struct
{
    sT_TrigSrcStatus_t stStatus;
} sT_CTimerCtrl_t;

static const sT_CTimerHWMap_t staCTimerHWMap[eNUMBER_OF_CTIMER_TRIG_SRCs] = {
    [eTrigSrc_CTIMER0_MAT0] = {CTIMER0, 0U, kCTIMER_Match_0, kFRO_HF_to_CTIMER0, kCLOCK_DivCTIMER0},
    [eTrigSrc_CTIMER0_MAT1] = {CTIMER0, 0U, kCTIMER_Match_1, kFRO_HF_to_CTIMER0, kCLOCK_DivCTIMER0},
    [eTrigSrc_CTIMER1_MAT0] = {CTIMER1, 1U, kCTIMER_Match_0, kFRO_HF_to_CTIMER1, kCLOCK_DivCTIMER1},
    [eTrigSrc_CTIMER1_MAT1] = {CTIMER1, 1U, kCTIMER_Match_1, kFRO_HF_to_CTIMER1, kCLOCK_DivCTIMER1},
    [eTrigSrc_CTIMER2_MAT0] = {CTIMER2, 2U, kCTIMER_Match_0, kFRO_HF_to_CTIMER2, kCLOCK_DivCTIMER2},
    [eTrigSrc_CTIMER2_MAT1] = {CTIMER2, 2U, kCTIMER_Match_1, kFRO_HF_to_CTIMER2, kCLOCK_DivCTIMER2},
    [eTrigSrc_CTIMER3_MAT0] = {CTIMER3, 3U, kCTIMER_Match_0, kFRO_HF_to_CTIMER3, kCLOCK_DivCTIMER3},
    [eTrigSrc_CTIMER3_MAT1] = {CTIMER3, 3U, kCTIMER_Match_1, kFRO_HF_to_CTIMER3, kCLOCK_DivCTIMER3},
    [eTrigSrc_CTIMER4_MAT0] = {CTIMER4, 4U, kCTIMER_Match_0, kFRO_HF_to_CTIMER4, kCLOCK_DivCTIMER4},
    [eTrigSrc_CTIMER4_MAT1] = {CTIMER4, 4U, kCTIMER_Match_1, kFRO_HF_to_CTIMER4, kCLOCK_DivCTIMER4},
};

static sT_CTimerCtrl_t staCTimerCtrl[eNUMBER_OF_CTIMER_TRIG_SRCs];
K_MUTEX_DEFINE(kTrigSrcMutex);

static bool bIsValidRequest(eTrigSrc_CTimer_t eSource, eTrigSrcConsumer_t eConsumer)
{
    return (eSource < eNUMBER_OF_CTIMER_TRIG_SRCs) &&
           (eConsumer < eNUMBER_OF_TRIG_CONSUMERs);
}

static bool bIsOwner(const sT_CTimerCtrl_t *pstCtrl, eTrigSrcConsumer_t eConsumer)
{
    return (pstCtrl->stStatus.uiConsumerMask & (1UL << eConsumer)) != 0U;
}

static bool bIsSiblingSourceInUse(eTrigSrc_CTimer_t eSource)
{
    uint8_t uiInstance = staCTimerHWMap[eSource].uiInstance;

    for(uint8_t i = 0U; i < eNUMBER_OF_CTIMER_TRIG_SRCs; i++)
    {
        if((i == eSource) || (staCTimerHWMap[i].uiInstance != uiInstance))
            continue;

        if(staCTimerCtrl[i].stStatus.uiConsumerMask != 0U)
            return true;
    }
    return false;
}

static bool bConfigureMatch(eTrigSrc_CTimer_t eSource, uint32_t uiFrequency_Hz)
{
    const sT_CTimerHWMap_t *pstMap = &staCTimerHWMap[eSource];
    ctimer_config_t stTimerConfig;
    ctimer_match_config_t stMatchConfig = {0};

    if((uiFrequency_Hz == 0U) || (uiFrequency_Hz > (UINT32_MAX / 2U)))
        return false;

    CLOCK_AttachClk(pstMap->eClockAttach);
    CLOCK_SetClockDiv(pstMap->eClockDiv, 1U);

    uint32_t uiTimerClock_Hz = CLOCK_GetCTimerClkFreq(pstMap->uiInstance);
    uint32_t uiToggleFrequency_Hz = uiFrequency_Hz * 2U;
    if((uiTimerClock_Hz == 0U) || (uiToggleFrequency_Hz > uiTimerClock_Hz))
        return false;

    uint32_t uiMatchValue = (uiTimerClock_Hz / uiToggleFrequency_Hz);
    if(uiMatchValue == 0U)
        return false;

    CTIMER_GetDefaultConfig(&stTimerConfig);
    stTimerConfig.mode = kCTIMER_TimerMode;
    stTimerConfig.prescale = 0U;

    /*
     * CTIMER_Init enables the peripheral clock gate. Accessing TCR before this
     * point bus-faults when the selected CTIMER is disabled in devicetree.
     */
    CTIMER_Init(pstMap->pstBase, &stTimerConfig);
    CTIMER_StopTimer(pstMap->pstBase);
    CTIMER_Reset(pstMap->pstBase);

    stMatchConfig.matchValue = uiMatchValue - 1U;
    stMatchConfig.enableCounterReset = true;
    stMatchConfig.enableCounterStop = false;
    stMatchConfig.outControl = kCTIMER_Output_Toggle;
    stMatchConfig.outPinInitState = false;
    stMatchConfig.enableInterrupt = false;
    CTIMER_SetupMatch(pstMap->pstBase, pstMap->eMatchChannel, &stMatchConfig);
    CTIMER_Reset(pstMap->pstBase);
    return true;
}

bool bTrigSrc_AcquireCTimer(eTrigSrc_CTimer_t eSource,
                            eTrigSrcConsumer_t eConsumer,
                            eTrigSrcShareMode_t eShareMode)
{
    if(!bIsValidRequest(eSource, eConsumer) || (eShareMode >= eNUMBER_OF_TRIG_SHARE_MODEs))
        return false;

    k_mutex_lock(&kTrigSrcMutex, K_FOREVER);
    sT_CTimerCtrl_t *pstCtrl = &staCTimerCtrl[eSource];
    uint32_t uiConsumerBit = (1UL << eConsumer);

    if((pstCtrl->stStatus.uiConsumerMask & uiConsumerBit) != 0U)
    {
        bool bSameMode = pstCtrl->stStatus.eShareMode == eShareMode;
        k_mutex_unlock(&kTrigSrcMutex);
        return bSameMode;
    }

    if(bIsSiblingSourceInUse(eSource))
    {
        k_mutex_unlock(&kTrigSrcMutex);
        return false;
    }

    if((pstCtrl->stStatus.uiConsumerMask != 0U) &&
       ((pstCtrl->stStatus.eShareMode == eTrigShareMode_Exclusive) ||
        (eShareMode == eTrigShareMode_Exclusive)))
    {
        k_mutex_unlock(&kTrigSrcMutex);
        return false;
    }

    if(pstCtrl->stStatus.uiConsumerMask == 0U)
        pstCtrl->stStatus.eShareMode = eShareMode;

    pstCtrl->stStatus.uiConsumerMask |= uiConsumerBit;
    k_mutex_unlock(&kTrigSrcMutex);
    return true;
}

void vTrigSrc_ReleaseCTimer(eTrigSrc_CTimer_t eSource, eTrigSrcConsumer_t eConsumer)
{
    if(!bIsValidRequest(eSource, eConsumer))
        return;

    k_mutex_lock(&kTrigSrcMutex, K_FOREVER);
    sT_CTimerCtrl_t *pstCtrl = &staCTimerCtrl[eSource];
    if(!bIsOwner(pstCtrl, eConsumer))
    {
        k_mutex_unlock(&kTrigSrcMutex);
        return;
    }

    pstCtrl->stStatus.uiConsumerMask &= ~(1UL << eConsumer);

    if(pstCtrl->stStatus.uiConsumerMask == 0U)
    {
        if(pstCtrl->stStatus.bIsConfigured)
        {
            const sT_CTimerHWMap_t *pstMap = &staCTimerHWMap[eSource];
            CTIMER_StopTimer(pstMap->pstBase);
            CTIMER_Reset(pstMap->pstBase);
            CTIMER_Deinit(pstMap->pstBase);
        }
        memset(pstCtrl, 0, sizeof(*pstCtrl));
    }
    k_mutex_unlock(&kTrigSrcMutex);
}

bool bTrigSrc_ConfigureCTimer(eTrigSrc_CTimer_t eSource,
                              eTrigSrcConsumer_t eConsumer,
                              uint32_t uiFrequency_Hz)
{
    if(!bIsValidRequest(eSource, eConsumer))
        return false;

    k_mutex_lock(&kTrigSrcMutex, K_FOREVER);
    sT_CTimerCtrl_t *pstCtrl = &staCTimerCtrl[eSource];
    if(!bIsOwner(pstCtrl, eConsumer))
    {
        k_mutex_unlock(&kTrigSrcMutex);
        return false;
    }

    if(pstCtrl->stStatus.bIsConfigured)
    {
        bool bCompatible = pstCtrl->stStatus.uiFrequency_Hz == uiFrequency_Hz;
        k_mutex_unlock(&kTrigSrcMutex);
        return bCompatible;
    }

    bool bResult = bConfigureMatch(eSource, uiFrequency_Hz);
    if(bResult)
    {
        pstCtrl->stStatus.bIsConfigured = true;
        pstCtrl->stStatus.bIsRunning = false;
        pstCtrl->stStatus.uiFrequency_Hz = uiFrequency_Hz;
    }
    k_mutex_unlock(&kTrigSrcMutex);
    return bResult;
}

bool bTrigSrc_UpdateCTimerFrequency(eTrigSrc_CTimer_t eSource,
                                    eTrigSrcConsumer_t eConsumer,
                                    uint32_t uiFrequency_Hz)
{
    if(!bIsValidRequest(eSource, eConsumer))
        return false;

    k_mutex_lock(&kTrigSrcMutex, K_FOREVER);
    sT_CTimerCtrl_t *pstCtrl = &staCTimerCtrl[eSource];
    if(!bIsOwner(pstCtrl, eConsumer) ||
       (pstCtrl->stStatus.eShareMode != eTrigShareMode_Exclusive))
    {
        k_mutex_unlock(&kTrigSrcMutex);
        return false;
    }

    bool bWasRunning = pstCtrl->stStatus.bIsRunning;
    bool bResult = bConfigureMatch(eSource, uiFrequency_Hz);
    if(bResult)
    {
        pstCtrl->stStatus.bIsConfigured = true;
        pstCtrl->stStatus.uiFrequency_Hz = uiFrequency_Hz;
        if(bWasRunning)
            CTIMER_StartTimer(staCTimerHWMap[eSource].pstBase);
        pstCtrl->stStatus.bIsRunning = bWasRunning;
    }
    k_mutex_unlock(&kTrigSrcMutex);
    return bResult;
}

bool bTrigSrc_StartCTimer(eTrigSrc_CTimer_t eSource, eTrigSrcConsumer_t eConsumer)
{
    if(!bIsValidRequest(eSource, eConsumer))
        return false;

    k_mutex_lock(&kTrigSrcMutex, K_FOREVER);
    sT_CTimerCtrl_t *pstCtrl = &staCTimerCtrl[eSource];
    if(!bIsOwner(pstCtrl, eConsumer) || !pstCtrl->stStatus.bIsConfigured)
    {
        k_mutex_unlock(&kTrigSrcMutex);
        return false;
    }

    CTIMER_StartTimer(staCTimerHWMap[eSource].pstBase);
    pstCtrl->stStatus.bIsRunning = true;
    k_mutex_unlock(&kTrigSrcMutex);
    return true;
}

bool bTrigSrc_StopCTimer(eTrigSrc_CTimer_t eSource, eTrigSrcConsumer_t eConsumer)
{
    if(!bIsValidRequest(eSource, eConsumer))
        return false;

    k_mutex_lock(&kTrigSrcMutex, K_FOREVER);
    sT_CTimerCtrl_t *pstCtrl = &staCTimerCtrl[eSource];
    if(!bIsOwner(pstCtrl, eConsumer))
    {
        k_mutex_unlock(&kTrigSrcMutex);
        return false;
    }

    uint32_t uiOtherConsumers = pstCtrl->stStatus.uiConsumerMask & ~(1UL << eConsumer);
    if((pstCtrl->stStatus.eShareMode == eTrigShareMode_SharedFixed) &&
       (uiOtherConsumers != 0U))
    {
        k_mutex_unlock(&kTrigSrcMutex);
        return false;
    }

    CTIMER_StopTimer(staCTimerHWMap[eSource].pstBase);
    pstCtrl->stStatus.bIsRunning = false;
    k_mutex_unlock(&kTrigSrcMutex);
    return true;
}

bool bTrigSrc_GetCTimerStatus(eTrigSrc_CTimer_t eSource, sT_TrigSrcStatus_t *pstStatus)
{
    if((eSource >= eNUMBER_OF_CTIMER_TRIG_SRCs) || (pstStatus == NULL))
        return false;

    k_mutex_lock(&kTrigSrcMutex, K_FOREVER);
    *pstStatus = staCTimerCtrl[eSource].stStatus;
    k_mutex_unlock(&kTrigSrcMutex);
    return true;
}

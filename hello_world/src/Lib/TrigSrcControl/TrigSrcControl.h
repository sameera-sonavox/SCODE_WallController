#ifndef TRIGSRCCONTROL_H
#define TRIGSRCCONTROL_H

#include "TrigSrcControl_Types.h"

bool bTrigSrc_AcquireCTimer(eTrigSrc_CTimer_t eSource,
                            eTrigSrcConsumer_t eConsumer,
                            eTrigSrcShareMode_t eShareMode);
void vTrigSrc_ReleaseCTimer(eTrigSrc_CTimer_t eSource, eTrigSrcConsumer_t eConsumer);

bool bTrigSrc_ConfigureCTimer(eTrigSrc_CTimer_t eSource,
                              eTrigSrcConsumer_t eConsumer,
                              uint32_t uiFrequency_Hz);
bool bTrigSrc_UpdateCTimerFrequency(eTrigSrc_CTimer_t eSource,
                                    eTrigSrcConsumer_t eConsumer,
                                    uint32_t uiFrequency_Hz);
bool bTrigSrc_StartCTimer(eTrigSrc_CTimer_t eSource, eTrigSrcConsumer_t eConsumer);
bool bTrigSrc_StopCTimer(eTrigSrc_CTimer_t eSource, eTrigSrcConsumer_t eConsumer);
bool bTrigSrc_GetCTimerStatus(eTrigSrc_CTimer_t eSource, sT_TrigSrcStatus_t *pstStatus);

#endif

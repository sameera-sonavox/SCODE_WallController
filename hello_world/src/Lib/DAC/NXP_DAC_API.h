#ifndef NXP_DAC_API_H
#define NXP_DAC_API_H

#include "../API_Usage_Definition.h"

#if defined(USE_DAC)

#include "NXP_DAC_Types.h"
#include "DAC_ProjDef.h"

#define HIGHER_LOWER_POWER_MODE_SETTLING_TIME_US        (3U) /* Placeholder value. Adjust based on actual hardware characteristics and requirements. */
#define LOWER_LOWER_POWER_MODE_SETTLING_TIME_US         (6U)
#define fSETTLING_TIME_MARGIN_PERCENTAGE                (0.15f) /* Percentage margin to be added to the calculated settling time to ensure reliable DAC output. Adjust as needed based on actual hardware characteristics and requirements. */
#define DAC_MIN_WAVEFORM_SAMPLES_PER_PERIOD             (16U)


extern void vDAC_Init(sT_DAC_Config_t *pstConfig);
extern void vDAC_EnableWaveGen( void );
extern void vDAC_DisableWaveGen( eDAC_DefaultOutLevel_t eDefaultLevel, uint32_t uiCustomVal_mV );
extern void vStop_WaveGen( eDAC_DefaultOutLevel_t eDefaultLevel, uint32_t uiCustomVal_mV );
extern void vReStart_WaveGen( void );
extern void vPause_WaveGen( void );
extern void vResume_WaveGen( void );
extern void vNotify_DMANoiseBuffer_Completed( eT_TCDBuff_t eCompletedBuff );
extern eT_TCDBuff_t eGet_Ready_TCDBufferId(void);
extern uint32_t *puiGet_TCDBuffer(eT_TCDBuff_t eBuffId);
extern bool bTry_Claim_TCDBuffer_ForDMA(eT_TCDBuff_t eBuffId);
extern void vMark_TCDBuffer_Queued(eT_TCDBuff_t eBuffId);

extern bool bDAC_UpdateOutputValue(uint16_t uiOutput_mV);
extern void vUpdate_WaveForm_Volume(uint16_t uiPeakVolt_mV);
extern void vUpdate_WaveForm_Frequency(uint32_t uiFreq_Hz);

#endif

#endif /* NXP_DAC_API_H */

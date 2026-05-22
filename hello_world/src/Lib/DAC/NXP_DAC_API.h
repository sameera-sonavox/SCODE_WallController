#ifndef NXP_DAC_API_H
#define NXP_DAC_API_H

#include "NXP_DAC_Types.h"
#include "DAC_ProjDef.h"

#define HIGHER_LOWER_POWER_MODE_SETTLING_TIME_US        (3U) /* Placeholder value. Adjust based on actual hardware characteristics and requirements. */
#define LOWER_LOWER_POWER_MODE_SETTLING_TIME_US         (6U)
#define fSETTLING_TIME_MARGIN_PERCENTAGE                (0.15f) /* Percentage margin to be added to the calculated settling time to ensure reliable DAC output. Adjust as needed based on actual hardware characteristics and requirements. */
#define DAC_MIN_WAVEFORM_SAMPLES_PER_PERIOD             (16U)


extern void vDAC_Init(sT_DAC_Config_t *pstConfig);
extern void vDAC_Enable( void );
extern void vDAC_Disable( void );
extern bool bDAC_UpdateOutputValue(uint16_t uiOutput_mV);
extern void vUpdate_WaveForm_Volume(uint16_t uiPeakVolt_mV);

#endif /* NXP_DAC_API_H */

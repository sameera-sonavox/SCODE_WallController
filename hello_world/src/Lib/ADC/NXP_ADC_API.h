#ifndef NXP_ADC_API_H
#define NXP_ADC_API_H

#include "NXP_ADC_Types.h"
#include "NXP_ADC_LinkedList.h"

#define ADC_MAX_WATERMARK_LEVEL                 8
#define ADC_MAX_LOOP_COUNT                      15
#define ADC_MAX_TRIG_DELAY_ADC_CLK_CYCLEs       16
#define ADC_TRIG_COMPLETE_MASK                  0x000F0000
#define ADC_TRIG_EXCEPTION_MASK                 0x0000000F
#define ADC_IDLE_TIMEOUT_MS                     2

extern void vInit_ADC(sT_ADC_ModuleConfig_t *pstADCModuleConfig);//Once this is called, all the pointers are handled by the API. It means memory release will be done by the API
                                                                 //Application should not engage with memory or reference any command config after the initialization.
extern void vDeInit_ADC(eADC_Module_t eADCModule);
extern bool bSet_ADCSW_Trig(eADC_Module_t eADCModule, eADC_TrigSlot_t eTrigSlot);
extern bool bGet_ADCValue(eADC_Module_t eModule,
                          eADC_Channel_t eChannel,
                          uint16_t *puiValue,
                          eADC_ValueType_t eValType);
extern void vClear_ADCStatisticsOverflow( void );
extern bool bIs_ADCStatisticsOverflowed( void );
extern const sT_ADC_CommandConfig_t *pstGetCommandData(eADC_Module_t eADCModule, eADC_Channel_t eChannel);

#endif

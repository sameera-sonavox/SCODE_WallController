#ifndef NXP_ADC_API_H
#define NXP_ADC_API_H

#include "NXP_ADC_Types.h"
#include "NXP_ADC_LinkedList.h"

#define ADC_MAX_ADCLK_FREQ_Hz                   96000000U
#define ADC_MAX_POWER_SENSITIVE_FREQ_Hz         24000000U
#define ADC_MAX_COV_RATE_12bit_S_s              4000000U
#define ADC_MAX_COV_RATE_16bit_S_s              3200000U

#define ADC_MIN_ADCLK_FREQ_Hz                   6000000U
#define ADC_MAX_ADCLK_FREQ_AT_LOW_PW_MODE       24000000U
#define ADC_MAX_ADCLK_FREQ_AT_HIGH_PW_MODE      64000000U

#define ADC_MAX_WATERMARK_LEVEL                 8U
#define ADC_MAX_LOOP_COUNT                      15U
#define ADC_MAX_TRIG_DELAY_ADC_CLK_CYCLEs       16U
#define ADC_TRIG_COMPLETE_MASK                  0x000F0000U
#define ADC_TRIG_EXCEPTION_MASK                 0x0000000FU
#define ADC_IDLE_TIMEOUT_MS                     2U

#define ADC_MAX_VALUE_12b_RESOLUTION            4095U
#define ADC_MAX_VALUE_16b_RESOLUTION            65535U

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

extern ADC_Type *pstGetHWADCModule(eADC_Module_t eADCModule);
extern void vEnable_ADC_TrigCompletionInterrupts(eADC_Module_t eADCModule);
extern sT_ADCToDMA_HW_Map_t stGetSWADCModule(eADC_Module_t eADCModule);
extern void vRequest_ADC_To_DisableInterrupts(eADC_Module_t eADCModule);
extern void vNotify_ADC_DMAError(eADC_Module_t eADCModule);

#endif

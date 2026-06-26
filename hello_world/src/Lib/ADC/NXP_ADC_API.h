#ifndef NXP_ADC_API_H
#define NXP_ADC_API_H

#include "../API_Usage_Definition.h"

#if defined(USE_ADC)

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

/**
* @brief Initializes and configures an ADC module. 
* This function has to be used each time when a different ADC module has to be initialized.
* This performs the configuration of specified ADC module, command buffers, trigger sources (only at ADC peripheral level), INPUTMUX routing and
* Trigger Source reservations.

* @note 
* 1. If 'eTrigSrcType' is HW triggered, then the particular hardware trigger source must be configured and started externally.
*    Since this init function only initialize and configures which is local to the specified ADC peripheral.
*    You have to include 'TrigSrcControl.h' in your project and assign the trigger sources for independent trigger slots.
*    Always use 'TrigSrcControl.h' for assignment, since it provides exclusive ownership and state management for the trigger sources to avoid 
*    same trigger source is used by multiple peripherals simultaneously.

* 2. All the dynamic memory allocations executed using the singly linked lists when the module is configured, are automatically handled and managed
*    by the ADC API itself. The user has nothing to worry about the memory leaks or freeing allocated memory.

* @param pstADCModuleConfig ADC Module Configuration
 */
extern void vInit_ADC(sT_ADC_ModuleConfig_t *pstADCModuleConfig);

/**
 * @brief De-Init the specified ADC module. This will release all dynamically allocated memory regions.
 * @param eADCModule The ADC module that needs to be de-initialized
 */
extern void vDeInit_ADC(eADC_Module_t eADCModule);

/**
 * @brief Set the SW triggering for a particular trigger slot
 * @param eADCModule ADC module
 * @param eTrigSlot Slot Index
 */
extern bool bSet_ADCSW_Trig(eADC_Module_t eADCModule, eADC_TrigSlot_t eTrigSlot);

extern bool bGet_ADCValue(eADC_Module_t eModule,
                          eADC_Channel_t eChannel,
                          uint16_t *puiValue,
                          eADC_ValueType_t eValType);

extern void vClear_ADCStatisticsOverflow( void );
extern bool bIs_ADCStatisticsOverflowed( void );
extern const sT_ADC_CommandConfig_t *pstGetCommandData(eADC_Module_t eADCModule, eADC_Channel_t eChannel);

extern ADC_Type *pstGetHWADCModule(eADC_Module_t eADCModule);
extern sT_ADCToDMA_HW_Map_t stGetSWADCModule(eADC_Module_t eADCModule);
extern void vRequest_ADC_To_DisableInterrupts(eADC_Module_t eADCModule);
extern void vNotify_ADC_DMAError(eADC_Module_t eADCModule);

#endif

#endif

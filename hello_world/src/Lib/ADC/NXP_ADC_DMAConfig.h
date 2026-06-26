#ifndef NXP_ADC_DMACONFIG_C
#define NXP_ADC_DMACONFIG_C


#include "../API_Usage_Definition.h"

#if defined(USE_ADC)

#include "fsl_lpadc.h"
#include "NXP_ADC_Types.h"

#define ADC_DMA_BLOCK_COUNT                 2U
#define ADC_DMA_MSG_QUEUE_SIZE              64U//64U
#define ADC_DMA_THREAD_STACK_SIZE           1024U
#define ADC_DMA_MAX_ERROR_COUNT             2U

extern bool bADC_API_DMAInit(eADC_Module_t eADCModule);
extern bool bRequest_To_StopDMA(eADC_Module_t eADCModule);
extern void vUpdate_ADCResult_FromDMA(eADC_Module_t eADCModule, lpadc_conv_result_t stConvResult);

#endif

#endif

#ifndef NXP_ADC_LINKEDLIST_H
#define NXP_ADC_LINKEDLIST_H

#include "../API_Usage_Definition.h"

#if defined(USE_ADC)

#include "NXP_ADC_Types.h"

extern sT_ADC_CommandConfig_t* pstCreate_ADCCommandConfigNode(sT_ADC_CMDData_t *pstCMDData);
extern bool bInsertCommand_AtBeginning(sT_ADC_CommandConfig_t **ppstHead, sT_ADC_CMDData_t *pstCMDData);
extern bool bInsertCommand_AtEnd(sT_ADC_CommandConfig_t **ppstHead, sT_ADC_CMDData_t *pstCMDData);
extern void vRelease_CMDBuffers(sT_ADC_CommandConfig_t **ppstHead);
extern sT_ADC_CommandConfig_t *pstGetCommandConfig(eADC_Command_t eCommandId, sT_ADC_CommandConfig_t *pstHead);

#endif
#endif /* NXP_ADC_LINKEDLIST_H */
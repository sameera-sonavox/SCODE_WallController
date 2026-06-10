#ifndef NXP_ADC_CHMAP_H
#define NXP_ADC_CHMAP_H

#include "fsl_inputmux.h"
#include "NXP_ADC_Types.h"

extern sT_ADC_ChannelMap_t staADC_ChannelMap[eNUMBER_OF_ADC_MODULEs][eNUMBER_OF_ADC_CHANNELs];
extern const inputmux_connection_t eaADCInputMuxConnection[eNUMBER_OF_ADC_MODULEs][eNUMBER_OF_ADC_TRIG_SOURCEs];

extern const sT_ADC_ChannelMap_t *pstGetADCChannelMapROnly(eADC_Module_t eModule, eADC_Channel_t eChannel);
extern sT_ADC_ChannelMap_t *pstGetADCChannelData(eADC_Module_t eModule, eADC_Channel_t eChannel);
extern bool bIsADC_ChannelUsed(eADC_Module_t eModule, eADC_Channel_t eChannel);
extern inputmux_connection_t eGetInputMuxConnection(eADC_Module_t eModule, eADC_TrigSource_t eSrc);
extern bool bValidate_CMD_ChChainingWithLoop(eADC_Module_t eModule, eADC_Channel_t eChannel, uint8_t uiLoopCount);
extern void vMark_ADC_CH_InUse(eADC_Module_t eModule, eADC_Channel_t eChannel);
extern void vRemove_ADC_CH_FromUse(eADC_Module_t eModule, eADC_Channel_t eChannel);
extern void vRelease_ADCChannelConfig(eADC_Module_t eModule);
extern bool bUpdateADCChannelCommandMap(eADC_Module_t eModule,
                                        eADC_Channel_t eChannel,
                                        uint8_t uiLoopCount,
                                        bool bIsLoopWithChIncrementEnabled,
                                        eADC_TrigSlot_t eTrigSlot,
                                        eADC_Command_t eCommandId,
                                        uint32_t uiMaxRelTime_ms,
                                        uint32_t uiMinRelTime_ms,
                                        uint16_t uiSWAvgSampleCount);
extern sT_ADC_ChannelMap_t *pstGetChInfo_ByCmdId_TrigSlot(eADC_Module_t eModule, eADC_TrigSlot_t eTrigSlot, eADC_Command_t eCommandId, uint8_t uiLoopCount);
extern bool bUpdate_ReleaseTime_OnChStats(sT_ADC_ChRelTimeUpdate_t *pstRelTimeUpdate);

#endif // NXP_ADC_CHMAP_H

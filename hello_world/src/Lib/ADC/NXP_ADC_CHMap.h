#ifndef NXP_ADC_CHMAP_H
#define NXP_ADC_CHMAP_H

#include "../API_Usage_Definition.h"

#if defined(USE_ADC)

#include "fsl_inputmux.h"
#include "NXP_ADC_Types.h"

typedef struct
{
    eADC_Module_t eADCModule;
    eADC_Channel_t eADCChannel;
    eADC_Command_t eCMDId;
    eADC_TrigSlot_t eTrigSlot;
    uint32_t uiMaxReleaseTime_ms;
    uint32_t uiMaxReleaseStepSize;
    uint32_t uiMinReleaseTime_ms;
    uint32_t uiMinReleaseStepSize;
    uint16_t uiSWAvgSampleCount;     
    uint8_t uiLoopCount;
    bool bIsLWIEn;
} sT_ChCMDConfig_Data_t;

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
extern bool bUpdateADCChannelCommandMap( sT_ChCMDConfig_Data_t *pstChCMDConfig );
extern sT_ADC_ChannelMap_t *pstGetChInfo_ByCmdId_TrigSlot(eADC_Module_t eModule, eADC_TrigSlot_t eTrigSlot, eADC_Command_t eCommandId, uint8_t uiLoopCount);
extern bool bUpdate_ReleaseTime_OnChStats(sT_ADC_ChRelTimeUpdate_t *pstRelTimeUpdate);

#endif

#endif // NXP_ADC_CHMAP_H

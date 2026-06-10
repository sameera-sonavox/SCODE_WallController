#include <zephyr/kernel.h>
#include <zephyr/device.h>

#include "ADC_Controller.h"
#include "../Lib/ADC/NXP_ADC_API.h"
#include "../Lib/ADC/NXP_ADC_ProjDef.h"
#include "../Lib/GenericMacro.h"

_Atomic bool bADCOverflow = false;

struct k_work_delayable kADC_SW_TrigSlot_0;
static void vTrigger_ADC_Slot_0( struct k_work *work );

void vADC_0_TrigCompleteCallback(eADC_Module_t eADCmodule, uint32_t uiTrigMask, void *pvUserdata);

void vInitialize_ADCModule( void )
{
    sT_ADC_ModuleConfig_t staADCModuleConfigs[eNUMBER_OF_ADC_MODULEs] = {0};
    sT_ADC_CMDData_t stTCMDConfig = {0};

    sT_ADC_ModuleConfig_t *pstADCModule_0 = &staADCModuleConfigs[eADC_ADC0];
    pstADCModule_0->eADCModule = eADC_ADC0;
    pstADCModule_0->eRefSrc = eADC_VrefSrc_VDD_ANA;
    pstADCModule_0->pbOverflowFlag = &bADCOverflow;
    pstADCModule_0->pvTrigCompltCallbackFn = vADC_0_TrigCompleteCallback;
    pstADCModule_0->stTNotifyCtrl.eNotificationType = eNotification_Interrupt;
    pstADCModule_0->stTNotifyCtrl.ADCNotify_t.stTInterruptCtrl.uiIntrPriority = ADC_IRQ_PRIORITY;
    pstADCModule_0->uiWaterMarkLevel = 1;

    sT_ADC_TrigConfig_t *pstTrigSrc = &pstADCModule_0->staTrigConfig[eTrig_Slot_0];
    pstTrigSrc->bIsTrigSlotEnabled = true;
    pstTrigSrc->bEnTrigCompletionNotifyReq = true;
    pstTrigSrc->eTrigSlot = eTrig_Slot_0;
    pstTrigSrc->eTrigSrcType = eTrigSrc_Software;
    pstTrigSrc->eTrigSrc = eADC_TrigSrc_None;
    pstTrigSrc->uiTrigDelay = 3;
    pstTrigSrc->ePrioLevel = eTrig_Prio_Lev_0;

    stTCMDConfig.eChannel = eADC_Ch_0;
    stTCMDConfig.eCommandId = eADC_CMD_1;
    stTCMDConfig.bIsLoopWithChIncrementEnabled = false;
    stTCMDConfig.bIsNewTrig_Req_For_NextConv = false;
    stTCMDConfig.eCompareValueReg = eADC_CVReg_None;
    stTCMDConfig.eHWAvgSampleCount = eADC_AVG_ConvCount_2;
    stTCMDConfig.eResolution = eADC_Resolution_16Bit;
    stTCMDConfig.eSampleTime = eADC_SampleTime_3_ADCKCycles;
    stTCMDConfig.uiADCMax_ReleaseTime_ms = 500;
    stTCMDConfig.uiADCMin_ReleaseTime_ms = 0;
    stTCMDConfig.uiLoopCount = 0;
    stTCMDConfig.uiSWAvgSampleCount = 10;
    pstTrigSrc->pstTHeadCmdConfig = pstCreate_ADCCommandConfigNode(&stTCMDConfig);


    stTCMDConfig.eChannel = eADC_Ch_1;
    stTCMDConfig.eCommandId = eADC_CMD_2;
    stTCMDConfig.bIsLoopWithChIncrementEnabled = false;
    stTCMDConfig.bIsNewTrig_Req_For_NextConv = false;
    stTCMDConfig.eCompareValueReg = eADC_CVReg_None;
    stTCMDConfig.eHWAvgSampleCount = eADC_AVG_ConvCount_2;
    stTCMDConfig.eResolution = eADC_Resolution_16Bit;
    stTCMDConfig.eSampleTime = eADC_SampleTime_3_ADCKCycles;
    stTCMDConfig.uiADCMax_ReleaseTime_ms = 500;
    stTCMDConfig.uiADCMin_ReleaseTime_ms = 0;
    stTCMDConfig.uiLoopCount = 0;
    stTCMDConfig.uiSWAvgSampleCount = 10;
    if(!bInsertCommand_AtEnd(&pstTrigSrc->pstTHeadCmdConfig, &stTCMDConfig))
    {
        FHALT("Cannot create command");
        vRelease_CMDBuffers(&pstTrigSrc->pstTHeadCmdConfig);
        return;
    }    

    vInit_ADC(&staADCModuleConfigs[eADC_ADC0]);
    if(!staADCModuleConfigs[eADC_ADC0].bIsConfigOk)
    {
        FHALT("ADC Init Fail");
        return;
    }

    sT_ADC_CommandConfig_t *pstCmd = pstGetCommandData(eADC_ADC0, eADC_Ch_1);
    if(pstCmd != NULL)
    {
        printf("Cmd : Id[%d], Ch[%d] @Res: %d\n\r", pstCmd->stTCMDData.eCommandId, pstCmd->stTCMDData.eChannel, pstCmd->stTCMDData.eResolution);
    }
    pstCmd->stTCMDData.eResolution = eADC_Resolution_12Bit;

    pstCmd = pstGetCommandData(eADC_ADC0, eADC_Ch_1);
    if(pstCmd != NULL)
    {
        printf("Cmd : Id[%d], Ch[%d] @Res: %d\n\r", pstCmd->stTCMDData.eCommandId, pstCmd->stTCMDData.eChannel, pstCmd->stTCMDData.eResolution);
    }
    else
    {
        printf("Error : CMD NULL\n\r");
    }
/*     k_work_init_delayable(&kADC_SW_TrigSlot_0, vTrigger_ADC_Slot_0);
    k_work_schedule(&kADC_SW_TrigSlot_0, K_USEC(100)); */
}

static void vTrigger_ADC_Slot_0( struct k_work *work )
{
    ARG_UNUSED(work);
    bool bRes = bSet_ADCSW_Trig(eADC_ADC0, eTrig_Slot_0);
    if(bRes)
    {
        k_work_schedule(&kADC_SW_TrigSlot_0, K_MSEC(10));
        return;
    }

    FHALT("SW Trigger Failed");
}

void vADC_0_TrigCompleteCallback(eADC_Module_t eADCmodule, uint32_t uiTrigMask, void *pvUserdata)
{
    uint16_t uiVal = 0;

/*     bGet_ADCValue(eADCmodule, eADC_Ch_0, &uiVal, eADC_Val);
    printf("ADC Val : %d\n\r", uiVal);
    bGet_ADCValue(eADCmodule, eADC_Ch_0, &uiVal, eADC_Avg);
    printf("ADC AVG : %d\n\r", uiVal);
    bGet_ADCValue(eADCmodule, eADC_Ch_0, &uiVal, eADC_RMS);
    printf("ADC RMS : %d\n\r", uiVal); */
    bGet_ADCValue(eADCmodule, eADC_Ch_0, &uiVal, eADC_Max);
    printf("ADC MAX : %d\n\r\n\r", uiVal);
}
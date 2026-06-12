#include <zephyr/kernel.h>
#include <zephyr/device.h>

#include "ADC_Controller.h"
#include "../Lib/ADC/NXP_ADC_API.h"
#include "../Lib/ADC/NXP_ADC_ProjDef.h"
#include "../Lib/TrigSrcControl/TrigSrcControl.h"
#include "../Lib/GenericMacro.h"

#define ADC0_TRIGGER_FREQUENCY_HZ 40000U

struct k_work_delayable k_ADCResult_RequestWorker;
static void vAcquire_ADCMeasurements( struct k_work *work );

_Atomic bool bADCOverflow = false;

static bool bSetup_TriggerSource( void );
void vADC_0_TrigCompleteCallback(eADC_Module_t eADCmodule, uint32_t uiTrigMask, void *pvUserdata);

void vInitialize_ADCModule( void )
{
    sT_ADC_ModuleConfig_t staADCModuleConfigs[eNUMBER_OF_ADC_MODULEs] = {0};
    sT_ADC_CMDData_t stTCMDConfig = {0};

    sT_ADC_ModuleConfig_t *pstADCModule_0 = &staADCModuleConfigs[eADC_ADC0];
    pstADCModule_0->eADCModule = eADC_ADC0;
    pstADCModule_0->eRefSrc = eADC_VrefSrc_VDD_ANA;
    pstADCModule_0->eADCClk_Src = eADC_SRC_CLK_12MHz;
    pstADCModule_0->eADCCLK_Div = eADCLK_DIV_1;
    pstADCModule_0->eADCPWlevel = eADC_PW_Lev_High;
    pstADCModule_0->pbOverflowFlag = &bADCOverflow;
    //pstADCModule_0->pvTrigCompltCallbackFn = vADC_0_TrigCompleteCallback;
    pstADCModule_0->stTNotifyCtrl.eNotificationType = eNotification_Interrupt;
    pstADCModule_0->stTNotifyCtrl.ADCNotify_t.stTInterruptCtrl.uiIntrPriority = ADC_IRQ_PRIORITY;
    pstADCModule_0->uiWaterMarkLevel = 3;

    sT_ADC_TrigConfig_t *pstTrigSrc = &pstADCModule_0->staTrigConfig[eTrig_Slot_0];
    pstTrigSrc->bIsTrigSlotEnabled = true;
    pstTrigSrc->bEnTrigCompletionNotifyReq = false;
    pstTrigSrc->eTrigSlot = eTrig_Slot_0;
    pstTrigSrc->stTADCTrigCtrl.eTrigSrcType = eADC_TrigSrcCtrl_Hardware;
    pstTrigSrc->stTADCTrigCtrl.eTrigSrc = eADC_TrigSrc_CTimer1_MAT0;
    pstTrigSrc->stTADCTrigCtrl.uiTrigFrequency_Hz = ADC0_TRIGGER_FREQUENCY_HZ;
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
    stTCMDConfig.uiADCMin_ReleaseTime_ms = 500;
    stTCMDConfig.uiLoopCount = 0;
    stTCMDConfig.uiSWAvgSampleCount = 10;
    pstTrigSrc->pstTHeadCmdConfig = pstCreate_ADCCommandConfigNode(&stTCMDConfig);


/*     stTCMDConfig.eChannel = eADC_Ch_1;
    stTCMDConfig.eCommandId = eADC_CMD_2;
    stTCMDConfig.bIsLoopWithChIncrementEnabled = false;
    stTCMDConfig.bIsNewTrig_Req_For_NextConv = false;
    stTCMDConfig.eCompareValueReg = eADC_CVReg_None;
    stTCMDConfig.eHWAvgSampleCount = eADC_AVG_ConvCount_2;
    stTCMDConfig.eResolution = eADC_Resolution_16Bit;
    stTCMDConfig.eSampleTime = eADC_SampleTime_3_ADCKCycles;
    stTCMDConfig.uiADCMax_ReleaseTime_ms = 50;
    stTCMDConfig.uiADCMin_ReleaseTime_ms = 20;
    stTCMDConfig.uiLoopCount = 0;
    stTCMDConfig.uiSWAvgSampleCount = 5;
    if(!bInsertCommand_AtEnd(&pstTrigSrc->pstTHeadCmdConfig, &stTCMDConfig))
    {
        FHALT("Cannot create command");
        vRelease_CMDBuffers(&pstTrigSrc->pstTHeadCmdConfig);
        return;
    } */    

    vInit_ADC(&staADCModuleConfigs[eADC_ADC0]);
    if(!staADCModuleConfigs[eADC_ADC0].bIsConfigOk)
    {
        FHALT("ADC Init Fail");
        return;
    }

    if(!bSetup_TriggerSource())
    {
        FHALT("ADC trigger source setup failed");
        vDeInit_ADC(eADC_ADC0);
        return;
    }

    k_work_init_delayable(&k_ADCResult_RequestWorker, vAcquire_ADCMeasurements);    
    k_work_schedule(&k_ADCResult_RequestWorker, K_MSEC(100));
}

void vAcquire_ADCMeasurements( struct k_work *work )
{
    uint16_t uiValue_Max = 0U, uiValue_Min = 0U;
    uint16_t uiRawCh0 = 0U, uiRawCh1 = 0U;
    ARG_UNUSED(work);

    if(bIs_ADCStatisticsOverflowed())
    {
        (void)bGet_ADCValue(eADC_ADC0, eADC_Ch_0, &uiRawCh0, eADC_Val);
        (void)bGet_ADCValue(eADC_ADC0, eADC_Ch_1, &uiRawCh1, eADC_Val);
        printk("ADC statistics overflow: worker cannot keep up. Raw Ch0=%u, Ch1=%u\n\r",
               uiRawCh0,
               uiRawCh1);
        vClear_ADCStatisticsOverflow();
        k_work_reschedule(&k_ADCResult_RequestWorker, K_MSEC(100));
        return;
    }

    bool bres = bGet_ADCValue(eADC_ADC0, eADC_Ch_0, &uiValue_Max, eADC_Max);
    if(!bres)
    {
        k_work_reschedule(&k_ADCResult_RequestWorker, K_MSEC(100));
        return;

    }
    bres = bGet_ADCValue(eADC_ADC0, eADC_Ch_0, &uiValue_Min, eADC_Min);
    if(!bres)
    {
        k_work_reschedule(&k_ADCResult_RequestWorker, K_MSEC(100));
        return;

    }
/*     bres = bGet_ADCValue(eADC_ADC0, eADC_Ch_1, &uiRawCh1, eADC_Val);
    if(!bres)
    {
        k_work_reschedule(&k_ADCResult_RequestWorker, K_MSEC(100));
        return;

    } */

    printf("ADC Ch0 (Max : %d, Min : %d)  Ch1: %d\n\r", uiValue_Max, uiValue_Min, uiRawCh1);
    k_work_reschedule(&k_ADCResult_RequestWorker, K_MSEC(100));
}

static bool bSetup_TriggerSource( void )
{
    if(!bTrigSrc_ConfigureCTimer(eTrigSrc_CTIMER1_MAT0,
                                  eTrigConsumer_ADC0_Slot0,
                                  ADC0_TRIGGER_FREQUENCY_HZ))
    {
        return false;
    }

    return bTrigSrc_StartCTimer(eTrigSrc_CTIMER1_MAT0,
                                eTrigConsumer_ADC0_Slot0);
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

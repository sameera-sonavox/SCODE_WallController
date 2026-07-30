#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <string.h>

#if defined(USE_ADC)

#include "ADC_Controller.h"
#include "ADC/NXP_ADC_API.h"
#include "ADC/NXP_ADC_ProjDef.h"
#include "TrigSrcControl/TrigSrcControl.h"
#include "GenericMacro.h"
#include "../UART_CAN_Bridge/UART_CAN_Bridge.h"

#define ADC0_TRIGGER_FREQUENCY_HZ               40000U
#define ADC_PC_STARTUP_SILENT_TIME_MS           300U
#define ADC_PC_MEASUREMENT_COUNT                4U
#define ADC_PC_DESCRIPTOR_SIZE                  4U
#define ADC_PC_CONFIG_HEADER_SIZE               5U

typedef struct ADC_Controller
{
    eADC_Module_t eADCModule;
    eADC_Channel_t eADCCh;
    eADC_ResolutionType_t eADCResolution;
    eADC_ValueType_t eValType;
} sT_ADCCHInfo_t;

static sT_ADCCHInfo_t staADCChInfo[ADC_PC_MEASUREMENT_COUNT];
static const uint8_t uiNumberofMeasers = ARRAY_SIZE(staADCChInfo);
static _Atomic bool bSendingChannelInfo;

struct k_work_delayable k_ADCResult_RequestWorker;
static void vAcquire_ADCMeasurements( struct k_work *work );

_Atomic bool bADCOverflow = false;

static bool bSetup_TriggerSource( void );
void vADC_0_TrigCompleteCallback(eADC_Module_t eADCmodule, uint32_t uiTrigMask, void *pvUserdata);
void vSetup_ChannelInfo( void );

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
    pstADCModule_0->stTNotifyCtrl.eNotificationType = eNotification_DMA;
    //pstADCModule_0->stTNotifyCtrl.ADCNotify_t.stTInterruptCtrl.uiIntrPriority = ADC_IRQ_PRIORITY;
    pstADCModule_0->uiWaterMarkLevel = 3;

    sT_ADC_TrigConfig_t *pstTrigSrc = &pstADCModule_0->staTrigConfig[eTrig_Slot_0];
    pstTrigSrc->bIsTrigSlotEnabled = true;
    pstTrigSrc->bEnTrigCompletionNotifyReq = false;
    pstTrigSrc->eTrigSlot = eTrig_Slot_0;
    pstTrigSrc->stTADCTrigCtrl.eTrigSrcType = eADC_TrigSrcCtrl_Hardware;
    pstTrigSrc->stTADCTrigCtrl.eTrigSrc = eADC_TrigSrc_CTimer1_MAT0;
    pstTrigSrc->stTADCTrigCtrl.uiTrigFrequency_Hz = ADC0_TRIGGER_FREQUENCY_HZ;
    pstTrigSrc->stTADCTrigCtrl.uiStatisticCompute_Freq_Hz = (uint32_t)((float)pstTrigSrc->stTADCTrigCtrl.uiTrigFrequency_Hz * 0.2f);
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
    stTCMDConfig.uiMax_ReleaseStepSize = 50;
    stTCMDConfig.uiADCMin_ReleaseTime_ms = 500;
    stTCMDConfig.uiMin_ReleaseStepSize = 50;
    stTCMDConfig.uiLoopCount = 0;
    stTCMDConfig.uiSWAvgSampleCount = 10;
    pstTrigSrc->pstTHeadCmdConfig = pstCreate_ADCCommandConfigNode(&stTCMDConfig);

    stTCMDConfig.eChannel = eADC_Ch_1;
    stTCMDConfig.eCommandId = eADC_CMD_2;
    stTCMDConfig.bIsLoopWithChIncrementEnabled = false;
    stTCMDConfig.bIsNewTrig_Req_For_NextConv = false;
    stTCMDConfig.eCompareValueReg = eADC_CVReg_None;
    stTCMDConfig.eHWAvgSampleCount = eADC_AVG_ConvCount_2;
    stTCMDConfig.eResolution = eADC_Resolution_12Bit;
    stTCMDConfig.eSampleTime = eADC_SampleTime_3_ADCKCycles;
    stTCMDConfig.uiADCMax_ReleaseTime_ms = 50;
    stTCMDConfig.uiMax_ReleaseStepSize = 50;
    stTCMDConfig.uiADCMin_ReleaseTime_ms = 20;
    stTCMDConfig.uiMin_ReleaseStepSize = 50;
    stTCMDConfig.uiLoopCount = 0;
    stTCMDConfig.uiSWAvgSampleCount = 5;
    if(!bInsertCommand_AtEnd(&pstTrigSrc->pstTHeadCmdConfig, &stTCMDConfig))
    {
        FHALT("Cannot create command");
        vRelease_CMDBuffers(&pstTrigSrc->pstTHeadCmdConfig);
        return;
    }    

    sT_ADC_TrigConfig_t *pstTrigSrc1 = &pstADCModule_0->staTrigConfig[eTrig_Slot_1];
    pstTrigSrc1->bIsTrigSlotEnabled = true;
    pstTrigSrc1->bEnTrigCompletionNotifyReq = false;
    pstTrigSrc1->eTrigSlot = eTrig_Slot_1;
    pstTrigSrc1->stTADCTrigCtrl.eTrigSrcType = eADC_TrigSrcCtrl_Hardware;
    pstTrigSrc1->stTADCTrigCtrl.eTrigSrc = eADC_TrigSrc_CTimer2_MAT0;
    pstTrigSrc1->stTADCTrigCtrl.uiTrigFrequency_Hz = ADC0_TRIGGER_FREQUENCY_HZ;
    pstTrigSrc1->stTADCTrigCtrl.uiStatisticCompute_Freq_Hz = (uint32_t)((float)pstTrigSrc1->stTADCTrigCtrl.uiTrigFrequency_Hz * 0.2f);
    pstTrigSrc1->uiTrigDelay = 3;
    pstTrigSrc1->ePrioLevel = eTrig_Prio_Lev_1;

    stTCMDConfig.eChannel = eADC_Ch_6;
    stTCMDConfig.eCommandId = eADC_CMD_3;
    stTCMDConfig.bIsLoopWithChIncrementEnabled = false;
    stTCMDConfig.bIsNewTrig_Req_For_NextConv = false;
    stTCMDConfig.eCompareValueReg = eADC_CVReg_None;
    stTCMDConfig.eHWAvgSampleCount = eADC_AVG_ConvCount_2;
    stTCMDConfig.eResolution = eADC_Resolution_16Bit;
    stTCMDConfig.eSampleTime = eADC_SampleTime_3_ADCKCycles;
    stTCMDConfig.uiADCMax_ReleaseTime_ms = 500;
    stTCMDConfig.uiMax_ReleaseStepSize = 50;
    stTCMDConfig.uiADCMin_ReleaseTime_ms = 20;
    stTCMDConfig.uiMin_ReleaseStepSize = 50;
    stTCMDConfig.uiLoopCount = 0;
    stTCMDConfig.uiSWAvgSampleCount = 10;    
    pstTrigSrc1->pstTHeadCmdConfig = pstCreate_ADCCommandConfigNode(&stTCMDConfig);

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

    vUART_CAN_Bridge_RegisterADCDataRequestCallback(vSetup_ChannelInfo);
    vSetup_ChannelInfo();

    k_work_init_delayable(&k_ADCResult_RequestWorker, vAcquire_ADCMeasurements);    
    k_work_schedule(&k_ADCResult_RequestWorker, K_MSEC(100));
}

void vSetup_ChannelInfo( void )
{
    static const sT_ADCCHInfo_t staConfiguredMeasurements[] = {
        {eADC_ADC0, eADC_Ch_0, eADC_Resolution_16Bit, eADC_Max},
        {eADC_ADC0, eADC_Ch_0, eADC_Resolution_16Bit, eADC_Min},
        {eADC_ADC0, eADC_Ch_1, eADC_Resolution_12Bit, eADC_Avg},
        {eADC_ADC0, eADC_Ch_6, eADC_Resolution_16Bit, eADC_Max},
    };

    atomic_store_explicit(&bSendingChannelInfo, true, memory_order_release);
    memcpy(staADCChInfo, staConfiguredMeasurements, sizeof(staADCChInfo));

    uint8_t uiaDescriptors[ADC_PC_CONFIG_HEADER_SIZE + (ADC_PC_MEASUREMENT_COUNT * ADC_PC_DESCRIPTOR_SIZE)] = {
        'A', 'D', 'C', 'F', ADC_PC_MEASUREMENT_COUNT
    };
    for(uint8_t i = 0U; i < uiNumberofMeasers; i++)
    {
        const uint8_t uiOffset = ADC_PC_CONFIG_HEADER_SIZE + (i * ADC_PC_DESCRIPTOR_SIZE);
        uiaDescriptors[uiOffset] = (uint8_t)staADCChInfo[i].eADCModule;
        uiaDescriptors[uiOffset + 1U] = (uint8_t)staADCChInfo[i].eADCCh;
        uiaDescriptors[uiOffset + 2U] = (uint8_t)staADCChInfo[i].eADCResolution;
        uiaDescriptors[uiOffset + 3U] = (uint8_t)staADCChInfo[i].eValType;
    }

    (void)bUART_CAN_Bridge_SendDataWithPostDelay(uiaDescriptors,
                                                 sizeof(uiaDescriptors),
                                                 ADC_PC_STARTUP_SILENT_TIME_MS);
    atomic_store_explicit(&bSendingChannelInfo, false, memory_order_release);
}

void vAcquire_ADCMeasurements( struct k_work *work )
{
    uint16_t uiValue_Max = 0U, uiValue_Min = 0U;
    uint16_t uiRawCh0 = 0U, uiRawCh1 = 0U, uiRawCh6 = 0U;
    ARG_UNUSED(work);

    if(atomic_load_explicit(&bSendingChannelInfo, memory_order_acquire))
    {
        k_work_reschedule(&k_ADCResult_RequestWorker, K_MSEC(100));
        return;
    }

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
    bres = bGet_ADCValue(eADC_ADC0, eADC_Ch_1, &uiRawCh1, eADC_Avg);
    if(!bres)
    {
        k_work_reschedule(&k_ADCResult_RequestWorker, K_MSEC(100));
        return;

    }
    bres = bGet_ADCValue(eADC_ADC0, eADC_Ch_6, &uiRawCh6, eADC_Max);
    if(!bres)
    {
        k_work_reschedule(&k_ADCResult_RequestWorker, K_MSEC(100));
        return;

    }

    const uint16_t uiaValues[] = {uiValue_Max, uiValue_Min, uiRawCh1, uiRawCh6};
    for(uint8_t i = 0U; i < uiNumberofMeasers; i++)
    {
        const uint8_t uiaMeasurement[6] = {
            (uint8_t)staADCChInfo[i].eADCModule,
            (uint8_t)staADCChInfo[i].eADCCh,
            (uint8_t)staADCChInfo[i].eADCResolution,
            (uint8_t)staADCChInfo[i].eValType,
            (uint8_t)(uiaValues[i] >> 8U),
            (uint8_t)uiaValues[i],
        };
        (void)bUART_CAN_Bridge_SendData(uiaMeasurement, sizeof(uiaMeasurement));
    }
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

    if(!bTrigSrc_ConfigureCTimer(eTrigSrc_CTIMER2_MAT0,
                                 eTrigConsumer_ADC0_Slot1,
                                 ADC0_TRIGGER_FREQUENCY_HZ))
    {
        return false;
    }

    if(!bTrigSrc_StartCTimer(eTrigSrc_CTIMER1_MAT0,
                              eTrigConsumer_ADC0_Slot0))
    {
        return false;
    }

    if(!bTrigSrc_StartCTimer(eTrigSrc_CTIMER2_MAT0,
                              eTrigConsumer_ADC0_Slot1))
    {
        (void)bTrigSrc_StopCTimer(eTrigSrc_CTIMER1_MAT0,
                                  eTrigConsumer_ADC0_Slot0);
        return false;
    }

    return true;
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
#endif


#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/input/input.h>
#include <zephyr/drivers/pinctrl.h>

#include "UI_InputController.h"
#include "../LVGL_Display_Types.h"
#include "GenericMacro.h"

#include "QDC/NXP_eQDC_API.h"

static sT_eQDCConfig_t eQDCUIScreen = {0};
static sT_QEncData_t stTEncData = {0};

static uint32_t uiNAV_COUNTS_PER_DETENT = 0U;
static int32_t iEncoderCountAccumulator = 0;

static bool bConfigure_eQDC_PhasePins( void );
static bool bConfigure_EncSWPin( void );
static void vEncoder_Callback(sT_eQDC_PosChangeNotify_t stTeQDCData);
static inline void vMark_EncDataInvalid(sT_eQDC_PosChangeNotify_t *pstNotifyCtrl);
static void vEnc_SWPressed_InterruptHandler(struct input_event *pstEvent, void *puserData);
static bool bCalculate_Detents(sT_UIInput_t *pstTUIInput, sT_eQDC_PosChangeNotify_t stTeQDCData);
static void vSubmit_Message(sT_UIInput_t *pstTUIInput);

static inline void vSet_EncSWPressed( void );
static inline void vClear_EncSWPressed( void );

PINCTRL_DT_DEFINE(EQDC0_PHASE_PIN_NODE);

static const struct device *const pstEncSWInputDevice = DEVICE_DT_GET(DT_PARENT(ENC_SW_PIN_NODE));
INPUT_CALLBACK_DEFINE(pstEncSWInputDevice, vEnc_SWPressed_InterruptHandler, NULL);

K_MSGQ_DEFINE(kmsgq_eQDC_MessageQueue, sizeof(sT_UIInput_t), MAX_ENCODER_MESSAGEs, 4);

bool bInitialize_eQDC( void )
{
    if(!bConfigure_eQDC_PhasePins())
        return false;

    eQDCUIScreen.bIsConfigOk = false;
    eQDCUIScreen.eID = eQDC_0;
    eQDCUIScreen.eQDCCountMode = eQDC_QuadratureCycle_1Count;
    eQDCUIScreen.eQuadratureMode = eQDCMode_NormalQuadrature;
    eQDCUIScreen.eInpRoute_PhaseA = eQDCIN_4;
    eQDCUIScreen.eInpRoute_PhaseB = eQDCIN_9;

    eQDCUIScreen.stTHWTrigCtrl.bUseHWTriggerForEncRead = true;
    eQDCUIScreen.stTHWTrigCtrl.eCTimerSrc = eTrigSrc_CTIMER0_MAT2;
    eQDCUIScreen.stTHWTrigCtrl.uiTrigFrequency_Hz = 500;

    eQDCUIScreen.stTInpFiltConfig.bIsEnabled = false;
    eQDCUIScreen.stTInpFiltConfig.eFilterSampleCount = eQDC_FiltSampleCOunt_3U;
    eQDCUIScreen.stTInpFiltConfig.eRefClock_PreScalar = eQDC_Prescalar_4096U;
    eQDCUIScreen.stTInpFiltConfig.uiSamplePeriod_us = 250U;
    eQDCUIScreen.pvUserCallback = vEncoder_Callback;

    k_mutex_init(&stTEncData.stkIsLocked);

    if(!bConfigure_EncSWPin())
        return false;

    switch(eQDCUIScreen.eQDCCountMode)
    {
        case eQDC_QuadratureCycle_1Count:
            uiNAV_COUNTS_PER_DETENT = 1U;
            break;
        case eQDC_QuadratureCycle_2Count:
            uiNAV_COUNTS_PER_DETENT = 2U;
            break;
        case eQDC_QuadratureCycle_4Count:
            uiNAV_COUNTS_PER_DETENT = 4U;
            break;
        default:
            uiNAV_COUNTS_PER_DETENT = 1U;            
            break;
    }
    iEncoderCountAccumulator = 0;

    vInit_eQDC(&eQDCUIScreen);

    if(!eQDCUIScreen.bIsConfigOk)
    {
        FHALT("Quadrature Encoder Configuration Failed");
        return false;
    }

    printf("eQDC Initialized\n\r");
    return true;  
}


static void vEncoder_Callback(sT_eQDC_PosChangeNotify_t stTeQDCData)
{    
    sT_UIInput_t stTUIInput = {0};
    if(!bCalculate_Detents(&stTUIInput, stTeQDCData))
    {
        return;
    }

    vSubmit_Message(&stTUIInput);
}

static void vSubmit_Message(sT_UIInput_t *pstTUIInput)
{
    if(pstTUIInput == NULL)
    {
        return;
    }

    uint32_t uiFreeMsgs = k_msgq_num_free_get(&kmsgq_eQDC_MessageQueue);    
    if(uiFreeMsgs > 0)
    {
        k_msgq_put(&kmsgq_eQDC_MessageQueue, pstTUIInput, K_NO_WAIT);
    }
    else
    {
        sT_UIInput_t stTTemp = {0};
        (void)k_msgq_get(&kmsgq_eQDC_MessageQueue, &stTTemp, K_NO_WAIT);
        (void)k_msgq_put(&kmsgq_eQDC_MessageQueue, pstTUIInput, K_NO_WAIT);
    }

}

static bool bCalculate_Detents(sT_UIInput_t *pstTUIInput, sT_eQDC_PosChangeNotify_t stTeQDCData)
{
    if(pstTUIInput == NULL)
    {
        FHALT("Null Pointer Reference");
        return false;
    }
    if(uiNAV_COUNTS_PER_DETENT == 0U)
    {
        pstTUIInput->eType = eUIInput_None;
        pstTUIInput->iDetents = 0;
        pstTUIInput->uiTimestamp_ms = k_uptime_get_32();
        return false;
    }
    if(stTeQDCData.iEncPosDiff == 0)
    {
        pstTUIInput->eType = eUIInput_None;
        pstTUIInput->iDetents = 0;
        pstTUIInput->uiTimestamp_ms = k_uptime_get_32();
        return false;
    }

    int32_t iSignedDelta = abs((int)stTeQDCData.iEncPosDiff);
    if(stTeQDCData.eDir == eQDC_Dir_CCW)
    {
        iSignedDelta = -iSignedDelta;
    }

    iEncoderCountAccumulator += iSignedDelta;

    int32_t iDetent_Delta = (iEncoderCountAccumulator / (int32_t)uiNAV_COUNTS_PER_DETENT);
    iEncoderCountAccumulator %= (int32_t)uiNAV_COUNTS_PER_DETENT;

    if(iDetent_Delta == 0)
    {
        pstTUIInput->eType = eUIInput_None;
        pstTUIInput->iDetents = 0;
        pstTUIInput->uiTimestamp_ms = k_uptime_get_32();
        return false;        
    }

    pstTUIInput->iDetents = iDetent_Delta;
    pstTUIInput->eType = eUIInput_Rotate;
    pstTUIInput->uiTimestamp_ms = k_uptime_get_32();
    return true;
}

int iGet_NextUIEvent(sT_UIInput_t *pstUIInput, k_timeout_t ktimeout)
{
    if(pstUIInput == NULL)
    {
        FHALT("Null Pointer Reference");
        return -EINVAL;
    }

    int iRes = k_msgq_get(&kmsgq_eQDC_MessageQueue, pstUIInput, ktimeout);
    return iRes;
}

void vClear_UIEvents( void )
{
    k_msgq_purge(&kmsgq_eQDC_MessageQueue);
}

void vClear_EncoderRotationData(sT_QEncData_t *pstQEncData)
{
    if(pstQEncData == NULL)
        return;

    vMark_EncDataInvalid(&pstQEncData->stTEncPhaseData);    
}

static inline void vMark_EncDataInvalid(sT_eQDC_PosChangeNotify_t *pstNotifyCtrl)
{
    if(pstNotifyCtrl == NULL)
        return;
    atomic_store_explicit(&pstNotifyCtrl->bIsValid, false, memory_order_release);
}

static inline void vSet_EncSWPressed( void )
{
    sT_UIInput_t stTUIInput = {
        .eType = eUIInput_Press,
        .iDetents = 0,
        .uiTimestamp_ms = k_uptime_get_32()
    };
    vSubmit_Message(&stTUIInput);
}

static inline void vClear_EncSWPressed( void )
{
    sT_UIInput_t stTUIInput = {
        .eType = eUIInput_Release,
        .iDetents = 0,
        .uiTimestamp_ms = k_uptime_get_32()
    };
    vSubmit_Message(&stTUIInput);
}

bool bIsEncSWPressed( void )
{
    bool bIsPressed = atomic_load_explicit(&stTEncData.bIsEncPressed, memory_order_acquire);
    return bIsPressed;
}

static void vEnc_SWPressed_InterruptHandler(struct input_event *pstEvent, void *puserData)
{
    ARG_UNUSED(puserData);

    if ((pstEvent->type == INPUT_EV_KEY) &&
        (pstEvent->code == INPUT_KEY_0))
    {
        if(pstEvent->value == 1)
        {
            vSet_EncSWPressed();
        }
        else if(pstEvent->value == 0)
        {
            vClear_EncSWPressed();
        }
    }    
}

static bool bConfigure_eQDC_PhasePins( void )
{
    static const struct pinctrl_dev_config *pstPinConfig = PINCTRL_DT_DEV_CONFIG_GET(EQDC0_PHASE_PIN_NODE);

    int res = pinctrl_apply_state(pstPinConfig, PINCTRL_STATE_DEFAULT);
    if(res != 0)
    {
        FHALT("eQDC phase pin configuration failed: %d", res);
        return false;        
    }

    return true;
}

static bool bConfigure_EncSWPin(void)
{
    if (!device_is_ready(pstEncSWInputDevice))
    {
        FHALT("Encoder switch input device is not ready");
        return false;
    }

    return true;
}
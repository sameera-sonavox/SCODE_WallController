
#include <zephyr/drivers/pwm.h>
#include <fsl_ctimer.h>

#include "NXP_PWM_API.h"
#include "NXP_PWM_ProjDef.h"
#include "../GenericMacro.h"

typedef enum{
    ePWM_Disabled = 0,
    ePWM_Running,
    ePWM_Stopped,
} eT_PWM_State_t;

typedef enum{
    eCTIMER_0 = 0,
    eCTIMER_1,
    eCTIMER_2,
    eCTIMER_3,
    eCTIMER_4,
    eNUMBER_OF_CTIMER_MODULES
} eT_CTIMER_Module_t;

typedef struct{
    eCTPWM_Channel_t ePWMID;
    eT_PWM_State_t eState;
    ePWM_IdleLevel_t eIdleLevel;
    uint32_t uiFrequency_Hz;
    uint8_t uiDutyCycle_percent;
} sT_CTPWM_Config_t;

sT_CTPWM_Config_t staPWM_Config[eNUMBER_OF_CTPWM_CHANNELS] = {0};

eT_CTIMER_Module_t eTimerSub_To_CTIMER_Module_Map[eNUMBER_OF_CTPWM_CHANNELS] = {
    eCTIMER_0, //eCTPWM1
    eCTIMER_0, //eCTPWM2
    eCTIMER_0, //eCTPWM3
    eCTIMER_0, //eCTPWM4
    eCTIMER_1, //eCTPWM5
    eCTIMER_1, //eCTPWM6
    eCTIMER_1, //eCTPWM7
    eCTIMER_1, //eCTPWM8
    eCTIMER_2, //eCTPWM9
    eCTIMER_2, //eCTPWM10
    eCTIMER_2, //eCTPWM11
    eCTIMER_2, //eCTPWM12
    eCTIMER_3, //eCTPWM13
    eCTIMER_3, //eCTPWM14
    eCTIMER_3, //eCTPWM15
    eCTIMER_3, //eCTPWM16
    eCTIMER_4, //eCTPWM17
    eCTIMER_4, //eCTPWM18
    eCTIMER_4, //eCTPWM19
    eCTIMER_4  //eCTPWM20
};

eCTPWM_Channel_t eCTIMER_Module_To_PWMChannel_Map[eNUMBER_OF_CTIMER_MODULES][4] = {
    {eCTPWM1, eCTPWM2, eCTPWM3, eCTPWM4},
    {eCTPWM5, eCTPWM6, eCTPWM7, eCTPWM8},
    {eCTPWM9, eCTPWM10, eCTPWM11, eCTPWM12},
    {eCTPWM13, eCTPWM14, eCTPWM15, eCTPWM16},
    {eCTPWM17, eCTPWM18, eCTPWM19, eCTPWM20},
};

static uint32_t uiGetInitial_PWM_Frequency(eCTPWM_Channel_t ePWMID);
static ePWM_IdleLevel_t eGet_PWM_IdleLevel(eCTPWM_Channel_t ePWMID);
static const struct pwm_dt_spec * pstIsValid_PWM_Channel(eCTPWM_Channel_t ePWMID);
static void vCheck_For_ValidFrequencyUpdate(eT_CTIMER_Module_t eCTIMER_Module);

void vInit_PWM(void)
{    
    uint32_t uiPeriod_ns, uiPulse_ns;

    for(int i = 0; i < eNUMBER_OF_CTPWM_CHANNELS; i++)
    {
        if(bIsCTPWMChannelEnabled((eCTPWM_Channel_t)i))
        {
            const struct pwm_dt_spec *pstPWM = pstGet_CTPWM_HWPin((eCTPWM_Channel_t)i);

            if((pstPWM == NULL) || !pwm_is_ready_dt(pstPWM))
            {
                FHALT("PWM channel %d is enabled but not properly initialized in the device tree", i);
                continue;
            }

            staPWM_Config[i].ePWMID = (eCTPWM_Channel_t)i;
            staPWM_Config[i].eState = ePWM_Stopped;
            staPWM_Config[i].uiFrequency_Hz = uiGetInitial_PWM_Frequency((eCTPWM_Channel_t)i);
            staPWM_Config[i].eIdleLevel = eGet_PWM_IdleLevel((eCTPWM_Channel_t)i);
            staPWM_Config[i].uiDutyCycle_percent = staPWM_Config[i].eIdleLevel == eIdle_Level_Low ? 0 : 100;

            uiPeriod_ns = PWM_HZ(staPWM_Config[i].uiFrequency_Hz);
            uiPulse_ns = (uiPeriod_ns * staPWM_Config[i].uiDutyCycle_percent) / 100U;

            pwm_set_dt(pstPWM, PWM_HZ(staPWM_Config[i].uiFrequency_Hz), uiPulse_ns);
        }
        else{
            staPWM_Config[i].ePWMID = (eCTPWM_Channel_t)i;
            staPWM_Config[i].eState = ePWM_Disabled;
            staPWM_Config[i].eIdleLevel = eIdle_Level_Low;
            staPWM_Config[i].uiFrequency_Hz = 0;
            staPWM_Config[i].uiDutyCycle_percent = 0;
        }
    }
}

static ePWM_IdleLevel_t eGet_PWM_IdleLevel(eCTPWM_Channel_t ePWMID)
{
    if(!bIsCTPWMChannelEnabled(ePWMID))
    {
        return 0;
    }

    switch(ePWMID)
    {
        case eCTPWM1:
        #if defined(CTPWM_1)
            return CTPWM1_IDLE_LEVEL;
        #else
            FHALT("Idle level requested for a disabled channel: %d", ePWMID);
            return 0;
        #endif
        case eCTPWM2:
        #if defined(CTPWM_2)
            return CTPWM2_IDLE_LEVEL;
        #else
            FHALT("Idle level requested for a disabled channel: %d", ePWMID);
            return 0;
        #endif
        case eCTPWM3:
        #if defined(CTPWM_3)
            return CTPWM3_IDLE_LEVEL;
        #else
            FHALT("Idle level requested for a disabled channel: %d", ePWMID);
            return 0;
        #endif
        case eCTPWM4:
        #if defined(CTPWM_4)
            return CTPWM4_IDLE_LEVEL;
        #else
            FHALT("Idle level requested for a disabled channel: %d", ePWMID);
            return 0;
        #endif
        case eCTPWM5:
        #if defined(CTPWM_5)
            return CTPWM5_IDLE_LEVEL;
        #else
            FHALT("Idle level requested for a disabled channel: %d", ePWMID);
            return 0;
        #endif
        case eCTPWM6:
        #if defined(CTPWM_6)
            return CTPWM6_IDLE_LEVEL;
        #else
            FHALT("Idle level requested for a disabled channel: %d", ePWMID);
            return 0;
        #endif
        case eCTPWM7:
        #if defined(CTPWM_7)
            return CTPWM7_IDLE_LEVEL;
        #else
            FHALT("Idle level requested for a disabled channel: %d", ePWMID);
            return 0;
        #endif
        case eCTPWM8:
        #if defined(CTPWM_8)
            return CTPWM8_IDLE_LEVEL;
        #else
            FHALT("Idle level requested for a disabled channel: %d", ePWMID);
            return 0;
        #endif
        case eCTPWM9:
        #if defined(CTPWM_9)
            return CTPWM9_IDLE_LEVEL;
        #else
            FHALT("Idle level requested for a disabled channel: %d", ePWMID);
            return 0;
        #endif
        case eCTPWM10:
        #if defined(CTPWM_10)
            return CTPWM10_IDLE_LEVEL;
        #else
            FHALT("Idle level requested for a disabled channel: %d", ePWMID);
            return 0;
        #endif
        case eCTPWM11:
        #if defined(CTPWM_11)
            return CTPWM11_IDLE_LEVEL;
        #else
            FHALT("Idle level requested for a disabled channel: %d", ePWMID);
            return 0;
        #endif
        case eCTPWM12:
        #if defined(CTPWM_12)
            return CTPWM12_IDLE_LEVEL;
        #else
            FHALT("Idle level requested for a disabled channel: %d", ePWMID);
            return 0;
        #endif
        case eCTPWM13:
        #if defined(CTPWM_13)
            return CTPWM13_IDLE_LEVEL;
        #else
            FHALT("Idle level requested for a disabled channel: %d", ePWMID);
            return 0;
        #endif
        case eCTPWM14:
        #if defined(CTPWM_14)
            return CTPWM14_IDLE_LEVEL;
        #else
            FHALT("Idle level requested for a disabled channel: %d", ePWMID);
            return 0;
        #endif
        case eCTPWM15:
        #if defined(CTPWM_15)
            return CTPWM15_IDLE_LEVEL;
        #else
            FHALT("Idle level requested for a disabled channel: %d", ePWMID);
            return 0;
        #endif
        case eCTPWM16:
        #if defined(CTPWM_16)
            return CTPWM16_IDLE_LEVEL;
        #else
            FHALT("Idle level requested for a disabled channel: %d", ePWMID);
            return 0;
        #endif
        case eCTPWM17:
        #if defined(CTPWM_17)
            return CTPWM17_IDLE_LEVEL;
        #else
            FHALT("Idle level requested for a disabled channel: %d", ePWMID);
            return 0;
        #endif
        case eCTPWM18:
        #if defined(CTPWM_18)
            return CTPWM18_IDLE_LEVEL;
        #else
            FHALT("Idle level requested for a disabled channel: %d", ePWMID);
            return 0;
        #endif
        case eCTPWM19:
        #if defined(CTPWM_19)
            return CTPWM19_IDLE_LEVEL;
        #else
            FHALT("Idle level requested for a disabled channel: %d", ePWMID);
            return 0;
        #endif
        case eCTPWM20:
        #if defined(CTPWM_20)
            return CTPWM20_IDLE_LEVEL;
        #else
            FHALT("Idle level requested for a disabled channel: %d", ePWMID);
            return 0;
        #endif
        default:
            FHALT("Idle level requested for a disabled channel: %d", ePWMID);
            return 0;

    }
}

static uint32_t uiGetInitial_PWM_Frequency(eCTPWM_Channel_t ePWMID)
{
    if(!bIsCTPWMChannelEnabled(ePWMID))
    {
        return 0;
    }

    switch(ePWMID)
    {
        case eCTPWM1:
        #if defined(CTPWM_1)
            return CTPWM1_BASE_FREQUENCY_Hz;
        #else
            return 0;
        #endif
        case eCTPWM2:
        #if defined(CTPWM_2)
            return CTPWM2_BASE_FREQUENCY_Hz;
        #else
            return 0;
        #endif
        case eCTPWM3:
        #if defined(CTPWM_3)
            return CTPWM3_BASE_FREQUENCY_Hz;
        #else
            return 0;
        #endif
        case eCTPWM4:
        #if defined(CTPWM_4)
            return CTPWM4_BASE_FREQUENCY_Hz;
        #else
            return 0;
        #endif
        case eCTPWM5:
        #if defined(CTPWM_5)
            return CTPWM5_BASE_FREQUENCY_Hz;
        #else
            return 0;
        #endif
        case eCTPWM6:
        #if defined(CTPWM_6)
            return CTPWM6_BASE_FREQUENCY_Hz;
        #else
            return 0;
        #endif
        case eCTPWM7:
        #if defined(CTPWM_7)
            return CTPWM7_BASE_FREQUENCY_Hz;
        #else
            return 0;
        #endif
        case eCTPWM8:
        #if defined(CTPWM_8)
            return CTPWM8_BASE_FREQUENCY_Hz;
        #else
            return 0;
        #endif
        case eCTPWM9:
        #if defined(CTPWM_9)
            return CTPWM9_BASE_FREQUENCY_Hz;
        #else
            return 0;
        #endif
        case eCTPWM10:
        #if defined(CTPWM_10)
            return CTPWM10_BASE_FREQUENCY_Hz;
        #else
            return 0;
        #endif
        case eCTPWM11:
        #if defined(CTPWM_11)
            return CTPWM11_BASE_FREQUENCY_Hz;
        #else
            return 0;
        #endif
        case eCTPWM12:
        #if defined(CTPWM_12)
            return CTPWM12_BASE_FREQUENCY_Hz;
        #else
            return 0;
        #endif
        case eCTPWM13:
        #if defined(CTPWM_13)
            return CTPWM13_BASE_FREQUENCY_Hz;
        #else
            return 0;
        #endif
        case eCTPWM14:
        #if defined(CTPWM_14)
            return CTPWM14_BASE_FREQUENCY_Hz;
        #else
            return 0;
        #endif
        case eCTPWM15:
        #if defined(CTPWM_15)
            return CTPWM15_BASE_FREQUENCY_Hz;
        #else
            return 0;
        #endif
        case eCTPWM16:
        #if defined(CTPWM_16)
            return CTPWM16_BASE_FREQUENCY_Hz;
        #else
            return 0;
        #endif
        case eCTPWM17:
        #if defined(CTPWM_17)
            return CTPWM17_BASE_FREQUENCY_Hz;
        #else
            return 0;
        #endif
        case eCTPWM18:
        #if defined(CTPWM_18)
            return CTPWM18_BASE_FREQUENCY_Hz;
        #else
            return 0;
        #endif
        case eCTPWM19:
        #if defined(CTPWM_19)
            return CTPWM19_BASE_FREQUENCY_Hz;
        #else
            return 0;
        #endif
        case eCTPWM20:
        #if defined(CTPWM_20)
            return CTPWM20_BASE_FREQUENCY_Hz;
        #else
            return 0;
        #endif
        default:
            return 0;

    }
}

static const struct pwm_dt_spec * pstIsValid_PWM_Channel(eCTPWM_Channel_t ePWMID)
{

    if(!bIsCTPWMChannelEnabled(ePWMID))
    {
        FHALT("Attempting to set PWM on a disabled channel: %d", ePWMID);
        return NULL;
    }

    const struct pwm_dt_spec *pstPWM = pstGet_CTPWM_HWPin(ePWMID);
    if(pstPWM == NULL)
    {
        FHALT("Attempting to set PWM on a uninitialized channel: %d", ePWMID);
        return NULL;
    }

    return pstPWM;
}

bool bUpdate_PWM_Frequency_Hz(eCTPWM_Channel_t ePWMID, uint32_t uiFrequency_Hz)
{
    uint32_t uiPeriod_ns, uiPulse_ns;
    int ret;

    const struct pwm_dt_spec *pstPWM = pstIsValid_PWM_Channel(ePWMID);
    if(pstPWM == NULL)
    {
        return false;
    }

    if(uiFrequency_Hz == 0)
    {
        FHALT("Invalid PWM Frequency: %d Hz on PWM Ch: %d", uiFrequency_Hz, ePWMID);
        return false;
    }
    
    uiPeriod_ns = PWM_HZ(uiFrequency_Hz);
    uiPulse_ns = (uiPeriod_ns * staPWM_Config[ePWMID].uiDutyCycle_percent) / 100U;

    eT_CTIMER_Module_t eCTIMER_Module = eTimerSub_To_CTIMER_Module_Map[ePWMID];
    vCheck_For_ValidFrequencyUpdate(eCTIMER_Module);
    switch (eCTIMER_Module)
    {
        case eCTIMER_0:
            CTIMER_StopTimer(CTIMER0);
            CTIMER_Reset(CTIMER0);
            break;            
        case eCTIMER_1:
            CTIMER_StopTimer(CTIMER1);
            CTIMER_Reset(CTIMER1);
            break;
        case eCTIMER_2:
            CTIMER_StopTimer(CTIMER2);
            CTIMER_Reset(CTIMER2);
            break;
        case eCTIMER_3:
            CTIMER_StopTimer(CTIMER3);
            CTIMER_Reset(CTIMER3);
            break;
        case eCTIMER_4:
            CTIMER_StopTimer(CTIMER4);
            CTIMER_Reset(CTIMER4);
            break;
        default:
            FHALT("Invalid CTIMER module mapping for PWM Ch: %d", ePWMID);
            return false;
    }
    
    ret = pwm_set_dt(pstPWM, uiPeriod_ns, uiPulse_ns);
    if(ret != 0)    
    {
        FHALT("Failed to update PWM frequency for PWM Ch: %d. Error code: %d", ePWMID, ret);
        return false;
    }
    
    staPWM_Config[ePWMID].uiFrequency_Hz = uiFrequency_Hz;
    return true;
}

void vCheck_For_ValidFrequencyUpdate(eT_CTIMER_Module_t eCTIMER_Module)
{
    eCTPWM_Channel_t eaPWMChannels[4];
    memcpy(eaPWMChannels, eCTIMER_Module_To_PWMChannel_Map[eCTIMER_Module], sizeof(eaPWMChannels));

    for(int i = 0; i < 4; i++)
    {
        eCTPWM_Channel_t eChannel = eaPWMChannels[i];
        if(staPWM_Config[eChannel].eState != ePWM_Disabled)
        {
            FHALT("The System contains active PWM channels that share the same CTIMER. So all channels will get the new frequency.");
        }
    }
}

bool bUpdate_PWM_Duty(eCTPWM_Channel_t ePWMID, uint8_t uiDutyCycle_percent)
{
    uint32_t uiPeriod_ns, uiPulse_ns;

    const struct pwm_dt_spec *pstPWM = pstIsValid_PWM_Channel(ePWMID);
    if(pstPWM == NULL)
    {
        return false;
    }

    if(uiDutyCycle_percent > 100)
    {
        FHALT("Invalid PWM Duty: %d on PWM Ch: %d", uiDutyCycle_percent, ePWMID);
        uiDutyCycle_percent = 100;
    }

    if(uiDutyCycle_percent == 0)
    {
        staPWM_Config[ePWMID].eState = ePWM_Stopped;
        return bStop_PWM(ePWMID);
    }

    staPWM_Config[ePWMID].eState = ePWM_Running;

    uiPeriod_ns = PWM_HZ(staPWM_Config[ePWMID].uiFrequency_Hz);
    uiPulse_ns = (uiPeriod_ns * uiDutyCycle_percent) / 100U;

    staPWM_Config[ePWMID].uiDutyCycle_percent = uiDutyCycle_percent;

    return (pwm_set_dt(pstPWM, uiPeriod_ns, uiPulse_ns) == 0);
}

bool bStop_PWM(eCTPWM_Channel_t ePWMID)
{
    uint32_t uiPulse_ns;

    const struct pwm_dt_spec *pstPWM = pstIsValid_PWM_Channel(ePWMID);
    if(pstPWM == NULL)
    {
        return false;
    }

    switch (staPWM_Config[ePWMID].eIdleLevel)
    {
        case eIdle_Level_Low:
            uiPulse_ns = 0;
            break;
        case eIdle_Level_High:
            uiPulse_ns = PWM_HZ(staPWM_Config[ePWMID].uiFrequency_Hz);
            break;
        default:
            FHALT("Invalid idle level configuration for PWM Ch: %d", ePWMID);
            return false;
    }

    staPWM_Config[ePWMID].eState = ePWM_Stopped;
    staPWM_Config[ePWMID].uiDutyCycle_percent = (uiPulse_ns == 0) ? 0 : 100;

    return (pwm_set_dt(pstPWM, PWM_HZ(staPWM_Config[ePWMID].uiFrequency_Hz), uiPulse_ns) == 0);
}

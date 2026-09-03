#include <zephyr/drivers/pwm.h>
#include "NXP_PWM_ProjDef.h"

typedef struct {
    eCTPWM_Channel_t ePWMID;
    bool bEnabled;
    const struct pwm_dt_spec *pstPWM;
} sCTPWM_PinMap_t;

#if defined(CTPWM_1)
    #define CT0_PWM0_DT DT_ALIAS(pwm_ch_0)
    static const struct pwm_dt_spec stCT0_PWM1 = PWM_DT_SPEC_GET(CT0_PWM0_DT);
#endif

#if defined(CTPWM_2)
#endif

#if defined(CTPWM_3)
#endif

#if defined(CTPWM_4)
#endif

#if defined(CTPWM_5)
#endif

#if defined(CTPWM_6)
#endif

#if defined(CTPWM_7)
#endif

#if defined(CTPWM_8)
#endif

#if defined(CTPWM_9)
#endif

#if defined(CTPWM_10)
#endif

#if defined(CTPWM_11)
#endif

#if defined(CTPWM_12)
#endif

#if defined(CTPWM_13)
#endif

#if defined(CTPWM_14)
#endif

#if defined(CTPWM_15)
#endif

#if defined(CTPWM_16)
#endif

#if defined(CTPWM_17)
#endif

#if defined(CTPWM_18)
#endif

#if defined(CTPWM_19)
#endif

#if defined(CTPWM_20)
#endif

static const sCTPWM_PinMap_t stasCTPWM_PinMap[] = {
#if defined(CTPWM_1)
    {eCTPWM1, true, &stCT0_PWM1},
#endif    
};

bool bIsCTPWMChannelEnabled(eCTPWM_Channel_t eChannel)
{
    for(size_t i = 0; i < sizeof(stasCTPWM_PinMap) / sizeof(stasCTPWM_PinMap[0]); i++)
    {
        if(stasCTPWM_PinMap[i].ePWMID == eChannel)
        {
            return stasCTPWM_PinMap[i].bEnabled;
        }
    }
    return false;
}

const struct pwm_dt_spec *pstGet_CTPWM_HWPin(eCTPWM_Channel_t eChannel)
{
    for(size_t i = 0; i < sizeof(stasCTPWM_PinMap) / sizeof(stasCTPWM_PinMap[0]); i++)
    {
        if(stasCTPWM_PinMap[i].ePWMID == eChannel)
        {
            return stasCTPWM_PinMap[i].pstPWM;
        }
    }
    return NULL;
}

#ifndef NXP_PWM_PROJDEF_H
#define NXP_PWM_PROJDEF_H

#include <zephyr/drivers/pwm.h>

#include "NXP_PWM_Types.h"

#define CTPWM_1
/* #define CTPWM_2
#define CTPWM_3
#define CTPWM_4
#define CTPWM_5
#define CTPWM_6
#define CTPWM_7
#define CTPWM_8
#define CTPWM_9
#define CTPWM_10
#define CTPWM_11
#define CTPWM_12
#define CTPWM_13
#define CTPWM_14
#define CTPWM_15
#define CTPWM_16
#define CTPWM_17
#define CTPWM_18
#define CTPWM_19
#define CTPWM_20 */

#if defined(CTPWM_1)
    #define CTPWM1_BASE_FREQUENCY_Hz    1000U
    #define CTPWM1_IDLE_LEVEL           eIdle_Level_High
#endif

#if defined(CTPWM_2)
    #define CTPWM2_BASE_FREQUENCY_Hz    1000U
    #define CTPWM2_IDLE_LEVEL           eIdle_Level_Low
#endif

#if defined(CTPWM_3)
    #define CTPWM3_BASE_FREQUENCY_Hz    1000U
    #define CTPWM3_IDLE_LEVEL           eIdle_Level_Low
#endif

#if defined(CTPWM_4)
    #define CTPWM4_BASE_FREQUENCY_Hz    1000U
    #define CTPWM4_IDLE_LEVEL           eIdle_Level_Low
#endif

#if defined(CTPWM_5)
    #define CTPWM5_BASE_FREQUENCY_Hz    1000U
    #define CTPWM5_IDLE_LEVEL           eIdle_Level_Low
#endif

#if defined(CTPWM_6)
    #define CTPWM6_BASE_FREQUENCY_Hz    1000U
    #define CTPWM6_IDLE_LEVEL           eIdle_Level_Low
#endif

#if defined(CTPWM_7)
    #define CTPWM7_BASE_FREQUENCY_Hz    1000U
    #define CTPWM7_IDLE_LEVEL           eIdle_Level_Low
#endif

#if defined(CTPWM_8)
    #define CTPWM8_BASE_FREQUENCY_Hz    1000U
    #define CTPWM8_IDLE_LEVEL           eIdle_Level_Low
#endif

#if defined(CTPWM_9)
    #define CTPWM9_BASE_FREQUENCY_Hz    1000U
    #define CTPWM9_IDLE_LEVEL           eIdle_Level_Low
#endif

#if defined(CTPWM_10)
    #define CTPWM10_BASE_FREQUENCY_Hz   1000U
    #define CTPWM10_IDLE_LEVEL          eIdle_Level_Low
#endif

#if defined(CTPWM_11)
    #define CTPWM11_BASE_FREQUENCY_Hz   1000U
    #define CTPWM11_IDLE_LEVEL          eIdle_Level_Low
#endif

#if defined(CTPWM_12)
    #define CTPWM12_BASE_FREQUENCY_Hz   1000U
    #define CTPWM12_IDLE_LEVEL          eIdle_Level_Low
#endif

#if defined(CTPWM_13)
    #define CTPWM13_BASE_FREQUENCY_Hz   1000U
    #define CTPWM13_IDLE_LEVEL          eIdle_Level_Low
#endif

#if defined(CTPWM_14)
    #define CTPWM14_BASE_FREQUENCY_Hz   1000U
    #define CTPWM14_IDLE_LEVEL          eIdle_Level_Low
#endif

#if defined(CTPWM_15)
    #define CTPWM15_BASE_FREQUENCY_Hz   1000U
    #define CTPWM15_IDLE_LEVEL          eIdle_Level_Low
#endif

#if defined(CTPWM_16)
    #define CTPWM16_BASE_FREQUENCY_Hz   1000U
    #define CTPWM16_IDLE_LEVEL          eIdle_Level_Low
#endif

#if defined(CTPWM_17)
    #define CTPWM17_BASE_FREQUENCY_Hz   1000U
    #define CTPWM17_IDLE_LEVEL          eIdle_Level_Low
#endif

#if defined(CTPWM_18)
    #define CTPWM18_BASE_FREQUENCY_Hz   1000U
    #define CTPWM18_IDLE_LEVEL          eIdle_Level_Low
#endif

#if defined(CTPWM_19)
    #define CTPWM19_BASE_FREQUENCY_Hz   1000U
    #define CTPWM19_IDLE_LEVEL          eIdle_Level_Low
#endif

#if defined(CTPWM_20)
    #define CTPWM20_BASE_FREQUENCY_Hz   1000U
    #define CTPWM20_IDLE_LEVEL          eIdle_Level_Low
#endif

bool bIsCTPWMChannelEnabled(eCTPWM_Channel_t eChannel);
const struct pwm_dt_spec *pstGet_CTPWM_HWPin(eCTPWM_Channel_t eChannel);

#endif /* NXP_PWM_PROJDEF_H */

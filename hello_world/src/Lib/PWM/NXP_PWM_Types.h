#ifndef NXP_PWM_TYPES_H
#define NXP_PWM_TYPES_H

#include <stdint.h>
#include <stdbool.h>

typedef enum{
    eCTPWM1 = 0,
    eCTPWM2,
    eCTPWM3,
    eCTPWM4,//CTIMER0
    eCTPWM5,
    eCTPWM6,
    eCTPWM7,
    eCTPWM8,//CTIMER1
    eCTPWM9,
    eCTPWM10,
    eCTPWM11,
    eCTPWM12,//CTIMER2
    eCTPWM13,
    eCTPWM14,
    eCTPWM15,
    eCTPWM16,//CTIMER3
    eCTPWM17,
    eCTPWM18,
    eCTPWM19,
    eCTPWM20,//CTIMER4
    eNUMBER_OF_CTPWM_CHANNELS
} eCTPWM_Channel_t;

typedef enum{
   eIdle_Level_Low = 0,
   eIdle_Level_High
} ePWM_IdleLevel_t;

#endif /* NXP_PWM_TYPES_H */

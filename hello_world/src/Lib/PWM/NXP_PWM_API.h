#ifndef NXP_PWM_API_H
#define NXP_PWM_API_H

#include "NXP_PWM_Types.h"
#include <stdint.h>
#include <stdbool.h>

extern void vInit_PWM(void);
extern bool bUpdate_PWM_Duty(eCTPWM_Channel_t ePWMID, uint8_t uiDutyCycle_percent);
extern bool bStop_PWM(eCTPWM_Channel_t ePWMID);
extern bool bUpdate_PWM_Frequency_Hz(eCTPWM_Channel_t ePWMID, uint32_t uiFrequency_Hz);

#endif /* NXP_PWM_API_H */

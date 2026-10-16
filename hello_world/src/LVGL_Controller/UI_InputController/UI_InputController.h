#ifndef UI_INPUTCONTROLLER_H
#define UI_INPUTCONTROLLER_H

#include <zephyr/kernel.h>
#include "../LVGL_Display_Types.h"

extern bool bInitialize_eQDC( void );
extern bool bIsEncSWPressed( void );
extern int iGet_NextUIEvent(sT_UIInput_t *pstUIInput, k_timeout_t ktimeout);
extern void vClear_UIEvents( void );

#endif//UI_INPUTCONTROLLER_H
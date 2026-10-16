#ifndef UI_SCREENMANAGER_H
#define UI_SCREENMANAGER_H

#include <stdbool.h>
#include <stdint.h>

#include "../LVGL_Display_Types.h"

extern bool bInit_UIScreenManager( void );
extern void vRun_UIScreenManager( void );
extern bool bRequest_UIScreen(eScreenId_t eID);
extern eScreenId_t eGetActiveScreen( void );

#endif//UI_SCREENMANAGER_H
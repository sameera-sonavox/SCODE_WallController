#ifndef WELCOMESCREEN_H
#define WELCOMESCREEN_H

#include "../../LVGL_Display_Types.h"

extern bool bInit_WelcomeScreen(sT_UIScreen_t *pstWelComeScreen);
extern bool bSetup_WelComeScreen( void );
extern void vStart_WelcomeImageFadeIn( void );
extern void vUpdate_DisplayText(const char *pcaText);

#endif//WELCOMESCREEN_H
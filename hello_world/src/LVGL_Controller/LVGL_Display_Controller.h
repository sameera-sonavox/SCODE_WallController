#ifndef LVGL_DISPLAY_CONTROLLER_H
#define LVGL_DISPLAY_CONTROLLER_H

#include "../Lib/API_Usage_Definition.h"

#ifdef USE_LVGL_DISPLAY

#include "LVGL_Display_Types.h"

extern void vInit_UI( void );
extern void vLoad_Screen( eScreenId_t eID );
extern bool bRemove_AudioSource( eAudioSrc_Id_t eSrcId );
extern bool bSet_AudioSource_ActiveState( eAudioSrc_Id_t eSrcId, bool bIsActive );
extern bool bSet_AudioSource_MuteState( eAudioSrc_Id_t eSrcId, bool bIsMute );

#endif

#endif
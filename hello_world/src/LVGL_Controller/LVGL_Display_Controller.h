#ifndef LVGL_DISPLAY_CONTROLLER_H
#define LVGL_DISPLAY_CONTROLLER_H

#include "API_Usage_Definition.h"
#include "LVGL_Display_Types.h"

extern bool bInit_LVGLDisplay( void );
extern void vLoad_Screen( eScreenId_t eID );
extern bool bRemove_AudioSource( eAudioSrc_Id_t eSrcId );
extern bool bSet_AudioSource_ActiveState( eAudioSrc_Id_t eSrcId, bool bIsActive );
extern bool bSet_AudioSource_MuteState( eAudioSrc_Id_t eSrcId, bool bIsMute );
extern void vRun_UI( void );

extern void vClear_EncSWPressed( void );
extern bool bIsEncSWPressed( void );

extern void vSet_ScreenActive(eScreenId_t eId);
extern void vSet_ScreenInactive(eScreenId_t eId);
extern eScreenId_t eGetActiveScreen( void );
extern sT_UIScreenDisplay *pstGetDisplayScreen(eScreenId_t eId);
extern void vClear_EncoderRotationData(sT_QEncData_t *pstQEncData);
extern void vClear_eQDCMessageQueue( void );
extern eHostSystemType_t eGet_HostSystemType( void );
extern void vSet_HostSystemType( eHostSystemType_t eType );
extern void vNotify_SrcVolumeChange( eAudioSrc_Id_t eSrcId, uint8_t uiVol );
extern void vDelete_LVGLObject(lv_obj_t **ppstObj);

#endif

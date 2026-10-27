#ifndef AUDIOSRCCONFIGURATION_H
#define AUDIOSRCCONFIGURATION_H

#include <stddef.h>

#include "../ExtFlash_Controller.h"
#include "../../LVGL_Controller/LVGL_Display_Types.h"

#define DEBUG_AUDIOSRC_CONFIG_FILE_ACCESS

#define AUDIO_SOURCE_CONFIG_RECORD_SIZE         48U
#define MUTEX_ACCESS_WAIT_TIME_ms               200U
#define AUDIO_VOLUME_SAVE_DELAY_ms              750U
#define AUDIO_VOLUME_MAX_DIRTY_TIME_ms          3000U

extern bool bLoad_AudioConfigurations( void );
extern bool bGet_AudioSrcConfigurations(eAudioSrc_Id_t eSrcId, sT_AudioSourceConfig_t *pstSnapShot);
extern bool bIs_AudioSrcConfigLoaded( void );
extern int iRead_AudioSrcConfigurationRecord(eAudioSrc_Id_t eSrcId,
                                              uint8_t *puiData,
                                              size_t uiLength);
extern int iUpdate_AudioSrcConfigurations(uint8_t *puiData, uint8_t uiLength, eAudioSrc_Id_t eSrcId);
extern int iUpdate_AudioSrcVolume(eAudioSrc_Id_t eSrcId, uint8_t uiVolume);
extern int iGet_SelectedAudioSrcConfig( sT_AudioSourceConfig_t *pstConfigSnapshot );
extern int iUpdate_AudioScrSelected(eAudioSrc_Id_t eSrcId, bool bIsSelected);
extern void vGetSnapshot_Of_AudioSrcName(eAudioSrc_Id_t eSrcId, char *pcaSrcName);
extern bool bUpdate_AudioSrcMuteStatus(eAudioSrc_Id_t eSrcId, bool bIsMute);
extern eAudioSrc_Id_t eGet_BluetoothSource( void );
extern eBLEState_t eGet_BleConnectionState( void );
extern bool bSet_BleConnectionState(eBLEState_t eState);

#endif//AUDIOSRCCONFIGURATION_H

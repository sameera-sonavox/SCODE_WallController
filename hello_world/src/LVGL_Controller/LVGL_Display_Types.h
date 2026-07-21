#ifndef LVGL_DISPLAY_TYPES_H
#define LVGL_DISPLAY_TYPES_H

#include "../Lib/API_Usage_Definition.h"

#ifdef USE_LVGL_DISPLAY

#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <lvgl.h>

#include "LVGL_ProjDef.h"

typedef enum
{
    eScreen_Welcome = 0,
    eScreen_SourceSelect,
    eNUMBER_OF_SCREENs
} eScreenId_t;

typedef enum
{
    eUIObj_Label,
    eUIObj_Button,
    eNUMBER_OF_UI_OBJECTs
} eUI_Obj_Type_t;

typedef enum
{
    eAudio_Src_0,
    eAudio_Src_1,
    eAudio_Src_2,
    eAudio_Src_3,
    eAudio_Src_4,
    eAudio_Src_5,
    eAudio_Src_6,
    eAudio_Src_7,
    eAudio_Src_8,
    eAudio_Src_9,
    eAudio_Src_10,
    eAudio_Src_11,
    eAudio_Src_12,
    eAudio_Src_13,
    eAudio_Src_14,
    eAudio_Src_15,
    eAudio_Src_16,
    eAudio_Src_17,
    eAudio_Src_18,
    eAudio_Src_19,
    eAudio_Src_20,
    eAudio_Src_21,
    eAudio_Src_22,
    eAudio_Src_23,
    eAudio_Src_24,
    eAudio_Src_25,
    eAudio_Src_26,
    eAudio_Src_27,
    eAudio_Src_28,
    eAudio_Src_29,
    eAudio_Src_30,
    eAudio_Src_31,
    eNUMBER_OF_AUDIO_SOURCES
} eAudioSrc_Id_t;

typedef struct
{
    lv_coord_t lx;
    lv_coord_t ly;
    int32_t iwidth;
    int32_t iheight;
    lv_color_t lcolor_Text;
    lv_color_t lcolor_BackGround;
} sT_UIObj_Label;

typedef struct
{
    eUI_Obj_Type_t eObjType;
    lv_obj_t *pstUIObj;
    union{
        sT_UIObj_Label stTObj_Label;
    } uiObject;

} sT_UIControl;

typedef struct
{
    eAudioSrc_Id_t eSrcId;
    lv_obj_t *pstAudSrcObj;
    char acName[24];
    _Atomic bool bIsActive;
    _Atomic bool bIsMute;
    _Atomic bool bIsSelected;
    _Atomic bool bIsVisible;
    uint16_t uiVolume;
    sT_UIControl staUIControls[NUMBER_OF_UI_CONTROLS_PER_AUDIO_SOURCE];
} sT_AudioSource_t;

typedef struct
{
    eScreenId_t eScreenId;
    union
    {
        sT_AudioSource_t staAudioSources[eNUMBER_OF_AUDIO_SOURCES];
    } screenType;

} sT_UIScreenDisplay;

typedef struct
{
    eScreenId_t eScreenId;
    _Atomic bool bIsActive;
    lv_obj_t *pstScreenObj;

    void (*pfCreate)(void);
    void (*pfShow)(void);
    void (*pfDestroy)(void);

    sT_UIScreenDisplay stTDisplayInfo;
} sT_UIScreen_t;

#endif

#endif
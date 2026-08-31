#ifndef LVGL_DISPLAY_TYPES_H
#define LVGL_DISPLAY_TYPES_H

#include "API_Usage_Definition.h"

#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <lvgl.h>
#include <zephyr/kernel.h>
#include <zephyr/fs/fs.h>

#include "../Lib/QDC/NXP_eQDC_Types.h"
#include "LVGL_ProjDef.h"
#include "../ExtFlash_Controller/ExtFlash_ProjDef.h"

typedef enum
{
    eScreen_Welcome = 0,
    eScreen_SourceSelect,
    eNUMBER_OF_SCREENs
} eScreenId_t;

typedef enum
{
    eUIObj_Label,
    eUIObj_Indicator,
    eUIObj_VolControl,
    eUIObj_Img,
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

typedef enum{
    eFont_Size_10,
    eFont_Size_12,
    eFont_Size_14,
    eFont_Size_16,
    eFont_Size_18,
    eFont_Size_20,
/*     eFont_Size_22,
    eFont_Size_24,
    eFont_Size_26,
    eFont_Size_28,
    eFont_Size_30,
    eFont_Size_32,
    eFont_Size_34,
    eFont_Size_36,
    eFont_Size_38,
    eFont_Size_40,
    eFont_Size_42,
    eFont_Size_44,
    eFont_Size_46,
    eFont_Size_48, */
    eNUMBER_OF_FONT_SIZEs
} eFontSize_t;

typedef enum
{
    eHostSystem_None,
    eHostSystem_DCM,
    eHostSystem_LMA,
    eHostSystem_DMA,
    eNUMBER_OF_HOST_SYSTEMs
} eHostSystemType_t;

typedef struct
{
    lv_coord_t lx;
    lv_coord_t ly;
    int32_t iwidth;
    int32_t iheight;
    lv_color_t lcolor_Text;
    lv_color_t lcolor_BackGround;
    char pcaText[DEFAULT_LABEL_TEXT_LENGTH];
} sT_UIObj_Label;

typedef struct
{
    lv_coord_t lx;
    lv_coord_t ly;
    int32_t iwidth;
    int32_t iheight;
    lv_color_t lcolor_Border;
    _Atomic bool bIsVisible;
} sT_UIObj_Indicator;

typedef struct
{
    lv_coord_t lx_Top;
    lv_coord_t ly_Top;
    int32_t iWidth;
    int32_t iHeight;
    _Atomic bool baIsLocked[eNUMBER_OF_AUDIO_SOURCES];
    _Atomic uint8_t uiMaxVolume;
    _Atomic uint8_t uiVol;
    lv_color_t lcolor_VolColor;    
} sT_UIObj_VolControl;

typedef struct
{
    char *pcaFilePath;    
} sT_UIObj_Image;

typedef struct
{
    eUI_Obj_Type_t eObjType;
    lv_obj_t *pstUIObj;
    union{
        sT_UIObj_Label stTObj_Label;
        sT_UIObj_Indicator stTObj_Indicator;
        sT_UIObj_VolControl stTObj_VolCtrl;
        sT_UIObj_Image stObj_ImgCtrl;
    } uiObject;

} sT_UIControl;

typedef struct
{
    eAudioSrc_Id_t eSrcId;
    lv_obj_t *pstAudSrcObj;
    char acName[24];
    _Atomic bool bIsExlusivelyOwned;//Only one entity will own the source
    _Atomic bool bIsInclusivelyOwned;//Multiple entities can own the resource
    _Atomic bool bIsActive;
    _Atomic bool bIsMute;
    _Atomic bool bIsSelected;
    _Atomic bool bIsVisible;
    _Atomic uint16_t uiVolume;
    sT_UIControl staUIControls[NUMBER_OF_UI_CONTROLS_PER_AUDIO_SOURCE];
    lv_point_precise_t sourceHoriz_SeparatorPoints[2];
} sT_AudioSource_t;

typedef struct
{
    sT_eQDC_PosChangeNotify_t stTEncPhaseData;
    _Atomic bool bIsEncPressed;
    struct k_mutex stkIsLocked;
} sT_QEncData_t;

typedef struct
{
    lv_obj_t *pstSrcVolObj;
    sT_UIObj_VolControl stTVolCtrl;
    sT_AudioSource_t staAudioSources[eNUMBER_OF_AUDIO_SOURCES];
} sT_AudioSrc_Display_t;

typedef struct
{
    lv_obj_t *pstWelSrcObj;
    lv_obj_t *pstAnimationSuffixObj;
    uint32_t uiDisplayTime_ms;
    sT_UIControl staUIControls[NUMBER_OF_UI_CONTROLS_FOR_WELCOME_SCREEN];
    struct k_mutex mutex_DisplayText;
    char caDisplayText[DEFAULT_LABEL_TEXT_LENGTH];
} sT_WelComeScreen_Display_t;

typedef struct
{
    eScreenId_t eScreenId;
    union
    {
        sT_AudioSrc_Display_t stTAudioSrcDisplay;
        sT_WelComeScreen_Display_t stTWelcomScrDisplay;
    } screenType;

} sT_UIScreenDisplay;

typedef bool (*bScreenCreate_t)( void );

typedef struct sT_UIScreen_t
{
    eScreenId_t eScreenId;
    _Atomic bool bIsActive;
    lv_obj_t *pstScreenObj;

    bScreenCreate_t pfCreate;
    void (*pfShow)(void);
    void (*pfDestroy)(void);

    sT_UIScreenDisplay stTDisplayInfo;
} sT_UIScreen_t;

typedef struct
{
    _Atomic eHostSystemType_t eHostSystem;
} sT_HostSystem_t;

#endif

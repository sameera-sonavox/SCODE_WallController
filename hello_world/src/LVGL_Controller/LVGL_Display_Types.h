#ifndef LVGL_DISPLAY_TYPES_H
#define LVGL_DISPLAY_TYPES_H

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
    eScreen_None = 0,
    eScreen_Welcome,
    eScreen_AudioSrcInfo,
    eScreen_SourceSelect,
    eScreen_SourceMute,
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
/*     eAudio_Src_4,
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
    eAudio_Src_31, */
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

typedef enum
{
    eUIInput_None = 0,
    eUIInput_Rotate,
    eUIInput_Press,
    eUIInput_Release,
    eNUMBER_OF_UI_INPUT_EVENTs
} eUIInputType_t;

typedef enum
{
    eAudioSrc_Type_None = 0,
    eAudioSrc_Type_Wired,
    eAudioSrc_Type_Bluetooth,
    eNUMBER_OF_AUDIO_SRC_TYPEs
} eAudioSrc_Type_t;

typedef struct
{
    eUIInputType_t eType;

    int32_t iDetents;
    uint32_t uiTimestamp_ms;
} sT_UIInput_t;

typedef struct
{
    eAudioSrc_Id_t eSrcId;
    eAudioSrc_Type_t eSrcType;

    char caSrcName[24];

    _Atomic bool bIsActive;
    _Atomic bool bIsMute;
    _Atomic bool bIsSelected;
    _Atomic uint16_t uiVolume;
} sT_AudioSourceConfig_t;

typedef struct
{
    sT_eQDC_PosChangeNotify_t stTEncPhaseData;
    _Atomic bool bIsEncPressed;
    struct k_mutex stkIsLocked;
} sT_QEncData_t;

typedef struct sT_UIScreenRuntime_t
{
    eScreenId_t eScreenId;
    lv_obj_t *pstRootObj;
    void *pvScreenData;
} sT_UIScreenRuntime_t;

typedef struct
{
    eScreenId_t eScreenId;

    sT_UIScreenRuntime_t *pstScreenRuntime;
    bool bDeleteOnExit;

    bool (*pfCreate)(sT_UIScreenRuntime_t *pstScreenRuntime);
    void (*pfEnter)(sT_UIScreenRuntime_t *pstScreenRuntime);
    void (*pfExit)(sT_UIScreenRuntime_t *pstScreenRuntime);
    void (*pfHandleInput)(sT_UIScreenRuntime_t *pstScreenRuntime, const sT_UIInput_t *pstTUIInput);
    void (*pfRun)(sT_UIScreenRuntime_t *pstScreenRuntime);
} sT_UIScreenDescriptor_t;

typedef struct
{
    _Atomic eHostSystemType_t eHostSystem;
} sT_HostSystem_t;

#endif

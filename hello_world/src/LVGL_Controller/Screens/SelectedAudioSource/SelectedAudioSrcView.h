#ifndef SELECTEDAUDIOSRCVIEW_H
#define SELECTEDAUDIOSRCVIEW_H

#include "../../LVGL_Display_Types.h"

typedef struct
{
    lv_obj_t *pstRoot;
    lv_obj_t *pstVolumeControl;
    lv_obj_t *pstVolumeLabel;
    lv_obj_t *pstSourceNameLabel;
} sT_SelectedAudioSrcViewObjects_t;

typedef struct
{
    eAudioSrc_Type_t eSourceType;
    bool (*pfCreate)(sT_SelectedAudioSrcViewObjects_t *pstViewObjects);
    void (*pfHandleLongPress)(void);
} sT_SelectedAudioSrcViewOperations_t;

#endif//SELECTEDAUDIOSRCVIEW_H

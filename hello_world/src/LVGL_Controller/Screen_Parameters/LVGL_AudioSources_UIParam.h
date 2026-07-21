#ifndef LVGL_AUDIOSOURCES_UIPARAM_H
#define LVGL_AUDIOSOURCES_UIPARAM_H

#include "../../Lib/API_Usage_Definition.h"

#ifdef USE_LVGL_DISPLAY

#include "../LVGL_Display_Types.h"

#define AUDIO_SRC_TEXT_LABEL_WIDTH              50U
#define AUDIO_SRC_TEXT_LABEL_HEIGHT             30U
#define AUDIO_SOURCE_LIST_DISPLAY_WIDTH         150
#define AUDIO_SOURCE_LIST_DISPLAY_HEIGHT        100
#define AUDIO_SRC_INITIAL_POS_X                 10
#define AUDIO_SRC_INITIAL_POS_Y                 10
#define AUDIO_SRC_CONTAINER_HEIGHT              30
#define AUDIO_SRC_CONTAINER_WIDTH               80

extern sT_UIObj_Label stTAudioSrcList_FirstLabel;

#endif

#endif//
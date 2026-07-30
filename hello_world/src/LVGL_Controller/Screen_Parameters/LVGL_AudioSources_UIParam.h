#ifndef LVGL_AUDIOSOURCES_UIPARAM_H
#define LVGL_AUDIOSOURCES_UIPARAM_H

#include "API_Usage_Definition.h"

#include "../LVGL_Display_Types.h"

//Diplay Dimensions
#define DISPLAY_WIDTH                           160U
#define DISPLAY_HEIGHT                          128U

#define AUDIO_SOURCE_LIST_DISPLAY_WIDTH         120
#define AUDIO_SOURCE_LIST_DISPLAY_HEIGHT        125

#define SCREEN_BACKGROUND_COLOR                 0x0A0A0A

//Audio Source Screen
//Separator
#define SEPARATOR_WIDTH                         1U
#define SEPARATOR_COLOR                         0x1C1B1B

#pragma region Audio SOurce List
//
#define AUDIO_SRC_CONTAINER_HEIGHT              32
#define AUDIO_SRC_CONTAINER_WIDTH               (AUDIO_SOURCE_LIST_DISPLAY_WIDTH - 0U)
#define DEFAULT_AUDIOSRC_SEPARATOR_COORD_Y      (AUDIO_SRC_CONTAINER_HEIGHT - 1U) 
#define AUDIO_SRC_TEXT_LABEL_WIDTH              75U
#define AUDIO_SRC_TEXT_LABEL_HEIGHT             30U
#define AUDIO_SRC_INITIAL_POS_X                 10
#define AUDIO_SRC_INITIAL_POS_Y                 10
#define AUDIO_SRC_TEXT_FONT_SIZE                eFont_Size_12

#define PADDING_BETWEEN_TWO_SOURCES             2U
#define SOURCE_LIST_BACKGROUND_COLOR            SCREEN_BACKGROUND_COLOR
#define ACTIVE_AUDIO_SRC_TEXT_COLOR             0xF8F9FB
#define INACTIVE_AUDIO_SRC_TEXT_COLOR           0xF7BD
#define MUTE_AUDIO_SRC_TEXT_COLOR               0xC163

#pragma endregion

extern sT_UIObj_Label stTAudioSrcList_FirstLabel;

#endif//

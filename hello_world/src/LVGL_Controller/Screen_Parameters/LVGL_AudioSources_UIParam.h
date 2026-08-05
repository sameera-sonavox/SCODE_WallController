#ifndef LVGL_AUDIOSOURCES_UIPARAM_H
#define LVGL_AUDIOSOURCES_UIPARAM_H

#include "API_Usage_Definition.h"

#include "../LVGL_Display_Types.h"

//Diplay Dimensions
#define DISPLAY_WIDTH                           160U
#define DISPLAY_HEIGHT                          128U

#define AUDIO_SOURCE_LIST_DISPLAY_WIDTH         120
#define AUDIO_SOURCE_LIST_DISPLAY_HEIGHT        125

#define SCREEN_BACKGROUND_R  0x0A
#define SCREEN_BACKGROUND_G  0x0A
#define SCREEN_BACKGROUND_B  0x0A

#define SCREEN_BACKGROUND_COLOR_HEX \
    ((SCREEN_BACKGROUND_R << 16) | (SCREEN_BACKGROUND_G << 8) | SCREEN_BACKGROUND_B)

#define SCREEN_BACKGROUND_COLOR_LV \
    LV_COLOR_MAKE(SCREEN_BACKGROUND_R, SCREEN_BACKGROUND_G, SCREEN_BACKGROUND_B)

//Audio Source Screen
//Separator
#define SEPARATOR_WIDTH                         1U
#define SEPARATOR_COLOR                         0x1C1B1B

#pragma region Audio SOurce List
//
#define AUDIO_SRC_SCROLLBAR_WIDTH               3U
#define INDICATOR_SCROLLBAR_GAP                 7U      
#define AUDIO_SRC_CONTAINER_HEIGHT              32
#define AUDIO_SRC_CONTAINER_WIDTH               (AUDIO_SOURCE_LIST_DISPLAY_WIDTH - 0U)
#define DEFAULT_AUDIOSRC_SEPARATOR_COORD_Y      (AUDIO_SRC_CONTAINER_HEIGHT - 1U) 
#define AUDIO_SRC_TEXT_LABEL_WIDTH              75U
#define AUDIO_SRC_TEXT_LABEL_HEIGHT             30U
#define AUDIO_SRC_INITIAL_POS_X                 10
#define AUDIO_SRC_INITIAL_POS_Y                 10
#define AUDIO_SRC_TEXT_FONT_SIZE                eFont_Size_12

#define PADDING_BETWEEN_TWO_SOURCES             2U
#define SOURCE_LIST_BACKGROUND_COLOR            SCREEN_BACKGROUND_COLOR_HEX
#define ACTIVE_AUDIO_SRC_TEXT_COLOR             0xF8F9FB
#define INACTIVE_AUDIO_SRC_TEXT_COLOR           0xF7BD
#define MUTE_AUDIO_SRC_TEXT_COLOR               0xC163
#define AUDIO_SRC_SELECTED_COLOR                0xA1A3A5//0x494A4D
#define AUDIO_SRC_SELECTED_TEXT_COLOR           0x0F2F4F

#define AUDIO_SRC_INDICATOR_WIDTH               5U
#define AUDIO_SRC_INDICATOR_HEIGHT              5U
#define INDICATOR_BORDER_WIDTH                  1U
#define INDICATOR_BORDER_COLOR                  0xFFFFFF

#define VOL_BAR_INDICATOR_COLOR                 0x1A66B1
#define VOL_BAR_BORDER_WIDTH                    1U
#define VOL_BAR_BORDER_COLOR                    VOL_BAR_INDICATOR_COLOR
#define VOL_BAR_HEIGHT                          90U
#define VOL_BAR_WIDTH                           20U
#define VOL_BAR_TITLE_LABEL_HEIGHT              15U
#define VOL_BAR_TEXT_FONT_SIZE                  eFont_Size_12
#define VOL_BAR_TEXT_COLOR                      0xFFFFFF
#define VOL_BAR_ANIM_DURATION_ms                500U
#define VOL_BAR_SELECTED_BORDER_COLOR           0xA1A3A5
#define VOL_BAR_SELECTED_BORDER_WIDTH           2U

#pragma endregion

extern sT_UIObj_Label stTAdSrc_DefaultLabelSettings;
extern sT_UIObj_Indicator stTAdSrc_DefaultIndicatorSettings;
extern sT_UIObj_VolControl stTAdSrc_DefaultVolCtrlSettings;

extern uint32_t uiNAV_COUNTS_PER_DETENT;
extern _Atomic eHostSystemType_t eHostSystemType;

#endif//

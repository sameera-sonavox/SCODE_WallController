#include "LVGL_AudioSources_UIParam.h"

sT_UIObj_Label stTAdSrc_DefaultLabelSettings = {
    .lx = 4U,
    .ly = 1U,
    .iwidth = AUDIO_SRC_TEXT_LABEL_WIDTH,
    .iheight = AUDIO_SRC_TEXT_LABEL_HEIGHT,
};

sT_UIObj_Indicator stTAdSrc_DefaultIndicatorSettings = {
    .lx = (AUDIO_SRC_CONTAINER_WIDTH - AUDIO_SRC_INDICATOR_WIDTH - 1U),
    .ly = 2U,
    .iheight = AUDIO_SRC_INDICATOR_HEIGHT,
    .iwidth = AUDIO_SRC_INDICATOR_WIDTH,
    .bIsVisible = true,
    .lcolor_Border = SCREEN_BACKGROUND_COLOR_LV,
};

sT_UIObj_VolControl stTAdSrc_DefaultVolCtrlSettings = {
    .lx_Top = AUDIO_SOURCE_LIST_DISPLAY_WIDTH + SEPARATOR_WIDTH + 1U,
    .ly_Top = 0U,
    .iHeight = DISPLAY_HEIGHT,
    .iWidth = (DISPLAY_WIDTH - AUDIO_SOURCE_LIST_DISPLAY_WIDTH - SEPARATOR_WIDTH - 1U),
    .baIsLocked = {false},
    .uiMaxVolume = 100U,
    .uiVol = 0U
};


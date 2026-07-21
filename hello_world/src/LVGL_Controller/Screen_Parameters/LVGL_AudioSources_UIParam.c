#include "../../Lib/API_Usage_Definition.h"

#ifdef USE_LVGL_DISPLAY

#include "LVGL_AudioSources_UIParam.h"

sT_UIObj_Label stTAudioSrcList_FirstLabel = {
    .lx = 10,
    .ly = 3,
    .iwidth = AUDIO_SRC_TEXT_LABEL_WIDTH,
    .iheight = AUDIO_SRC_TEXT_LABEL_HEIGHT,
};

#endif
#include "SelectedAudioSrcWired_View.h"
#include "SelectedAudioSrcWired_UIParam.h"
#include "../SelectedAudioSrc_UIParam.h"
#include "GUI_Guider_Generated/generated/gui_guider.h"
#include "GenericMacro.h"

#include <string.h>

typedef struct
{
    gg_ui_t stGuiderUI;
} sT_SelectedAudioSrcWiredViewContext_t;

static sT_SelectedAudioSrcWiredViewContext_t stWiredViewContext;

static bool bCreate_WiredView(sT_SelectedAudioSrcViewObjects_t *pstViewObjects);

static const sT_SelectedAudioSrcViewOperations_t stWiredViewOperations =
{
    .eSourceType = eAudioSrc_Type_Wired,
    .pfCreate = bCreate_WiredView,
    .pfHandleLongPress = NULL
};

const sT_SelectedAudioSrcViewOperations_t *pstGet_SelectedAudioSrcWiredView(void)
{
    return &stWiredViewOperations;
}

static bool bCreate_WiredView(sT_SelectedAudioSrcViewObjects_t *pstViewObjects)
{
    if(pstViewObjects == NULL)
    {
        FHALT("Wired view output is null");
        return false;
    }

    memset(&stWiredViewContext, 0, sizeof(stWiredViewContext));
    memset(pstViewObjects, 0, sizeof(*pstViewObjects));

    setup_Wired_Home_revised(&stWiredViewContext.stGuiderUI);

    lv_obj_t *pstSliderSlot =
        stWiredViewContext.stGuiderUI.Home_revised.Options_Option_4;

    pstViewObjects->pstRoot = stWiredViewContext.stGuiderUI.Home_revised.screen;
    pstViewObjects->pstVolumeLabel = stWiredViewContext.stGuiderUI.Home_revised.Option_2_Option_label_2;
    pstViewObjects->pstSourceNameLabel = stWiredViewContext.stGuiderUI.Home_revised.Option_3_Option_label_3;

    if(pstViewObjects->pstRoot == NULL ||
       pstSliderSlot == NULL ||
       pstViewObjects->pstVolumeLabel == NULL ||
       pstViewObjects->pstSourceNameLabel == NULL)
    {
        FHALT("GUI Guider failed to create the Wired audio-source view");
        return false;
    }

    lv_obj_t *pstSlider = lv_slider_create(pstSliderSlot);
    if(pstSlider == NULL)
    {
        FHALT("Wired volume slider could not be created");
        return false;
    }

    lv_obj_set_size(pstSlider,
                    WIRED_VOLUME_SLIDER_WIDTH,
                    WIRED_VOLUME_SLIDER_HEIGHT);
    lv_obj_center(pstSlider);
    lv_slider_set_range(pstSlider,
                        SELECTED_AUDIO_SRC_MIN_VOLUME,
                        SELECTED_AUDIO_SRC_MAX_VOLUME);
    lv_obj_remove_flag(pstSlider, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(pstSlider, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_style_bg_color(pstSlider,
                              lv_color_hex(WIRED_VOLUME_SLIDER_TRACK_COLOR),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_opa(pstSlider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(pstSlider, 0U, LV_PART_MAIN);
    lv_obj_set_style_pad_all(pstSlider, 0U, LV_PART_MAIN);

    lv_obj_set_style_bg_color(pstSlider,
                              lv_color_hex(WIRED_VOLUME_SLIDER_INDICATOR_COLOR),
                              LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(pstSlider, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(pstSlider, 0U, LV_PART_INDICATOR);

    lv_obj_set_style_bg_opa(pstSlider, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_border_width(pstSlider, 0U, LV_PART_KNOB);
    lv_obj_set_style_pad_all(pstSlider, 0U, LV_PART_KNOB);

    lv_obj_set_width(pstViewObjects->pstVolumeLabel, LV_SIZE_CONTENT);
    lv_obj_set_align(pstViewObjects->pstVolumeLabel, LV_ALIGN_TOP_MID);
    lv_obj_set_x(pstViewObjects->pstVolumeLabel, 0);
    lv_obj_set_style_text_align(pstViewObjects->pstVolumeLabel,
                                LV_TEXT_ALIGN_CENTER,
                                LV_PART_MAIN);
    lv_label_set_text(pstViewObjects->pstVolumeLabel, "0");

    //lv_obj_set_align(pstViewObjects->pstSourceNameLabel, LV_ALIGN_CENTER);

    pstViewObjects->pstVolumeControl = pstSlider;

    return true;
}

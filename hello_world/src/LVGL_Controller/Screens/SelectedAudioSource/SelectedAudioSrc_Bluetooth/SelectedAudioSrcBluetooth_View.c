#include "SelectedAudioSrcBluetooth_View.h"
#include "SelectedAudioSrcBluetooth_UIParam.h"
#include "../SelectedAudioSrc_UIParam.h"
#include "GUI_Guider_Generated/generated/gui_guider.h"
#include "GenericMacro.h"

#include <zephyr/kernel.h>
#include <string.h>

typedef struct
{
    gg_ui_t stGuiderUI;

    lv_obj_t *pstBluetoothIcon;
} sT_SelectedAudioSrcBluetoothViewContext_t;

static sT_SelectedAudioSrcBluetoothViewContext_t stBluetoothViewContext;

static bool bCreate_BluetoothView(sT_SelectedAudioSrcViewObjects_t *pstViewObjects);
static void vHandle_BluetoothLongPress(void);
static bool bLoadBluetoothIcon(lv_obj_t *pstIconSlot, lv_obj_t *pstBleIcon);

static const sT_SelectedAudioSrcViewOperations_t stBluetoothViewOperations =
{
    .eSourceType = eAudioSrc_Type_Bluetooth,
    .pfCreate = bCreate_BluetoothView,
    .pfHandleLongPress = vHandle_BluetoothLongPress
};

const sT_SelectedAudioSrcViewOperations_t *pstGet_SelectedAudioSrcBluetoothView(void)
{
    return &stBluetoothViewOperations;
}

static bool bCreate_BluetoothView(sT_SelectedAudioSrcViewObjects_t *pstViewObjects)
{
    if(pstViewObjects == NULL)
    {
        FHALT("Bluetooth view output is null");
        return false;
    }

    memset(&stBluetoothViewContext, 0, sizeof(stBluetoothViewContext));
    memset(pstViewObjects, 0, sizeof(*pstViewObjects));

    setup_Home_revised(&stBluetoothViewContext.stGuiderUI);

    lv_obj_t *pstSliderSlot = stBluetoothViewContext.stGuiderUI.Home_revised.Option_4_Level_bar;
    lv_obj_t *pstSliderParent = stBluetoothViewContext.stGuiderUI.Home_revised.Options_Option_4;

    lv_obj_t *pstIconSlot = stBluetoothViewContext.stGuiderUI.Home_revised.Options_Option;
    lv_obj_t *pstBleIcon = NULL;

    pstViewObjects->pstRoot = stBluetoothViewContext.stGuiderUI.Home_revised.screen;
    pstViewObjects->pstVolumeLabel = stBluetoothViewContext.stGuiderUI.Home_revised.Option_2_Option_label_2;
    pstViewObjects->pstSourceNameLabel = stBluetoothViewContext.stGuiderUI.Home_revised.Option_3_Option_label_3;

    if(pstViewObjects->pstRoot == NULL ||
       pstSliderSlot == NULL ||
       pstSliderParent == NULL ||
       pstViewObjects->pstVolumeLabel == NULL ||
       pstViewObjects->pstSourceNameLabel == NULL || 
       pstIconSlot == NULL)
    {
        FHALT("GUI Guider failed to create the Bluetooth audio-source view");
        return false;
    }

    if(!bLoadBluetoothIcon(pstIconSlot, pstBleIcon))
    {
        return false;
    }

    lv_obj_t *pstSlider = lv_slider_create(pstSliderSlot);
    if(pstSlider == NULL)
    {
        FHALT("Bluetooth volume slider could not be created");
        return false;
    }

    lv_obj_set_size(pstSlider,
                    BLUETOOTH_VOLUME_SLIDER_WIDTH,
                    BLUETOOTH_VOLUME_SLIDER_HEIGHT);
    lv_obj_center(pstSlider);
    lv_slider_set_range(pstSlider,
                        SELECTED_AUDIO_SRC_MIN_VOLUME,
                        SELECTED_AUDIO_SRC_MAX_VOLUME);
    lv_obj_remove_flag(pstSlider, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(pstSlider, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_style_bg_color(pstSlider,
                              lv_color_hex(BLUETOOTH_VOLUME_SLIDER_TRACK_COLOR),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_opa(pstSlider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(pstSlider, 0U, LV_PART_MAIN);
    lv_obj_set_style_pad_all(pstSlider, 0U, LV_PART_MAIN);

    lv_obj_set_style_bg_color(pstSlider,
                              lv_color_hex(BLUETOOTH_VOLUME_SLIDER_INDICATOR_COLOR),
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

    pstViewObjects->pstVolumeControl = pstSlider;

    return true;
}

static bool bLoadBluetoothIcon(lv_obj_t *pstIconSlot, lv_obj_t *pstBleIcon)
{
    if(pstIconSlot == NULL)
    {
        FHALT("Null Pointer Reference");
        return false;
    }

    lv_obj_remove_flag(pstIconSlot, LV_OBJ_FLAG_SCROLLABLE);

    lv_image_header_t stTImageHeader = {0};
    lv_result_t eResult = lv_image_decoder_get_info(BLUETOOTH_ICON_IMAGE_PATH, &stTImageHeader);
    if(eResult != LV_RESULT_OK)
    {
        FHALT("Welcome image validation failed: %s, result: %d",
              BLUETOOTH_ICON_IMAGE_PATH,
              (int)eResult);
        return false;        
    }

    pstBleIcon = lv_image_create(pstIconSlot);
    if(pstBleIcon == NULL)
    {
        FHALT("Failed to create Welcome logo image");
        return false;
    }
    
    lv_image_set_src(pstBleIcon, BLUETOOTH_ICON_IMAGE_PATH);
    lv_obj_set_align(pstBleIcon, LV_ALIGN_RIGHT_MID);

    return true;
}

static void vHandle_BluetoothLongPress(void)
{
    printk("Bluetooth Pairing Requested...\n\r");
    #warning Must issue Bluetooth pairing request here. Block user operation until the pairing result is available
}

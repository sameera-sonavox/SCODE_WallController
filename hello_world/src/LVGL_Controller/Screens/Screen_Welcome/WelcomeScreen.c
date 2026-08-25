#include "WelcomeScreen.h"
#include "WelcomeScreen_UIParam.h"
#include "GenericMacro.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <lvgl_zephyr.h>
#include <stdlib.h>

static sT_UIScreen_t *pstUIWelcomeScreen = NULL;

static bool bCreate_WelComeScreen( void );

void vInit_WelcomeScreen(sT_UIScreen_t *pstWelComeScreen)
{
    if(pstWelComeScreen == NULL)
    {
        FHALT("Invalid Null Pointer Reference");
        return;
    }

    pstUIWelcomeScreen = pstWelComeScreen;
    pstUIWelcomeScreen->pfCreate = bCreate_WelComeScreen;
    pstUIWelcomeScreen->pfDestroy = NULL;
    pstUIWelcomeScreen->pfShow = NULL;
    pstUIWelcomeScreen->eScreenId = eScreen_Welcome;

    pstUIWelcomeScreen->stTDisplayInfo.eScreenId = eScreen_Welcome;

    sT_WelComeScreen_Display_t *pstUIScreen = &pstUIWelcomeScreen->stTDisplayInfo.screenType.stTWelcomScrDisplay;
    pstUIScreen->pstWelSrcObj = NULL;
    pstUIScreen->uiDisplayTime_ms = 0U;

    sT_UIControl *pstImgCtrl = &pstUIScreen->staUIControls[0];
    pstImgCtrl->eObjType = eUIObj_Img;
    pstImgCtrl->uiObject.stObj_ImgCtrl.pcaFilePath = WELCOME_IMAGE_PATH;

    sT_UIControl *pstLabelCtrl = &pstUIScreen->staUIControls[1];
    pstLabelCtrl->eObjType = eUIObj_Label;
    sT_UIObj_Label *pstLabel = &pstLabelCtrl->uiObject.stTObj_Label;
    pstLabel->iheight = WELCOMESCRN_LABEL_HEIGHT;
    pstLabel->iwidth = WELCOMESCRN_LABEL_WIDTH;
    pstLabel->lcolor_Text = lv_color_hex(WELCOMESCRN_LABEL_TEXT_COLOR);

    pstUIWelcomeScreen->pfCreate();
}

static bool bCreate_WelComeScreen( void )
{
    lvgl_lock();
    pstUIWelcomeScreen->pstScreenObj = lv_obj_create(NULL);    
    lv_obj_set_style_bg_color(pstUIWelcomeScreen->pstScreenObj, lv_color_hex(SCREEN_BACKGROUND_COLOR_HEX), LV_PART_MAIN);

    lv_obj_t *pstImage = lv_image_create(pstUIWelcomeScreen->pstScreenObj);
    lv_obj_set_pos(pstImage, WELCOMESCRN_TOP_LEFT_X, WELCOMESCRN_TOP_LEFT_Y);
    lv_obj_set_style_radius(pstImage, 0U, LV_PART_MAIN);
    lv_obj_set_size(pstImage, DISPLAY_WIDTH, WELCOMESCRN_IMAGE_CONTAINER_HEIGHT);

    lv_image_header_t stImageHeader = {0};
    lv_result_t eRes = lv_image_decoder_get_info(WELCOME_IMAGE_PATH, &stImageHeader);
    if(eRes != LV_RESULT_OK)
    {
        lvgl_unlock();
        FHALT("Invalid Image File : %s", WELCOME_IMAGE_PATH);
        return false;
    }

    lv_img_set_src(pstImage, WELCOME_IMAGE_PATH);
    lv_image_set_inner_align(pstImage, LV_IMAGE_ALIGN_CENTER);

    lv_obj_t *pstTextContainer = lv_obj_create(pstUIWelcomeScreen->pstScreenObj);
    lv_obj_set_pos(pstTextContainer, WELCOMESCRN_TOP_LEFT_X, WELCOMESCRN_TEXT_CONTAINER_POS_Y);
    lv_obj_set_size(pstTextContainer, WELCOMESCRN_LABEL_WIDTH, WELCOMESCRN_LABEL_HEIGHT);
    lv_obj_set_align(pstTextContainer, LV_ALIGN_CENTER);
    lv_obj_set_style_radius(pstTextContainer, 0U, LV_PART_MAIN);

    //lv_obj_t *pstLabel = lv_label_create(pstParent);

    lvgl_unlock();
    
    return true;
}
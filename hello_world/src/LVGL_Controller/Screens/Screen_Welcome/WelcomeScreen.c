#include "WelcomeScreen.h"
#include "WelcomeScreen_UIParam.h"
#include "../../LVGL_Display_Controller.h"
#include "GenericMacro.h"
#include "../../LVGL_TextAnimator/LVGL_TextAnimate.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <lvgl_zephyr.h>
#include <stdlib.h>

static sT_UIScreen_t *pstUIWelcomeScreen = NULL;
static sT_WelComeScreen_Display_t *pstWelcomeDisplay = NULL;

static bool bCreate_WelComeScreen( void );
static void *pGetWelcomeScreen_UIObject(eUI_Obj_Type_t eType);
static sT_UIControl *pstGetWelcomeScreen_UIControl(eUI_Obj_Type_t eType);
static void vSet_WelComeImage_Opacity( void *pvImage, int32_t iOpacity);
static void vWelcomeScreen_ImageFadingCompleted(lv_anim_t *pstImgAnimation);

static const char *const pcaAnimateString[] = {" .", " ..", " ...", ""};

bool bInit_WelcomeScreen(sT_UIScreen_t *pstWelComeScreen)
{
    if(pstWelComeScreen == NULL)
    {
        FHALT("Invalid Null Pointer Reference");
        return false;
    }

    pstUIWelcomeScreen = pstWelComeScreen;
    pstUIWelcomeScreen->pfCreate = bCreate_WelComeScreen;
    pstUIWelcomeScreen->pfDestroy = NULL;
    pstUIWelcomeScreen->pfShow = NULL;
    pstUIWelcomeScreen->eScreenId = eScreen_Welcome;

    pstUIWelcomeScreen->stTDisplayInfo.eScreenId = eScreen_Welcome;
    pstWelcomeDisplay = &pstUIWelcomeScreen->stTDisplayInfo.screenType.stTWelcomScrDisplay;
    (void)k_mutex_init(&pstWelcomeDisplay->mutex_DisplayText);

    pstWelcomeDisplay->pstWelSrcObj = NULL;
    pstWelcomeDisplay->uiDisplayTime_ms = 0U;

    sT_UIControl *pstImgCtrl = &pstWelcomeDisplay->staUIControls[0];
    pstImgCtrl->eObjType = eUIObj_Img;
    pstImgCtrl->uiObject.stObj_ImgCtrl.pcaFilePath = WELCOME_IMAGE_PATH;

    sT_UIControl *pstLabelCtrl = &pstWelcomeDisplay->staUIControls[1];
    pstLabelCtrl->eObjType = eUIObj_Label;
    sT_UIObj_Label *pstLabel = &pstLabelCtrl->uiObject.stTObj_Label;
    pstLabel->iheight = WELCOMESCRN_LABELCONTAINER_HEIGHT;
    pstLabel->iwidth = WELCOMESCRN_LABELCONTAINER_WIDTH;
    pstLabel->lcolor_Text = lv_color_hex(WELCOMESCRN_LABEL_TEXT_COLOR);

    vUpdate_DisplayText(WELCOMESCRN_LABEL_DEFAULT_TEXT);
    
    return pstUIWelcomeScreen->pfCreate();
}

void vUpdate_DisplayText(const char *pcaText)
{
    if((pcaText == NULL) || (pstWelcomeDisplay == NULL))
    {
        FHALT("Invalid display text update arguments");
        return;
    }

    int iResult = k_mutex_lock(&pstWelcomeDisplay->mutex_DisplayText, K_MSEC(200));
    if(iResult < 0)
    {
        FHALT("Welcome Screen Display Text Mutex Locked Time Out");
        return;
    }

    sT_UIControl *pstLabelCtrl = pstGetWelcomeScreen_UIControl(eUIObj_Label);
    if(pstLabelCtrl == NULL)
    {
        k_mutex_unlock(&pstWelcomeDisplay->mutex_DisplayText);
        return;
    }

    sT_UIObj_Label *pstLabel = &pstLabelCtrl->uiObject.stTObj_Label;
    size_t uiTextLength = strlen(pcaText);
    if(uiTextLength >= sizeof(pstLabel->pcaText))
    {
        k_mutex_unlock(&pstWelcomeDisplay->mutex_DisplayText);
        FHALT("Welcome Screen display text is too long");
        return;
    }

    memcpy(pstLabel->pcaText, pcaText, uiTextLength + 1U); 

    lv_obj_t *plv_Label = pstLabelCtrl->pstUIObj;
    if((plv_Label == NULL) || !lv_obj_is_valid(plv_Label))
    {
        k_mutex_unlock(&pstWelcomeDisplay->mutex_DisplayText);
        FHALT("Invalid LVGL Object returned");
        return;
    }

    //Dynamic Label Size Update
    char caLongestText[DEFAULT_LABEL_TEXT_LENGTH];
    snprintf(caLongestText, sizeof(caLongestText), "%s...", pcaText);

    lv_label_set_text(plv_Label, caLongestText);
    lv_obj_set_size(plv_Label, LV_SIZE_CONTENT, WELCOMESCRN_LABEL_HEIGHT);
    lv_obj_update_layout(plv_Label);

    lv_coord_t lMaxTtextWidth = lv_obj_get_width(plv_Label);
    lv_obj_set_width(plv_Label, lMaxTtextWidth);
    lv_obj_align(plv_Label, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text(plv_Label, pcaText);

    k_mutex_unlock(&pstWelcomeDisplay->mutex_DisplayText);
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
        vDelete_LVGLObject(&pstUIWelcomeScreen->pstScreenObj);
        lvgl_unlock();
        FHALT("Invalid Image File : %s", WELCOME_IMAGE_PATH);
        return false;
    }

    lv_img_set_src(pstImage, WELCOME_IMAGE_PATH);
    lv_image_set_inner_align(pstImage, LV_IMAGE_ALIGN_CENTER);
    lv_obj_set_style_image_opa(pstImage, LV_OPA_TRANSP, LV_PART_MAIN);
    sT_UIControl *pstUIControl = pstGetWelcomeScreen_UIControl(eUIObj_Img);
    if(pstUIControl == NULL)
    {
        vDelete_LVGLObject(&pstUIWelcomeScreen->pstScreenObj);
        lvgl_unlock();
        FHALT("Invalid UI Control for Image Type");
        return false;        
    }
    pstUIControl->pstUIObj = pstImage;

    lv_obj_t *pstTextContainer = lv_obj_create(pstUIWelcomeScreen->pstScreenObj);
    lv_obj_set_pos(pstTextContainer, WELCOMESCRN_TOP_LEFT_X, WELCOMESCRN_TEXT_CONTAINER_POS_Y);
    lv_obj_set_size(pstTextContainer, WELCOMESCRN_LABELCONTAINER_WIDTH, WELCOMESCRN_LABELCONTAINER_HEIGHT);
    lv_obj_set_style_radius(pstTextContainer, 0U, LV_PART_MAIN);
    lv_obj_set_style_border_width(pstTextContainer, 0U, LV_PART_MAIN);
    lv_obj_set_style_pad_all(pstTextContainer, 0U, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(pstTextContainer, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_remove_flag(pstTextContainer, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *pstLabel = lv_label_create(pstTextContainer);
    lv_obj_set_size(pstLabel, WELCOMESCRN_LABELCONTAINER_WIDTH, WELCOMESCRN_LABEL_HEIGHT);
    lv_label_set_long_mode(pstLabel, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    lv_obj_set_style_radius(pstLabel, 0U, LV_PART_MAIN);
    lv_obj_set_style_text_align(pstLabel, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(pstLabel, LV_ALIGN_CENTER, 0U, 0U);
    lv_obj_add_flag(pstLabel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_flag(pstLabel, LV_OBJ_FLAG_HIDDEN, true);

    pstUIControl = pstGetWelcomeScreen_UIControl(eUIObj_Label);
    if(pstUIControl == NULL)
    {
        vDelete_LVGLObject(&pstUIWelcomeScreen->pstScreenObj);
        lvgl_unlock();
        FHALT("Invalid UI Control for Image Type");
        return false;        
    }
    pstUIControl->pstUIObj = pstLabel;

    sT_UIObj_Label *pstLabelCtrl = (sT_UIObj_Label *)&pstUIControl->uiObject.stTObj_Label;
    lv_label_set_text(pstLabel, pstLabelCtrl->pcaText);
    lv_obj_set_style_text_color(pstLabel, pstLabelCtrl->lcolor_Text, LV_PART_MAIN);

    lvgl_unlock();
    
    return true;
}

void vStart_WelcomeImageFadeIn( void )
{
    sT_UIControl *pstUIImgControl = pstGetWelcomeScreen_UIControl(eUIObj_Img);
    sT_UIControl *pstUILabelControl = pstGetWelcomeScreen_UIControl(eUIObj_Label);
    if(pstUIImgControl == NULL || pstUILabelControl == NULL)
    {
        return;
    }

    lvgl_lock();

    lv_obj_t *plv_Image = pstUIImgControl->pstUIObj;
    lv_obj_t *plv_Label = pstUILabelControl->pstUIObj;

    (void)lv_anim_delete(plv_Image, vSet_WelComeImage_Opacity);

    lv_obj_set_style_image_opa(plv_Image, LV_OPA_TRANSP, LV_PART_MAIN);

    lv_anim_t stImgAnimation;
    lv_anim_init(&stImgAnimation);
    lv_anim_set_var(&stImgAnimation, plv_Image);
    lv_anim_set_exec_cb(&stImgAnimation, vSet_WelComeImage_Opacity);
    lv_anim_set_values(&stImgAnimation, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_duration(&stImgAnimation, WELCOME_IMAGE_FADE_DURATION_ms);
    lv_anim_set_path_cb(&stImgAnimation, lv_anim_path_ease_in_out);
    lv_anim_set_user_data(&stImgAnimation, plv_Label);
    lv_anim_set_completed_cb(&stImgAnimation, vWelcomeScreen_ImageFadingCompleted);

    (void)lv_anim_start(&stImgAnimation);

    lvgl_unlock();
}

static void vWelcomeScreen_ImageFadingCompleted(lv_anim_t *pstImgAnimation)
{
    if(pstImgAnimation == NULL)
    {
        return;
    }

    lv_obj_t *plv_Label = (lv_obj_t *)lv_anim_get_user_data(pstImgAnimation);
    if(plv_Label == NULL || !lv_obj_is_valid(plv_Label))
    {
        return;
    }

    vStart_TextAnimator(pcaAnimateString, WELCOMESCRN_LABEL_DEFAULT_TEXT, 4U, vUpdate_DisplayText, 1000U);
    lv_obj_set_flag(plv_Label, LV_OBJ_FLAG_HIDDEN, false);
}

static void vSet_WelComeImage_Opacity( void *pvImage, int32_t iOpacity)
{
    lv_obj_set_style_image_opa((lv_obj_t *)pvImage, (lv_opa_t)iOpacity, LV_PART_MAIN);
}

static void *pGetWelcomeScreen_UIObject(eUI_Obj_Type_t eType)
{
    sT_UIControl *pstUICtrl = pstUIWelcomeScreen->stTDisplayInfo.screenType.stTWelcomScrDisplay.staUIControls;

    for(uint8_t i = 0; i < NUMBER_OF_UI_CONTROLS_FOR_WELCOME_SCREEN; i++)
    {
        if(pstUICtrl[i].eObjType == eType)
        {
            return (void *)&pstUICtrl[i].uiObject;
        }
    }
    return NULL;
}

static sT_UIControl *pstGetWelcomeScreen_UIControl(eUI_Obj_Type_t eType)
{
    sT_UIControl *pstUICtrl = pstUIWelcomeScreen->stTDisplayInfo.screenType.stTWelcomScrDisplay.staUIControls;

    for(uint8_t i = 0; i < NUMBER_OF_UI_CONTROLS_FOR_WELCOME_SCREEN; i++)
    {
        if(pstUICtrl[i].eObjType == eType)
        {
            return &pstUICtrl[i];
        }        
    }
    return NULL;
}


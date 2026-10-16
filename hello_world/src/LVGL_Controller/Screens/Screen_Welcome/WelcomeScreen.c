#include "WelcomeScreen.h"
#include "WelcomeScreen_UIParam.h"
#include "../../LVGL_Display_Controller.h"
#include "GenericMacro.h"
#include "../../LVGL_TextAnimator/LVGL_TextAnimate.h"
#include "../../../WallController/WallController_StateMachine/Wallcontroller_StateMachine.h"
#include "GUI_Guider_Generated/generated/gui_guider.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <lvgl_zephyr.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    gg_ui_t stGuiderUI;
    lv_obj_t *pstLogoImage;
} sT_WelcomeScreenContext_t;

static sT_WelcomeScreenContext_t stTWelcomeScreenContext;

static bool bCreate_WelComeScreen( sT_UIScreenRuntime_t *pstScreenRuntime );
static void vEnter_WelcomeScreen( sT_UIScreenRuntime_t *pstScreenRuntime );
static void vExit_WelcomeScreen( sT_UIScreenRuntime_t *pstScreenRuntime );
static void vRun_WelcomeScreen( sT_UIScreenRuntime_t *pstScreenRuntime );

void vStart_WelcomeImageFadeIn( sT_UIScreenRuntime_t *pstScreenRuntime );
static void vSet_WelComeImage_Opacity( void *pvImage, int32_t iOpacity);
static void vWelcomeScreen_ImageFadingCompleted(lv_anim_t *pstImgAnimation);

static const char *const pcaAnimateString[] = {" .", " ..", " ...", ""};

bool bInit_WelcomeScreen(sT_UIScreenRuntime_t *pstWelComeScreen,
                         sT_UIScreenDescriptor_t *pstScreenDescriptor)
{
    if(pstWelComeScreen == NULL || pstScreenDescriptor == NULL)
    {
        FHALT("Null Pointer Reference");
        return false;
    }

    memset(&stTWelcomeScreenContext, 0, sizeof(stTWelcomeScreenContext));

    pstWelComeScreen->eScreenId = eScreen_Welcome;
    pstWelComeScreen->pstRootObj = NULL;
    pstWelComeScreen->pvScreenData = &stTWelcomeScreenContext;

    pstScreenDescriptor->eScreenId = eScreen_Welcome;
    pstScreenDescriptor->pstScreenRuntime = pstWelComeScreen;

    pstScreenDescriptor->bDeleteOnExit = true;
    pstScreenDescriptor->pfCreate = bCreate_WelComeScreen;
    pstScreenDescriptor->pfEnter = vEnter_WelcomeScreen;
    pstScreenDescriptor->pfExit = vExit_WelcomeScreen;
    pstScreenDescriptor->pfHandleInput = NULL;
    pstScreenDescriptor->pfRun = vRun_WelcomeScreen;

    return true;

}

/// @brief Performs Image Validation and Image Loading
/// @param pstScreenRuntime 
/// @return 
static bool bCreate_WelComeScreen( sT_UIScreenRuntime_t *pstScreenRuntime )
{
    if(pstScreenRuntime == NULL)
    {
        FHALT("Null Pointer Reference");
        return false;
    }

    sT_WelcomeScreenContext_t *pstScreenContext = (sT_WelcomeScreenContext_t *)pstScreenRuntime->pvScreenData;
    if(pstScreenContext == NULL)
    {
        FHALT("WelcomeScreen Context is Null");
        return false;
    }

    bool bResult = false;
    lv_obj_t *pstRoot = NULL;
    lv_obj_t *pstLogoSlot = NULL;
    lv_obj_t *pstLogoImage = NULL;

    lvgl_lock();
    if(pstScreenRuntime->pstRootObj != NULL &&
       lv_obj_is_valid(pstScreenRuntime->pstRootObj))
    {
        lvgl_unlock();
        return true;
    }

    pstScreenRuntime->pstRootObj = NULL;
    memset(pstScreenContext, 0, sizeof(*pstScreenContext));

    setup_Sleep_Screen(&pstScreenContext->stGuiderUI);
    pstRoot = pstScreenContext->stGuiderUI.Sleep_Screen.screen;
    pstLogoSlot = pstScreenContext->stGuiderUI.Sleep_Screen.Welcome_LogoSlot;

    if(pstRoot == NULL || pstLogoSlot==NULL)
    {
        FHALT("GUI Guider Failed to Create Welcome Screen");
        goto Exit;        
    }

    lv_obj_remove_flag(pstLogoSlot, LV_OBJ_FLAG_SCROLLABLE);

    lv_image_header_t stTImageHeader = {0};
    lv_result_t eResult = lv_image_decoder_get_info(WELCOME_IMAGE_PATH, &stTImageHeader);
    if(eResult != LV_RESULT_OK)
    {
        FHALT("Welcome image validation failed: %s, result: %d",
              WELCOME_IMAGE_PATH,
              (int)eResult);
        goto Exit;        
    }

    pstLogoImage = lv_image_create(pstLogoSlot);
    if(pstLogoImage == NULL)
    {
        FHALT("Failed to create Welcome logo image");
        goto Exit;
    }
    
    lv_image_set_src(pstLogoImage, WELCOME_IMAGE_PATH);
    lv_obj_center(pstLogoImage);
    pstScreenContext->pstLogoImage = pstLogoImage;

    pstScreenRuntime->pstRootObj = pstRoot;
    bResult = true;

    Exit:

    if(!bResult)
    {
        if(pstRoot != NULL && lv_obj_is_valid(pstRoot))
        {
            lv_obj_delete(pstRoot);
        }
        memset(pstScreenContext, 0, sizeof(*pstScreenContext));
        pstScreenRuntime->pstRootObj = NULL;
    }

    lvgl_unlock();
    return bResult;
}

/// @brief Called when it is required to load the screen
/// @param pstScreenRuntime 
static void vEnter_WelcomeScreen( sT_UIScreenRuntime_t *pstScreenRuntime )
{
    if(pstScreenRuntime == NULL || pstScreenRuntime->pstRootObj == NULL)
    {
        FHALT("Invalid Welcome Screen Runtime Data");
        return;
    }

    lvgl_lock();

    if(!lv_obj_is_valid(pstScreenRuntime->pstRootObj))
    {
        lvgl_unlock();
        FHALT("Invalid Welcome Screen Root Object");
        return;        
    }

    lv_screen_load(pstScreenRuntime->pstRootObj);
    lvgl_unlock();
    
    vStart_WelcomeImageFadeIn(pstScreenRuntime);
}

static void vExit_WelcomeScreen( sT_UIScreenRuntime_t *pstScreenRuntime )
{
    if(pstScreenRuntime == NULL)
    {
        FHALT("Null Pointer Reference");
        return;
    }
        
}

static void vRun_WelcomeScreen( sT_UIScreenRuntime_t *pstScreenRuntime )
{
    if(pstScreenRuntime == NULL)
    {
        FHALT("Null Pointer Reference");
        return;
    }    
}

void vStart_WelcomeImageFadeIn( sT_UIScreenRuntime_t *pstScreenRuntime )
{
    if(pstScreenRuntime == NULL)
    {
        FHALT("Null Pointer Reference");
        return;        
    }

    sT_WelcomeScreenContext_t *pstScreenContext = (sT_WelcomeScreenContext_t *)pstScreenRuntime->pvScreenData;
    if(pstScreenContext == NULL)
    {
        FHALT("WelcomeScreen Context is Null");
        return;
    }

    lvgl_lock();

    if(!lv_obj_is_valid(pstScreenRuntime->pstRootObj))
    {
        lvgl_unlock();
        FHALT("Invalid Welcome Screen Root Object");
        return;        
    }

    lv_obj_t *plv_Image = pstScreenContext->pstLogoImage;
    if(plv_Image == NULL || !lv_obj_is_valid(plv_Image))
    {
        lvgl_unlock();
        FHALT("Invalid Welcome Screen Image Object");
        return;        
    }

    (void)lv_anim_delete(plv_Image, vSet_WelComeImage_Opacity);

    lv_obj_set_style_image_opa(plv_Image, LV_OPA_TRANSP, LV_PART_MAIN);

    lv_anim_t stImgAnimation;
    lv_anim_init(&stImgAnimation);
    lv_anim_set_var(&stImgAnimation, plv_Image);
    lv_anim_set_exec_cb(&stImgAnimation, vSet_WelComeImage_Opacity);
    lv_anim_set_values(&stImgAnimation, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_duration(&stImgAnimation, WELCOME_IMAGE_FADE_DURATION_ms);
    lv_anim_set_path_cb(&stImgAnimation, lv_anim_path_ease_in_out);
    lv_anim_set_completed_cb(&stImgAnimation, vWelcomeScreen_ImageFadingCompleted);

    (void)lv_anim_start(&stImgAnimation);

    lvgl_unlock();
}

static void vWelcomeScreen_ImageFadingCompleted(lv_anim_t *pstImgAnimation)
{
    vWallCtrl_Set_Command(eWallCtrlCmd_Startup);
}

static void vSet_WelComeImage_Opacity( void *pvImage, int32_t iOpacity)
{
    lv_obj_set_style_image_opa((lv_obj_t *)pvImage, (lv_opa_t)iOpacity, LV_PART_MAIN);
}


#include "MenuSelection.h"

#include "GUI_Guider_Generated/generated/gui_guider.h"
#include "../../LVGL_Display_Controller.h"
#include "GenericMacro.h"
#include "../../../ExtFlash_Controller/AudioSrcConfiguration/AudioSrcConfiguration.h"
#include "../../UI_ScreenManager/UI_ScreenManager.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <lvgl_zephyr.h>
#include <stdlib.h>
#include <string.h>

typedef enum
{
    eMenuItem_Back,
    eMenuItem_Bluetooth,
    eMenuItem_Info,
    eNUMBER_OF_MENU_ITEMs
} eMenuItem_t;

typedef struct
{
    gg_ui_t stGuiderUI;

    lv_obj_t *pstMenuSelect_Icon[eNUMBER_OF_MENU_ITEMs];
    lv_obj_t *pstSrcSelectBack_Icon;

    /* Encoder navigation state. These do not change the configured source. */
    eMenuItem_t eFocusedItem;
    eMenuItem_t ePreviouslyFocusedItem;

    uint32_t uiLastNavigation_ms;
} sT_MenuSelectionScreen_Context_t;

static sT_MenuSelectionScreen_Context_t stTMenuSelectionContext;

static bool bCreate_AudioSrcSelectionScreen(sT_UIScreenRuntime_t *pstMenuSelectionScreen);
static void vEnter_AudioSrcSelectionScreen(sT_UIScreenRuntime_t *pstMenuSelectionScreen);
static void vExit_AudioSrcSelectionScreen(sT_UIScreenRuntime_t *pstMenuSelectionScreen);
static void vHandle_EncoderInput(sT_UIScreenRuntime_t *pstMenuSelectionScreen, const sT_UIInput_t *pstTUIInput);
static void vRun_AudioSrcSelectionScreen(sT_UIScreenRuntime_t *pstMenuSelectionScreen);

static bool bCreate_BackIcon(sT_MenuSelectionScreen_Context_t *pstScreenContext);
static bool bCreate_MenuIcon(sT_MenuSelectionScreen_Context_t *pstScreenContext, eMenuItem_t eItem);
static void vSet_SelectedMenuItem(sT_UIScreenRuntime_t *pstMenuSelectionScreen, eMenuItem_t eNewItem);
static lv_obj_t *pstGet_MenuItem(eMenuItem_t eItem, sT_MenuSelectionScreen_Context_t *pstScreenContext);

static void vHandle_EncoderRotation(sT_UIScreenRuntime_t *pstMenuSelectionScreen, const sT_UIInput_t *pstTUIInput);
static void vHandle_EncoderButonPress(sT_UIScreenRuntime_t *pstMenuSelectionScreen, const sT_UIInput_t *pstTUIInput);
static eMenuItem_t eGet_NextMenuItem(const sT_MenuSelectionScreen_Context_t *pstScreenContext,
                                     bool bIsForward);

bool bInit_MainMenuScreen(sT_UIScreenRuntime_t *pstMenuSelectionScreen, sT_UIScreenDescriptor_t *pstScreenDescriptor)
{
    if(pstMenuSelectionScreen == NULL || pstScreenDescriptor == NULL)
    {
        FHALT("Null Pointer Reference");
        return false;
    }

    memset(&stTMenuSelectionContext, 0, sizeof(stTMenuSelectionContext));

    pstMenuSelectionScreen->eScreenId = eScreen_MainMenu;
    pstMenuSelectionScreen->pstRootObj = NULL;
    pstMenuSelectionScreen->pvScreenData = &stTMenuSelectionContext;

    pstScreenDescriptor->eScreenId = eScreen_MainMenu;
    pstScreenDescriptor->pstScreenRuntime = pstMenuSelectionScreen;

    pstScreenDescriptor->bDeleteOnExit = true;
    pstScreenDescriptor->pfCreate = bCreate_AudioSrcSelectionScreen;
    pstScreenDescriptor->pfEnter = vEnter_AudioSrcSelectionScreen;
    pstScreenDescriptor->pfExit = vExit_AudioSrcSelectionScreen;
    pstScreenDescriptor->pfHandleInput = vHandle_EncoderInput;
    pstScreenDescriptor->pfRun = vRun_AudioSrcSelectionScreen;

    stTMenuSelectionContext.eFocusedItem = eMenuItem_Bluetooth;
    stTMenuSelectionContext.ePreviouslyFocusedItem = eNUMBER_OF_MENU_ITEMs;
    stTMenuSelectionContext.uiLastNavigation_ms = 0U;

/*     k_work_init_delayable(&kWork_SubmitNewScreen, vHandle_ScreenSubmit); */

    return true; 
}

static bool bCreate_AudioSrcSelectionScreen(sT_UIScreenRuntime_t *pstMenuSelectionScreen)
{
    if(pstMenuSelectionScreen == NULL)
    {
        FHALT("Null Pointer Reference");
        return false;
    }

    sT_MenuSelectionScreen_Context_t *pstScreenContext = (sT_MenuSelectionScreen_Context_t *)pstMenuSelectionScreen->pvScreenData;
    if(pstScreenContext == NULL)
    {
        FHALT("Audio Source Selection Screen Context is Null");
        return false;        
    }

    bool bResult = false;
    lv_obj_t *pstRoot = NULL;
    
    lvgl_lock();
    if(pstMenuSelectionScreen->pstRootObj != NULL &&
       lv_obj_is_valid(pstMenuSelectionScreen->pstRootObj))
    {
        lvgl_unlock();
        return true;
    }

    pstMenuSelectionScreen->pstRootObj = NULL;
    memset(pstScreenContext, 0, sizeof(*pstScreenContext));

    setup_Menu_revised(&pstScreenContext->stGuiderUI);
    pstRoot = pstScreenContext->stGuiderUI.Menu_revised.screen;

    if(!bCreate_BackIcon(pstScreenContext))
    {
        goto Exit;
    }

    for(uint8_t i = eMenuItem_Bluetooth; i < eNUMBER_OF_MENU_ITEMs; i++)
    {
        if(!bCreate_MenuIcon(pstScreenContext, (eMenuItem_t)i))
        {
            goto Exit;
        }
    }

    pstMenuSelectionScreen->pstRootObj = pstRoot;
    bResult = true;

    Exit:

    if(!bResult)
    {
        if(pstRoot != NULL && lv_obj_is_valid(pstRoot))
        {
            lv_obj_delete(pstRoot);
        }
        
        memset(pstScreenContext, 0, sizeof(*pstScreenContext));
        pstMenuSelectionScreen->pstRootObj = NULL;
    }

    lvgl_unlock();
    return bResult;  
}

static void vEnter_AudioSrcSelectionScreen(sT_UIScreenRuntime_t *pstMenuSelectionScreen)
{
    if(pstMenuSelectionScreen == NULL || pstMenuSelectionScreen->pstRootObj == NULL)
    {
        FHALT("Invalid Audio Source Selection Screen Runtime Data");
        return;
    }

    sT_MenuSelectionScreen_Context_t *pstScreenContext = (sT_MenuSelectionScreen_Context_t *)pstMenuSelectionScreen->pvScreenData;
    if(pstScreenContext == NULL)
    {
        FHALT("Audio Source Screen Context is Null");
        return;
    } 
 
    lvgl_lock();

    if(!lv_obj_is_valid(pstMenuSelectionScreen->pstRootObj))
    {
        lvgl_unlock();
        FHALT("Invalid Audio Source Selection Screen Root Object");
        return;        
    }

    vSet_SelectedMenuItem(pstMenuSelectionScreen, eMenuItem_Bluetooth);
    
    lv_screen_load(pstMenuSelectionScreen->pstRootObj);
    lvgl_unlock(); 
}

static void vSet_SelectedMenuItem(sT_UIScreenRuntime_t *pstMenuSelectionScreen, eMenuItem_t eNewItem)
{
    if(pstMenuSelectionScreen == NULL)
    {
        FHALT("Null Pointer Reference");
        return;
    }

    sT_MenuSelectionScreen_Context_t *pstScreenContext = (sT_MenuSelectionScreen_Context_t *)pstMenuSelectionScreen->pvScreenData;
    if(pstScreenContext == NULL)
    {
        FHALT("Audio Source Screen Context is Null");
        return;
    }

    lv_obj_t *pstMenuItem = NULL;

    if(pstScreenContext->eFocusedItem != eNUMBER_OF_MENU_ITEMs)
    {
        pstMenuItem = pstGet_MenuItem(pstScreenContext->eFocusedItem, pstScreenContext);
        if(pstMenuItem == NULL)
        {
            return;
        }
        lv_obj_set_style_bg_color(pstMenuItem, lv_color_hex(DEFAULT_BACKGROUND_COLOR), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(pstMenuItem, LV_OPA_TRANSP, LV_PART_MAIN);
    }

    pstMenuItem = pstGet_MenuItem(eNewItem, pstScreenContext);
    if(pstMenuItem == NULL)
    {
        return;
    }
    lv_obj_set_style_bg_color(pstMenuItem, lv_color_hex(ITEM_SELECTION_BACKGROUND_COLOR), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(pstMenuItem, LV_OPA_COVER, LV_PART_MAIN);

    pstScreenContext->ePreviouslyFocusedItem = pstScreenContext->eFocusedItem;
    pstScreenContext->eFocusedItem = eNewItem;
}

static lv_obj_t *pstGet_MenuItem(eMenuItem_t eItem, sT_MenuSelectionScreen_Context_t *pstScreenContext)
{
    if(pstScreenContext == NULL)
    {
        FHALT("Null Pointer Reference for Context @Item: %d", eItem);
        return NULL;
    }

    switch(eItem)
    {
        case eMenuItem_Back:
            return pstScreenContext->stGuiderUI.Menu_revised.Option_2;
        case eMenuItem_Bluetooth:
            return pstScreenContext->stGuiderUI.Menu_revised.Option_3;
        case eMenuItem_Info:
            return pstScreenContext->stGuiderUI.Menu_revised.Option_4;
        default:
            FHALT("Invalid Menu Item : %d", eItem);
            return NULL;
    }    
}

static void vExit_AudioSrcSelectionScreen(sT_UIScreenRuntime_t *pstMenuSelectionScreen)
{

}

static void vHandle_EncoderInput(sT_UIScreenRuntime_t *pstMenuSelectionScreen, const sT_UIInput_t *pstTUIInput)
{
    if(pstTUIInput == NULL || pstMenuSelectionScreen == NULL)
    {
        FHALT("Null Pointer Reference");
        return;
    }

    switch(pstTUIInput->eType)
    {
        case eUIInput_Rotate:
            vHandle_EncoderRotation(pstMenuSelectionScreen, pstTUIInput);
            break;
        case eUIInput_Press:
        case eUIInput_Release:
            vHandle_EncoderButonPress(pstMenuSelectionScreen, pstTUIInput);
            break;
        default:
            FHALT("Invalid Encoder Event Type: %d", pstTUIInput->eType);
            break;
    }
}

static void vHandle_EncoderRotation(sT_UIScreenRuntime_t *pstMenuSelectionScreen, const sT_UIInput_t *pstTUIInput)
{
    if(pstTUIInput == NULL || pstMenuSelectionScreen == NULL)
    {
        FHALT("Null Pointer Reference");
        return;
    }

    sT_MenuSelectionScreen_Context_t *pstScreenContext = (sT_MenuSelectionScreen_Context_t *)pstMenuSelectionScreen->pvScreenData;
    if(pstScreenContext == NULL)
    {
        FHALT("Audio Source Selection Screen Context is Null");
        return;        
    }

    const uint32_t uiNow_ms = k_uptime_get_32();

    if((uiNow_ms - pstScreenContext->uiLastNavigation_ms) < MENU_NAVIGATION_BLANK_TIME_ms)
    {
        return;
    }
    if(pstTUIInput->iDetents == 0)
    {
        pstScreenContext->uiLastNavigation_ms = k_uptime_get_32();
        return;
    }

    bool bIsForward = (pstTUIInput->iDetents > 0);
    eMenuItem_t eNewIndex = eGet_NextMenuItem(pstScreenContext, bIsForward);
    if(eNewIndex == eNUMBER_OF_MENU_ITEMs)
    {
        pstScreenContext->uiLastNavigation_ms = k_uptime_get_32();
        return;
    }

    lvgl_lock();

    vSet_SelectedMenuItem(pstMenuSelectionScreen, eNewIndex);

    lvgl_unlock();

    pstScreenContext->uiLastNavigation_ms = uiNow_ms;
}

static eMenuItem_t eGet_NextMenuItem(const sT_MenuSelectionScreen_Context_t *pstScreenContext,
                                         bool bIsForward)
{
    if(pstScreenContext == NULL ||
       pstScreenContext->eFocusedItem >= eNUMBER_OF_MENU_ITEMs)
    {
        return eNUMBER_OF_MENU_ITEMs;
    }

    if(bIsForward)
    {
        if(pstScreenContext->eFocusedItem == eMenuItem_Info)
        {
            return eMenuItem_Back;
        }

        return (eMenuItem_t)(pstScreenContext->eFocusedItem + 1);
    }

    if(pstScreenContext->eFocusedItem == eMenuItem_Back)
    {
        return eMenuItem_Info;
    }

    return (eMenuItem_t)(pstScreenContext->eFocusedItem - 1);
}

static void vHandle_EncoderButonPress(sT_UIScreenRuntime_t *pstMenuSelectionScreen, const sT_UIInput_t *pstTUIInput)
{
/*     if(pstTUIInput == NULL || pstMenuSelectionScreen == NULL)
    {
        FHALT("Null Pointer Reference");
        return;
    }

    if(pstTUIInput->eType != eUIInput_Press)
    {
        return;
    }

    sT_AudioSrcSelectionScreen_Context_t *pstScreenContext = (sT_AudioSrcSelectionScreen_Context_t *)pstMenuSelectionScreen->pvScreenData;
    if(pstScreenContext == NULL)
    {
        FHALT("Audio Source Screen Context is Null");
        return;
    }

    const eSelectedItem_t eFocusedItem = pstScreenContext->eFocusedItem;

    if(eFocusedItem > eItemSelect_Back && eFocusedItem < eItemSelect_Menu)
    {
        const eAudioSrc_Id_t eSrcId = eGet_AudioSrcFromItem(eFocusedItem);
        if(eSrcId >= eNUMBER_OF_AUDIO_SOURCES)
        {
            return;
        }

        const int iResult = iUpdate_AudioScrSelected(eSrcId, true);
        if(iResult != 0)
        {
            FHALT("Source[%d] cannot be selected: %d", eSrcId, iResult);
            return;
        }
        pstScreenContext->eCommittedSourceItem = eFocusedItem;

        lvgl_lock();
        vPlace_SelectionIcon(pstScreenContext->eCommittedSourceItem,
                             pstScreenContext);
        lvgl_unlock();

        (void)k_work_reschedule(
            &kWork_SubmitNewScreen,
            K_MSEC(SCREEN_DISPLAY_WORKE_SCHEDULE_TIME_ms));
        return;
    }

    if(eFocusedItem == eItemSelect_Back)
    {
        (void)k_work_cancel_delayable(&kWork_SubmitNewScreen);
        if(!bRequest_UIScreen(eScreen_AudioSrcInfo))
        {
            FHALT("Screen Navigation Failed");
        }
        return;
    }

    if(eFocusedItem == eItemSelect_Menu)
    {
        (void)k_work_cancel_delayable(&kWork_SubmitNewScreen);
        if(!bRequest_UIScreen(eScreen_MainMenu))
        {
            FHALT("Screen Navigation Failed");
        }
        return;
    }

    FHALT("Invalid focused item: %d", eFocusedItem); */
}

static void vRun_AudioSrcSelectionScreen(sT_UIScreenRuntime_t *pstMenuSelectionScreen)
{

}

static bool bCreate_MenuIcon(sT_MenuSelectionScreen_Context_t *pstScreenContext, eMenuItem_t eItem)
{
    if(pstScreenContext == NULL)
    {
        FHALT("Null Pointer Reference");
        return false;        
    }

    lv_obj_t *pstMenuIconImage = NULL;
    lv_obj_t *pstMenuIconSlot = pstGet_MenuItem(eItem, pstScreenContext);

    if(pstMenuIconSlot == NULL || !lv_obj_is_valid(pstMenuIconSlot))
    {
        FHALT("Invalid Menu Icon container");
        return false;
    }
    lv_obj_remove_flag(pstMenuIconSlot, LV_OBJ_FLAG_SCROLLABLE);

    lv_image_header_t stTImageHeader = {0};
    lv_result_t eResult = lv_image_decoder_get_info(MAINMENU_ICON_IMAGE_PATH, &stTImageHeader);
    if(eResult != LV_RESULT_OK)
    {
        FHALT("Menu icon validation failed: %s, result: %d",
              MAINMENU_ICON_IMAGE_PATH,
              (int)eResult);
        return false;        
    }

    pstMenuIconImage = lv_image_create(pstMenuIconSlot);
    if(pstMenuIconImage == NULL)
    {
        FHALT("Failed to create Menu Icon image");
        return false;
    }
    
    lv_image_set_src(pstMenuIconImage, MAINMENU_ICON_IMAGE_PATH);
    lv_obj_set_align(pstMenuIconImage, LV_ALIGN_RIGHT_MID);
    lv_obj_set_x(pstMenuIconImage, -6);
    pstScreenContext->pstMenuSelect_Icon[eItem] = pstMenuIconImage;
    
    return true;    
}

static bool bCreate_BackIcon(sT_MenuSelectionScreen_Context_t *pstScreenContext)
{
    if(pstScreenContext == NULL)
    {
        FHALT("Null Pointer Reference");
        return false;        
    }

    lv_obj_t *pstBackIconImage = NULL;
    lv_obj_t *pstBackIconSlot = pstScreenContext->stGuiderUI.Menu_revised.Option_2;
    if(pstBackIconSlot == NULL || !lv_obj_is_valid(pstBackIconSlot))
    {
        FHALT("Invalid Back Icon container");
        return false;
    }
    lv_obj_remove_flag(pstBackIconSlot, LV_OBJ_FLAG_SCROLLABLE);

    lv_image_header_t stTImageHeader = {0};
    lv_result_t eResult = lv_image_decoder_get_info(MENUBACK_ICON_IMAGE_PATH, &stTImageHeader);
    if(eResult != LV_RESULT_OK)
    {
        FHALT("Back icon validation failed: %s, result: %d",
              MENUBACK_ICON_IMAGE_PATH,
              (int)eResult);
        return false;        
    }

    pstBackIconImage = lv_image_create(pstBackIconSlot);
    if(pstBackIconImage == NULL)
    {
        FHALT("Failed to create Back Icon image");
        return false;
    }
    
    lv_image_set_src(pstBackIconImage, MENUBACK_ICON_IMAGE_PATH);
    lv_obj_set_align(pstBackIconImage, LV_ALIGN_LEFT_MID);
    lv_obj_set_x(pstBackIconImage, 6);
    pstScreenContext->pstSrcSelectBack_Icon = pstBackIconImage;
    
    return true;
}
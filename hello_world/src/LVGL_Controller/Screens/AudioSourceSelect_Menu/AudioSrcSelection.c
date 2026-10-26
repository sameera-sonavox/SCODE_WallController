#include "AudioSrcSelection.h"
#include "AudioSrcSelection_UIParam.h"
#include "../../LVGL_Display_Controller.h"
#include "GenericMacro.h"
#include "../../../WallController/WallController_StateMachine/Wallcontroller_StateMachine.h"
#include "GUI_Guider_Generated/generated/gui_guider.h"
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
    eItemSelect_Back = 0,
    eItemSelect_Src1,
    eItemSelect_Src2,
    eItemSelect_Src3,
    eItemSelect_Src4,
    eItemSelect_Menu,
    eNUMBER_OF_ITEM_SELECTIONs
} eSelectedItem_t;

typedef enum
{
    eItemSelect_ByConfigData,
    eItemSelect_ByEncoder,
    eNUMBER_OF_ITEM_SELECTION_TYPEs
} eItemSelectionOp_t;

typedef struct
{
    gg_ui_t stGuiderUI;

    lv_obj_t *pstSrcSelect_Icon;
    lv_obj_t *pstMenuSelect_Icon;
    lv_obj_t *pstSrcSelectBack_Icon;

    /* Encoder navigation state. These do not change the configured source. */
    eSelectedItem_t eFocusedItem;
    eSelectedItem_t ePreviouslyFocusedItem;

    /* Source that owns the persistent selection/check icon. */
    eSelectedItem_t eCommittedSourceItem;

    uint32_t uiLastNavigation_ms;
} sT_AudioSrcSelectionScreen_Context_t;

const eSelectedItem_t eaAudioSrc_To_MenuItem_Map[eNUMBER_OF_AUDIO_SOURCES] = {
    eItemSelect_Src1,//eAudio_Src_0,
    eItemSelect_Src2,//eAudio_Src_1,
    eItemSelect_Src3,//eAudio_Src_2,
    eItemSelect_Src4//eAudio_Src_3,
};

sT_AudioSrcSelectionScreen_Context_t stTAudSrcSelectionContext;
struct k_work_delayable kWork_SubmitNewScreen;

static bool bCreate_AudioSrcSelectionScreen(sT_UIScreenRuntime_t *pstAudSrcSelectionScreen);
static void vEnter_AudioSrcSelectionScreen(sT_UIScreenRuntime_t *pstAudSrcSelectionScreen);
static void vExit_AudioSrcSelectionScreen(sT_UIScreenRuntime_t *pstAudSrcSelectionScreen);
static void vHandle_EncoderInput(sT_UIScreenRuntime_t *pstAudSrcSelectionScreen, const sT_UIInput_t *pstTUIInput);
static void vRun_AudioSrcSelectionScreen(sT_UIScreenRuntime_t *pstAudSrcSelectionScreen);

static void vHandle_EncoderRotation(sT_UIScreenRuntime_t *pstAudioSelectionSrcScreen, const sT_UIInput_t *pstTUIInput);
static void vHandle_EncoderButonPress(sT_UIScreenRuntime_t *pstAudioSelectionSrcScreen, const sT_UIInput_t *pstTUIInput);
static eSelectedItem_t eGet_NextMenuItem(const sT_AudioSrcSelectionScreen_Context_t *pstScreenContext,
                                         bool bIsForward);
static void vUpdate_SourceNames(sT_AudioSrcSelectionScreen_Context_t *pstScreenContext);

static void vSet_SelectedItem_ByAudioSrc(eAudioSrc_Id_t eSrcId, sT_AudioSrcSelectionScreen_Context_t *pstScreenContext);
static lv_obj_t *pstGet_ItemObject(eSelectedItem_t eSelectedItem,
                                   sT_AudioSrcSelectionScreen_Context_t *pstScreenContext);
static bool bValidate_GeneratedObjects(sT_AudioSrcSelectionScreen_Context_t *pstScreenContext);
static void vReset_ItemStyles(sT_AudioSrcSelectionScreen_Context_t *pstScreenContext);
static bool bCreate_BackIcon(sT_AudioSrcSelectionScreen_Context_t *pstScreenContext);
static bool bCreate_MenuIcon(sT_AudioSrcSelectionScreen_Context_t *pstScreenContext);
static bool bCreate_SelectionIcon(sT_AudioSrcSelectionScreen_Context_t *pstScreenContext);
static void vPlace_SelectionIcon(eSelectedItem_t eSelectedItem,
                                 sT_AudioSrcSelectionScreen_Context_t *pstScreenContext);
static void vHandle_ScreenSubmit( struct k_work *work);
static eAudioSrc_Id_t eGet_AudioSrcFromItem(eSelectedItem_t eItem);

static void vUpdate_AudioSrcMenuSelection(eItemSelectionOp_t eSelectionOp, sT_AudioSrcSelectionScreen_Context_t *pstScreenContext);
static void vUpdate_Selection_FromConfigData(sT_AudioSrcSelectionScreen_Context_t *pstScreenContext);
static void vUpdate_Selection_FromEncoder(sT_AudioSrcSelectionScreen_Context_t *pstScreenContext);
static void vUpdate_UI_AtSelectionCHanged(sT_AudioSrcSelectionScreen_Context_t *pstScreenContext);

bool bInit_AudioSrcSelectionScreen(sT_UIScreenRuntime_t *pstAudSrcSelectionScreen, sT_UIScreenDescriptor_t *pstScreenDescriptor)
{
    if(pstAudSrcSelectionScreen == NULL || pstScreenDescriptor == NULL)
    {
        FHALT("Null Pointer Reference");
        return false;
    }

    memset(&stTAudSrcSelectionContext, 0, sizeof(stTAudSrcSelectionContext));

    pstAudSrcSelectionScreen->eScreenId = eScreen_SourceSelect;
    pstAudSrcSelectionScreen->pstRootObj = NULL;
    pstAudSrcSelectionScreen->pvScreenData = &stTAudSrcSelectionContext;

    pstScreenDescriptor->eScreenId = eScreen_SourceSelect;
    pstScreenDescriptor->pstScreenRuntime = pstAudSrcSelectionScreen;

    pstScreenDescriptor->bDeleteOnExit = true;
    pstScreenDescriptor->pfCreate = bCreate_AudioSrcSelectionScreen;
    pstScreenDescriptor->pfEnter = vEnter_AudioSrcSelectionScreen;
    pstScreenDescriptor->pfExit = vExit_AudioSrcSelectionScreen;
    pstScreenDescriptor->pfHandleInput = vHandle_EncoderInput;
    pstScreenDescriptor->pfRun = vRun_AudioSrcSelectionScreen;

    k_work_init_delayable(&kWork_SubmitNewScreen, vHandle_ScreenSubmit);

    return true;
}

static void vRun_AudioSrcSelectionScreen(sT_UIScreenRuntime_t *pstAudSrcSelectionScreen)
{
    
}

static void vHandle_EncoderInput(sT_UIScreenRuntime_t *pstAudioSelectionSrcScreen, const sT_UIInput_t *pstTUIInput)
{
    if(pstTUIInput == NULL || pstAudioSelectionSrcScreen == NULL)
    {
        FHALT("Null Pointer Reference");
        return;
    }

    switch(pstTUIInput->eType)
    {
        case eUIInput_Rotate:
            vHandle_EncoderRotation(pstAudioSelectionSrcScreen, pstTUIInput);
            break;
        case eUIInput_Press:
        case eUIInput_Release:
            vHandle_EncoderButonPress(pstAudioSelectionSrcScreen, pstTUIInput);
            break;
        default:
            FHALT("Invalid Encoder Event Type: %d", pstTUIInput->eType);
            break;
    }
}

static void vHandle_EncoderRotation(sT_UIScreenRuntime_t *pstAudioSelectionSrcScreen, const sT_UIInput_t *pstTUIInput)
{
    if(pstTUIInput == NULL || pstAudioSelectionSrcScreen == NULL)
    {
        FHALT("Null Pointer Reference");
        return;
    }

    sT_AudioSrcSelectionScreen_Context_t *pstScreenContext = (sT_AudioSrcSelectionScreen_Context_t *)pstAudioSelectionSrcScreen->pvScreenData;
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
    eSelectedItem_t eNewIndex = eGet_NextMenuItem(pstScreenContext, bIsForward);
    if(eNewIndex == eNUMBER_OF_ITEM_SELECTIONs)
    {
        pstScreenContext->uiLastNavigation_ms = k_uptime_get_32();
        return;
    }

    lvgl_lock();

    pstScreenContext->ePreviouslyFocusedItem =
        pstScreenContext->eFocusedItem;
    pstScreenContext->eFocusedItem = eNewIndex;

    /* Scrolling changes only the blue focus highlight. */
    vUpdate_UI_AtSelectionCHanged(pstScreenContext);

    lvgl_unlock();

    pstScreenContext->uiLastNavigation_ms = uiNow_ms;
}

static eSelectedItem_t eGet_NextMenuItem(
    const sT_AudioSrcSelectionScreen_Context_t *pstScreenContext,
    bool bIsForward)
{
    if(pstScreenContext == NULL ||
       pstScreenContext->eFocusedItem >= eNUMBER_OF_ITEM_SELECTIONs)
    {
        return eNUMBER_OF_ITEM_SELECTIONs;
    }

    if(bIsForward)
    {
        if(pstScreenContext->eFocusedItem == eItemSelect_Menu)
        {
            return eItemSelect_Back;
        }

        return (eSelectedItem_t)(pstScreenContext->eFocusedItem + 1);
    }

    if(pstScreenContext->eFocusedItem == eItemSelect_Back)
    {
        return eItemSelect_Menu;
    }

    return (eSelectedItem_t)(pstScreenContext->eFocusedItem - 1);
}

static void vHandle_EncoderButonPress(sT_UIScreenRuntime_t *pstAudioSelectionSrcScreen, const sT_UIInput_t *pstTUIInput)
{
    if(pstTUIInput == NULL || pstAudioSelectionSrcScreen == NULL)
    {
        FHALT("Null Pointer Reference");
        return;
    }

    if(pstTUIInput->eType != eUIInput_Press)
    {
        return;
    }

    sT_AudioSrcSelectionScreen_Context_t *pstScreenContext = (sT_AudioSrcSelectionScreen_Context_t *)pstAudioSelectionSrcScreen->pvScreenData;
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

        /* Flash access must not run while the LVGL lock is held. */
        const int iResult = iUpdate_AudioScrSelected(eSrcId, true);
        if(iResult != 0)
        {
            FHALT("Source[%d] cannot be selected: %d", eSrcId, iResult);
            return;
        }

        /* Commit UI state only after the configuration update succeeds. */
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

    FHALT("Invalid focused item: %d", eFocusedItem);
}

static eAudioSrc_Id_t eGet_AudioSrcFromItem(eSelectedItem_t eItem)
{
    switch (eItem)
    {
        case eItemSelect_Src1:
            return eAudio_Src_0;
        case eItemSelect_Src2:
            return eAudio_Src_1;
        case eItemSelect_Src3:
            return eAudio_Src_2;
        case eItemSelect_Src4:
            return eAudio_Src_3;
        case eItemSelect_Back:
        case eItemSelect_Menu:
        default:
            FHALT("Invalid audio-source item: %d", eItem);
            return eNUMBER_OF_AUDIO_SOURCES;
    }
}

static void vHandle_ScreenSubmit( struct k_work *work)
{
    ARG_UNUSED(work);

    eAudioSrc_Id_t eSrcId = eNUMBER_OF_AUDIO_SOURCES;

    switch (stTAudSrcSelectionContext.eCommittedSourceItem)
    {
        case eItemSelect_Src1:
            eSrcId = eAudio_Src_0;
            break;
        case eItemSelect_Src2:
            eSrcId = eAudio_Src_1;
            break;
        case eItemSelect_Src3:
            eSrcId = eAudio_Src_2;
            break;
        case eItemSelect_Src4:
            eSrcId = eAudio_Src_3;
            break;
        case eItemSelect_Back:
        case eItemSelect_Menu:
        default:
            FHALT("Invalid Submission Work for Item: %d", stTAudSrcSelectionContext.eCommittedSourceItem);
            return;
    }

    sT_AudioSourceConfig_t stAudioConfig = {0};    
    if(!bGet_AudioSrcConfigurations(eSrcId, &stAudioConfig ))
    {
        FHALT("Audio Source :%d could not be selected", eAudio_Src_0);
        return;        
    }

    eScreenId_t eScrId = (stAudioConfig.bIsMute)? eScreen_SourceMute : eScreen_AudioSrcInfo;
    if(!bRequest_UIScreen(eScrId))
    {
        k_work_schedule(&kWork_SubmitNewScreen, K_MSEC(SCREEN_DISPLAY_WORKE_RESCHEDULE_TIME_ms));
    }
}

static bool bCreate_AudioSrcSelectionScreen(sT_UIScreenRuntime_t *pstAudSrcSelectionScreen)
{
    if(pstAudSrcSelectionScreen == NULL)
    {
        FHALT("Null Pointer Reference");
        return false;
    }

    sT_AudioSrcSelectionScreen_Context_t *pstScreenContext = (sT_AudioSrcSelectionScreen_Context_t *)pstAudSrcSelectionScreen->pvScreenData;
    if(pstScreenContext == NULL)
    {
        FHALT("Audio Source Selection Screen Context is Null");
        return false;        
    }

    bool bResult = false;
    lv_obj_t *pstRoot = NULL;
    
    lvgl_lock();
    if(pstAudSrcSelectionScreen->pstRootObj != NULL &&
       lv_obj_is_valid(pstAudSrcSelectionScreen->pstRootObj))
    {
        lvgl_unlock();
        return true;
    }

    pstAudSrcSelectionScreen->pstRootObj = NULL;
    memset(pstScreenContext, 0, sizeof(*pstScreenContext));

    setup_Source_Selection_revised(&pstScreenContext->stGuiderUI);
    pstRoot = pstScreenContext->stGuiderUI.Source_Selection_revised.screen;
    if(!bValidate_GeneratedObjects(pstScreenContext))
    {
        goto Exit;
    }

    if(!bCreate_BackIcon(pstScreenContext))
    {
        goto Exit;
    }
    if(!bCreate_SelectionIcon(pstScreenContext))
    {
        goto Exit;
    }
    if(!bCreate_MenuIcon(pstScreenContext))
    {
        goto Exit;
    }

    pstAudSrcSelectionScreen->pstRootObj = pstRoot;
    bResult = true;

    Exit:

    if(!bResult)
    {
        if(pstRoot != NULL && lv_obj_is_valid(pstRoot))
        {
            lv_obj_delete(pstRoot);
        }
        
        memset(pstScreenContext, 0, sizeof(*pstScreenContext));
        pstAudSrcSelectionScreen->pstRootObj = NULL;
    }

    lvgl_unlock();
    return bResult;    
}

static bool bValidate_GeneratedObjects(sT_AudioSrcSelectionScreen_Context_t *pstScreenContext)
{
    if(pstScreenContext == NULL)
    {
        FHALT("Null Pointer Reference");
        return false;
    }

    gg_Source_Selection_revised_t *pstUI =
        &pstScreenContext->stGuiderUI.Source_Selection_revised;
    lv_obj_t *const apstRequiredObjects[] = {
        pstUI->screen,
        pstUI->Option,
        pstUI->Option_Option_label,
        pstUI->Option_2,
        pstUI->Option_2_Option_label_2,
        pstUI->Option_3,
        pstUI->Option_3_Option_label_3,
        pstUI->Option_4,
        pstUI->Option_4_Option_label_4,
        pstUI->Option_5,
        pstUI->Option_5_Option_label_5,
        pstUI->Option_6,
        pstUI->Option_6_Option_label_6,
        pstUI->Option_7,
        pstUI->Option_7_Option_label_7
    };

    for(size_t uiIndex = 0U;
        uiIndex < (sizeof(apstRequiredObjects) / sizeof(apstRequiredObjects[0]));
        uiIndex++)
    {
        if(apstRequiredObjects[uiIndex] == NULL ||
           !lv_obj_is_valid(apstRequiredObjects[uiIndex]))
        {
            FHALT("Invalid GUI Guider object at index: %u", (unsigned int)uiIndex);
            return false;
        }
    }

    return true;
}

static bool bCreate_MenuIcon(sT_AudioSrcSelectionScreen_Context_t *pstScreenContext)
{
    if(pstScreenContext == NULL)
    {
        FHALT("Null Pointer Reference");
        return false;        
    }

    lv_obj_t *pstMenuIconImage = NULL;
    lv_obj_t *pstMenuIconSlot = pstScreenContext->stGuiderUI.Source_Selection_revised.Option_7;
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
    pstScreenContext->pstMenuSelect_Icon = pstMenuIconImage;
    
    return true;
}

static bool bCreate_BackIcon(sT_AudioSrcSelectionScreen_Context_t *pstScreenContext)
{
    if(pstScreenContext == NULL)
    {
        FHALT("Null Pointer Reference");
        return false;        
    }

    lv_obj_t *pstBackIconImage = NULL;
    lv_obj_t *pstBackIconSlot = pstScreenContext->stGuiderUI.Source_Selection_revised.Option_2;
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

static bool bCreate_SelectionIcon(sT_AudioSrcSelectionScreen_Context_t *pstScreenContext)
{
    if(pstScreenContext == NULL)
    {
        FHALT("Null Pointer Reference");
        return false;
    }

    lv_obj_t *pstRoot = pstScreenContext->stGuiderUI.Source_Selection_revised.screen;
    if(pstRoot == NULL || !lv_obj_is_valid(pstRoot))
    {
        FHALT("Invalid Selection Icon root container");
        return false;
    }

    lv_image_header_t stImageHeader = {0};
    lv_result_t eResult = lv_image_decoder_get_info(AUDIOSRC_SELECTION_ICON_IMAGE_PATH,
                                                    &stImageHeader);
    if(eResult != LV_RESULT_OK)
    {
        FHALT("Selection icon validation failed: %s, result: %d",
              AUDIOSRC_SELECTION_ICON_IMAGE_PATH,
              (int)eResult);
        return false;
    }

    lv_obj_t *pstSelectionIcon = lv_image_create(pstRoot);
    if(pstSelectionIcon == NULL)
    {
        FHALT("Failed to create Selection Icon image");
        return false;
    }

    lv_image_set_src(pstSelectionIcon, AUDIOSRC_SELECTION_ICON_IMAGE_PATH);
    lv_obj_add_flag(pstSelectionIcon, LV_OBJ_FLAG_HIDDEN);
    pstScreenContext->pstSrcSelect_Icon = pstSelectionIcon;
    return true;
}

static void vEnter_AudioSrcSelectionScreen(sT_UIScreenRuntime_t *pstAudSrcSelectionScreen)
{
    if(pstAudSrcSelectionScreen == NULL || pstAudSrcSelectionScreen->pstRootObj == NULL)
    {
        FHALT("Invalid Audio Source Selection Screen Runtime Data");
        return;
    }

    sT_AudioSrcSelectionScreen_Context_t *pstScreenContext = (sT_AudioSrcSelectionScreen_Context_t *)pstAudSrcSelectionScreen->pvScreenData;
    if(pstScreenContext == NULL)
    {
        FHALT("Audio Source Screen Context is Null");
        return;
    } 
 
    lvgl_lock();

    if(!lv_obj_is_valid(pstAudSrcSelectionScreen->pstRootObj))
    {
        lvgl_unlock();
        FHALT("Invalid Audio Source Selection Screen Root Object");
        return;        
    }

    vReset_ItemStyles(pstScreenContext);
    vUpdate_SourceNames(pstScreenContext);
    vUpdate_AudioSrcMenuSelection(eItemSelect_ByConfigData, pstScreenContext);
    
    lv_screen_load(pstAudSrcSelectionScreen->pstRootObj);
    lvgl_unlock();    
}

static void vUpdate_SourceNames(sT_AudioSrcSelectionScreen_Context_t *pstScreenContext)
{
    if(pstScreenContext == NULL)
    {
        FHALT("Null Pointer Reference");
        return;
    }

    char caSrcName[AUDIO_SOURCE_CONFIG_NAME_SIZE];

    for(eAudioSrc_Id_t eId = eAudio_Src_0; eId < eNUMBER_OF_AUDIO_SOURCES; eId++)
    {
        eSelectedItem_t eItem = eaAudioSrc_To_MenuItem_Map[eId];
        lv_obj_t *pstLabel = NULL;

        switch (eItem)
        {
            case eItemSelect_Src1:
                pstLabel = pstScreenContext->stGuiderUI.Source_Selection_revised.Option_3_Option_label_3;
                break;
            case eItemSelect_Src2:
                pstLabel = pstScreenContext->stGuiderUI.Source_Selection_revised.Option_4_Option_label_4;
                break;
            case eItemSelect_Src3:
                pstLabel = pstScreenContext->stGuiderUI.Source_Selection_revised.Option_5_Option_label_5;
                break;
            case eItemSelect_Src4:
                pstLabel = pstScreenContext->stGuiderUI.Source_Selection_revised.Option_6_Option_label_6;
                break;        
            default:
                pstLabel = NULL;
                break;
        }

        if(pstLabel == NULL)
        {
            FHALT("Invalid Src Label Configuration @eId: %d", eId);
            continue;
        }

        memset(caSrcName, '\0', sizeof(caSrcName));
        vGetSnapshot_Of_AudioSrcName(eId, caSrcName);

        lv_label_set_text(pstLabel, caSrcName);
    }
}

static void vUpdate_AudioSrcMenuSelection(eItemSelectionOp_t eSelectionOp, sT_AudioSrcSelectionScreen_Context_t *pstScreenContext)
{
    switch(eSelectionOp)
    {
        case eItemSelect_ByConfigData:
            vUpdate_Selection_FromConfigData(pstScreenContext);
            break;
        case eItemSelect_ByEncoder:
            vUpdate_Selection_FromEncoder(pstScreenContext);
            break;
        default:
            FHALT("Operation not Implemented");
            return;
    }

    vUpdate_UI_AtSelectionCHanged(pstScreenContext);
}

static void vUpdate_UI_AtSelectionCHanged(sT_AudioSrcSelectionScreen_Context_t *pstScreenContext)
{
    if(pstScreenContext == NULL)
    {
        FHALT("Null Pointer Reference");
        return;
    }

    lv_obj_t *pstSelectedObject =
        pstGet_ItemObject(pstScreenContext->eFocusedItem, pstScreenContext);
    lv_obj_t *pstPrevSelectedObject =
        pstGet_ItemObject(pstScreenContext->ePreviouslyFocusedItem, pstScreenContext);

    if(pstSelectedObject == NULL)
    {
        return;
    }

    if(pstPrevSelectedObject != NULL)
    {
        lv_obj_set_style_bg_color(pstPrevSelectedObject, lv_color_hex(DEFAULT_BACKGROUND_COLOR), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(pstPrevSelectedObject, LV_OPA_TRANSP, LV_PART_MAIN);
    }

    lv_obj_set_style_bg_color(pstSelectedObject, lv_color_hex(ITEM_SELECTION_BACKGROUND_COLOR), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(pstSelectedObject, LV_OPA_COVER, LV_PART_MAIN);
}

static lv_obj_t *pstGet_ItemObject(eSelectedItem_t eSelectedItem,
                                   sT_AudioSrcSelectionScreen_Context_t *pstScreenContext)
{
    if(pstScreenContext == NULL)
    {
        return NULL;
    }

    switch(eSelectedItem)
    {
        case eItemSelect_Back:
            return pstScreenContext->stGuiderUI.Source_Selection_revised.Option_2;
        case eItemSelect_Src1:
            return pstScreenContext->stGuiderUI.Source_Selection_revised.Option_3;
        case eItemSelect_Src2:
            return pstScreenContext->stGuiderUI.Source_Selection_revised.Option_4;
        case eItemSelect_Src3:
            return pstScreenContext->stGuiderUI.Source_Selection_revised.Option_5;
        case eItemSelect_Src4:
            return pstScreenContext->stGuiderUI.Source_Selection_revised.Option_6;
        case eItemSelect_Menu:
            return pstScreenContext->stGuiderUI.Source_Selection_revised.Option_7;
        case eNUMBER_OF_ITEM_SELECTIONs:
        default:
            return NULL;
    }
}

static void vReset_ItemStyles(sT_AudioSrcSelectionScreen_Context_t *pstScreenContext)
{
    for(eSelectedItem_t eItem = eItemSelect_Back;
        eItem < eNUMBER_OF_ITEM_SELECTIONs;
        eItem++)
    {
        lv_obj_t *pstItem = pstGet_ItemObject(eItem, pstScreenContext);
        if(pstItem == NULL || !lv_obj_is_valid(pstItem))
        {
            continue;
        }

        lv_obj_set_style_bg_color(pstItem,
                                  lv_color_hex(DEFAULT_BACKGROUND_COLOR),
                                  LV_PART_MAIN);
        lv_obj_set_style_bg_opa(pstItem, LV_OPA_TRANSP, LV_PART_MAIN);
    }
}

static void vUpdate_Selection_FromConfigData(sT_AudioSrcSelectionScreen_Context_t *pstScreenContext)
{
    sT_AudioSourceConfig_t stSelectedAudioSrcConfig = {0};
    //Get the selected Audio Source Configurations
    if(iGet_SelectedAudioSrcConfig(&stSelectedAudioSrcConfig) != 0)
    {
        FHALT("Selected Audio Source Configuration is empty");
        return;
    }

    vSet_SelectedItem_ByAudioSrc(stSelectedAudioSrcConfig.eSrcId, pstScreenContext);
    vPlace_SelectionIcon(pstScreenContext->eCommittedSourceItem,
                         pstScreenContext);
}

static void vPlace_SelectionIcon(eSelectedItem_t eSelectedItem,
                                 sT_AudioSrcSelectionScreen_Context_t *pstScreenContext)
{
    if(pstScreenContext == NULL ||
       pstScreenContext->pstSrcSelect_Icon == NULL ||
       !lv_obj_is_valid(pstScreenContext->pstSrcSelect_Icon))
    {
        return;
    }

    if(eSelectedItem < eItemSelect_Src1 || eSelectedItem > eItemSelect_Src4)
    {
        lv_obj_add_flag(pstScreenContext->pstSrcSelect_Icon, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_obj_t *pstSelectedSource = pstGet_ItemObject(eSelectedItem,
                                                    pstScreenContext);
    if(pstSelectedSource == NULL || !lv_obj_is_valid(pstSelectedSource))
    {
        lv_obj_add_flag(pstScreenContext->pstSrcSelect_Icon, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_obj_set_parent(pstScreenContext->pstSrcSelect_Icon, pstSelectedSource);
    lv_obj_set_align(pstScreenContext->pstSrcSelect_Icon, LV_ALIGN_RIGHT_MID);
    lv_obj_set_x(pstScreenContext->pstSrcSelect_Icon, -6);
    lv_obj_set_y(pstScreenContext->pstSrcSelect_Icon, 0);
    lv_obj_remove_flag(pstScreenContext->pstSrcSelect_Icon, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(pstScreenContext->pstSrcSelect_Icon);
}

static void vUpdate_Selection_FromEncoder(sT_AudioSrcSelectionScreen_Context_t *pstScreenContext)
{
    
}

static void vSet_SelectedItem_ByAudioSrc(eAudioSrc_Id_t eSrcId, sT_AudioSrcSelectionScreen_Context_t *pstScreenContext)
{
    eSelectedItem_t eNewSelectedItem = eNUMBER_OF_ITEM_SELECTIONs;

    switch(eSrcId)
    {
        case eAudio_Src_0:
            eNewSelectedItem = eItemSelect_Src1;
            break; 
        case eAudio_Src_1:
            eNewSelectedItem = eItemSelect_Src2;
            break; 
        case eAudio_Src_2:
            eNewSelectedItem = eItemSelect_Src3;
            break; 
        case eAudio_Src_3:
            eNewSelectedItem = eItemSelect_Src4;
            break; 
        default:
            FHALT("Invalid Audio Source: %d", eSrcId);
            eNewSelectedItem = eItemSelect_Back;
            break;        
    }

    pstScreenContext->eCommittedSourceItem = eNewSelectedItem;

    if(eNewSelectedItem == pstScreenContext->eFocusedItem)
    {
        return;
    }

    pstScreenContext->ePreviouslyFocusedItem = pstScreenContext->eFocusedItem;
    pstScreenContext->eFocusedItem = eNewSelectedItem;
}

static void vExit_AudioSrcSelectionScreen(sT_UIScreenRuntime_t *pstAudSrcSelectionScreen)
{
    ARG_UNUSED(pstAudSrcSelectionScreen);
    (void)k_work_cancel_delayable(&kWork_SubmitNewScreen);
}


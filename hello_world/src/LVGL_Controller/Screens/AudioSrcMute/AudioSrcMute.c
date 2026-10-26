#include "AudioSrcMute.h"
#include "AudioSrcMute_UIParam.h"
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

typedef struct
{
    gg_ui_t stGuiderUI;
    
    lv_obj_t *pstBluetooth_Icon;
    lv_obj_t *pstMute_Icon;
    lv_obj_t *pstSourceName;

    _Atomic bool bIsUnmuteInProgress;
} sT_AudioSrcMute_Context_t;

typedef struct
{
    struct k_work_delayable kwork_SubmitNewScreen;

    eAudioSrc_Id_t eSourceId;
    _Atomic bool bWorkCompleted;
} sT_MuteWorkContext_t;

sT_AudioSrcMute_Context_t stTAudSrcMuteCtrlContext;
sT_MuteWorkContext_t stTMuteContext;

static bool bCreate_AudioSrcMuteScreen(sT_UIScreenRuntime_t *pstAudioSrcMuteScreenRuntime);
static void vEnter_AudioSrcMuteScreen(sT_UIScreenRuntime_t *pstAudioSrcMuteScreenRuntime);
static void vExit_AudioSrcMuteScreen(sT_UIScreenRuntime_t *pstAudioSrcMuteScreenRuntime);
static void vHandle_EncoderInput(sT_UIScreenRuntime_t *pstAudioSrcMuteScreenRuntime, const sT_UIInput_t *pstTUIInput);
static void vRun_AudioSrcMuteScreen(sT_UIScreenRuntime_t *pstAudioSrcMuteScreenRuntime);

static void vHandle_ScreenSubmit( struct k_work *work);
static bool bCreate_MuteIcon(sT_AudioSrcMute_Context_t *pstScreenContext);
static bool bLoad_BluetoothIcon(sT_AudioSrcMute_Context_t *pstScreenContext);

bool bInit_AudioSrcMuteScreen(sT_UIScreenRuntime_t *pstAudioSrcMuteScreenRuntime, sT_UIScreenDescriptor_t *pstScreenDescriptor)
{
    if(pstAudioSrcMuteScreenRuntime == NULL || pstScreenDescriptor == NULL)
    {
        FHALT("Null Pointer Reference");
        return false;
    }

    memset(&stTAudSrcMuteCtrlContext, 0, sizeof(stTAudSrcMuteCtrlContext));

    pstAudioSrcMuteScreenRuntime->eScreenId = eScreen_SourceMute;
    pstAudioSrcMuteScreenRuntime->pstRootObj = NULL;
    pstAudioSrcMuteScreenRuntime->pvScreenData = &stTAudSrcMuteCtrlContext;

    pstScreenDescriptor->eScreenId = eScreen_SourceMute;
    pstScreenDescriptor->pstScreenRuntime = pstAudioSrcMuteScreenRuntime;

    pstScreenDescriptor->bDeleteOnExit = true;
    pstScreenDescriptor->pfCreate = bCreate_AudioSrcMuteScreen;
    pstScreenDescriptor->pfEnter = vEnter_AudioSrcMuteScreen;
    pstScreenDescriptor->pfExit = vExit_AudioSrcMuteScreen;
    pstScreenDescriptor->pfHandleInput = vHandle_EncoderInput;
    pstScreenDescriptor->pfRun = vRun_AudioSrcMuteScreen;

    atomic_store_explicit(&stTAudSrcMuteCtrlContext.bIsUnmuteInProgress, false, memory_order_release);
    atomic_store_explicit(&stTMuteContext.bWorkCompleted, false, memory_order_release);
    stTMuteContext.eSourceId = eNUMBER_OF_AUDIO_SOURCES;
    k_work_init_delayable(&stTMuteContext.kwork_SubmitNewScreen, vHandle_ScreenSubmit);

    return true;
}

static bool bCreate_AudioSrcMuteScreen(sT_UIScreenRuntime_t *pstAudioSrcMuteScreenRuntime)
{
    if(pstAudioSrcMuteScreenRuntime == NULL)
    {
        FHALT("Null Pointer reference for Runtime Screen Pointer for Mute Screen");
        return false;
    }

    sT_AudioSrcMute_Context_t *pstScreenContext = (sT_AudioSrcMute_Context_t *)pstAudioSrcMuteScreenRuntime->pvScreenData;
    if(pstScreenContext == NULL)
    {
        FHALT("Audio Source Selection Screen Context is Null");
        return false;        
    }

    bool bResult = false;
    lv_obj_t *pstRoot = NULL;
    
    lvgl_lock();
    if(pstAudioSrcMuteScreenRuntime->pstRootObj != NULL &&
       lv_obj_is_valid(pstAudioSrcMuteScreenRuntime->pstRootObj))
    {
        lvgl_unlock();
        return true;
    }

    pstAudioSrcMuteScreenRuntime->pstRootObj = NULL;
    memset(pstScreenContext, 0, sizeof(*pstScreenContext));

    setup_Mute_Home_revised(&pstScreenContext->stGuiderUI);
    pstRoot = pstScreenContext->stGuiderUI.Home_revised.screen;
    if(pstRoot == NULL || !lv_obj_is_valid(pstRoot))
    {
        FHALT("Invalid Audio Source Mute screen root object");
        goto Exit;
    }

    pstScreenContext->pstSourceName =
        pstScreenContext->stGuiderUI.Home_revised.Option_3_Option_label_2;
    if(pstScreenContext->pstSourceName == NULL ||
       !lv_obj_is_valid(pstScreenContext->pstSourceName))
    {
        FHALT("Invalid Audio Source Mute source-name label");
        goto Exit;
    }
    lv_obj_set_align(pstScreenContext->pstSourceName, LV_ALIGN_TOP_MID);
    lv_obj_set_x(pstScreenContext->pstSourceName, 0);

    if(!bCreate_MuteIcon(pstScreenContext))
    {
        goto Exit;
    }

    pstAudioSrcMuteScreenRuntime->pstRootObj = pstRoot;
    bResult = true;

    Exit:
    if(!bResult)
    {
        if(pstRoot != NULL && lv_obj_is_valid(pstRoot))
        {
            lv_obj_delete(pstRoot);
        }
        
        memset(pstScreenContext, 0, sizeof(*pstScreenContext));
        pstAudioSrcMuteScreenRuntime->pstRootObj = NULL;
    }

    lvgl_unlock();
    return bResult;  
}

static bool bCreate_MuteIcon(sT_AudioSrcMute_Context_t *pstScreenContext)
{
    if(pstScreenContext == NULL)
    {
        FHALT("Null Pointer Reference");
        return false;
    }

    lv_obj_t *pstMuteIconImage = NULL;
    lv_obj_t *pstMuteIconSlot = pstScreenContext->stGuiderUI.Home_revised.Options_Option_2;
    if(pstMuteIconSlot == NULL || !lv_obj_is_valid(pstMuteIconSlot))
    {
        FHALT("Invalid Back Icon container");
        return false;
    }
    lv_obj_remove_flag(pstMuteIconSlot, LV_OBJ_FLAG_SCROLLABLE);

    lv_image_header_t stTImageHeader = {0};
    lv_result_t eResult = lv_image_decoder_get_info(MUTE_ICON_IMAGE_PATH, &stTImageHeader);
    if(eResult != LV_RESULT_OK)
    {
        FHALT("Back icon validation failed: %s, result: %d",
              MUTE_ICON_IMAGE_PATH,
              (int)eResult);
        return false;        
    }

    pstMuteIconImage = lv_image_create(pstMuteIconSlot);
    if(pstMuteIconImage == NULL)
    {
        FHALT("Failed to create Back Icon image");
        return false;
    }
    
    lv_image_set_src(pstMuteIconImage, MUTE_ICON_IMAGE_PATH);
    lv_obj_set_align(pstMuteIconImage, LV_ALIGN_CENTER);
    //lv_obj_set_x(pstMuteIconImage, 6);
    pstScreenContext->pstMute_Icon = pstMuteIconImage;
    
    return true;    
}

static void vEnter_AudioSrcMuteScreen(sT_UIScreenRuntime_t *pstAudioSrcMuteScreenRuntime)
{
    if(pstAudioSrcMuteScreenRuntime == NULL || pstAudioSrcMuteScreenRuntime->pstRootObj == NULL)
    {
        FHALT("Invalid Audio Source Selection Screen Runtime Data");
        return;
    }

    sT_AudioSrcMute_Context_t *pstScreenContext = (sT_AudioSrcMute_Context_t *)pstAudioSrcMuteScreenRuntime->pvScreenData;
    if(pstScreenContext == NULL)
    {
        FHALT("Audio Source Screen Context is Null");
        return;
    }
 
    lvgl_lock();

    if(!lv_obj_is_valid(pstAudioSrcMuteScreenRuntime->pstRootObj))
    {
        lvgl_unlock();
        FHALT("Invalid Audio Source Selection Screen Root Object");
        return;        
    }

    sT_AudioSourceConfig_t stConfigSnapshot = {0};
    int iresult = iGet_SelectedAudioSrcConfig( &stConfigSnapshot );
    if(iresult != 0)
    {
        lvgl_unlock();
        return;
    }

    if(stConfigSnapshot.eSrcType == eAudioSrc_Type_Bluetooth)
    {
        if(!bLoad_BluetoothIcon(pstScreenContext))
        {
             lvgl_unlock();
            return;
        }
    }

    if(strlen(stConfigSnapshot.caSrcName) != 0)
    {
        lv_label_set_text(pstScreenContext->pstSourceName, stConfigSnapshot.caSrcName);
    }

    stTMuteContext.eSourceId = stConfigSnapshot.eSrcId;        
    lv_screen_load(pstAudioSrcMuteScreenRuntime->pstRootObj);
    lvgl_unlock();

}

static bool bLoad_BluetoothIcon(sT_AudioSrcMute_Context_t *pstScreenContext)
{
    if(pstScreenContext == NULL)
    {
        FHALT("Null Pointer Reference");
        return false;
    }

    lv_obj_t *pstBleIcon = NULL;
    lv_obj_t *pstIconSlot = pstScreenContext->stGuiderUI.Home_revised.Options_Option;
    if(pstIconSlot == NULL || !lv_obj_is_valid(pstIconSlot))
    {
        FHALT("Invalid Bluetooth icon container");
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
    pstScreenContext->pstBluetooth_Icon = pstBleIcon;

    return true;    
}

static void vExit_AudioSrcMuteScreen(sT_UIScreenRuntime_t *pstAudioSrcMuteScreenRuntime)
{
    ARG_UNUSED(pstAudioSrcMuteScreenRuntime);

    atomic_store_explicit(&stTAudSrcMuteCtrlContext.bIsUnmuteInProgress, false, memory_order_release);

    struct k_work_sync stWorkSync;
    (void)k_work_cancel_delayable_sync(&stTMuteContext.kwork_SubmitNewScreen, &stWorkSync);

    atomic_store_explicit(&stTMuteContext.bWorkCompleted, false, memory_order_release);
    stTMuteContext.eSourceId = eNUMBER_OF_AUDIO_SOURCES;
}

static void vHandle_EncoderInput(sT_UIScreenRuntime_t *pstAudioSrcMuteScreenRuntime, const sT_UIInput_t *pstTUIInput)
{
    if(pstAudioSrcMuteScreenRuntime == NULL || pstTUIInput == NULL)
    {
        FHALT("Null Pointer Reference");
        return;
    }

    if(pstTUIInput->eType != eUIInput_Press)
    {
        return;
    }

    sT_AudioSrcMute_Context_t *pstScreenContext = (sT_AudioSrcMute_Context_t *)pstAudioSrcMuteScreenRuntime->pvScreenData;
    if(pstScreenContext == NULL)
    {
        FHALT("Audio Source Selection Screen Context is Null");
        return;        
    }

    bool bResult = atomic_exchange_explicit(&pstScreenContext->bIsUnmuteInProgress, true, memory_order_acq_rel);
    if(bResult)
    {
        return;
    }

    atomic_store_explicit(&stTMuteContext.bWorkCompleted, false, memory_order_release);
    (void)k_work_schedule(&stTMuteContext.kwork_SubmitNewScreen, K_MSEC(UNMUTE_OPERATION_RESIDENCE_TIME_ms));
}

static void vHandle_ScreenSubmit( struct k_work *work)
{
    ARG_UNUSED(work);

    bool bResult = atomic_load_explicit(&stTAudSrcMuteCtrlContext.bIsUnmuteInProgress, memory_order_acquire);
    if(!bResult)
    {
        return;
    }

    if(!bUpdate_AudioSrcMuteStatus(stTMuteContext.eSourceId, false))
    {
        k_work_schedule(&stTMuteContext.kwork_SubmitNewScreen, K_MSEC(UNMUTE_OPERATION_RESIDENCE_TIME_ms));
        return;
    }

    atomic_store_explicit(&stTMuteContext.bWorkCompleted, true, memory_order_release);
}

static void vRun_AudioSrcMuteScreen(sT_UIScreenRuntime_t *pstAudioSrcMuteScreenRuntime)
{
    ARG_UNUSED(pstAudioSrcMuteScreenRuntime);

    bool bCompleted = atomic_load_explicit(&stTMuteContext.bWorkCompleted, memory_order_acquire);
    if(bCompleted)
    {        
        if(!bRequest_UIScreen(eScreen_AudioSrcInfo))
        {
            atomic_store_explicit(&stTMuteContext.bWorkCompleted, false, memory_order_release);
            k_work_schedule(&stTMuteContext.kwork_SubmitNewScreen, K_MSEC(UNMUTE_OPERATION_RESIDENCE_TIME_ms));
            return;        
        }

        atomic_store_explicit(&stTAudSrcMuteCtrlContext.bIsUnmuteInProgress, false, memory_order_release);
        atomic_store_explicit(&stTMuteContext.bWorkCompleted, false, memory_order_release);
    }
}

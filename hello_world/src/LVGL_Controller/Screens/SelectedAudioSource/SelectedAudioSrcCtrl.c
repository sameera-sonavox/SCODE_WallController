#include "SelectedAudioSrcCtrl.h"
#include "../../UI_ScreenManager/UI_ScreenManager.h"
#include "../../UI_InputController/UI_InputController.h"
#include "../../LVGL_Display_Controller.h"
#include "SelectedAudioSrc_UIParam.h"
#include "SelectedAudioSrcView.h"
#include "SelectedAudioSrc_Bluetooth/SelectedAudioSrcBluetooth_View.h"
#include "SelectedAudioSrc_Wired/SelectedAudioSrcWired_View.h"
#include "GenericMacro.h"
#include "../../../ExtFlash_Controller/AudioSrcConfiguration/AudioSrcConfiguration.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <lvgl_zephyr.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct
{
    eAudioSrc_Id_t eSrcId;
    eAudioSrc_Type_t eSrcType;
    const sT_SelectedAudioSrcViewOperations_t *pstViewOperations;
    sT_SelectedAudioSrcViewObjects_t stViewObjects;

    lv_obj_t *pstVolSlider;
    lv_obj_t *pstVolLabel;
    lv_obj_t *pstAudSrcNameLabel;
    char caVolumeText[4];

    uint8_t uiVolume;
    uint8_t uiShortPressCount;
    uint32_t uiButtonPressStart_ms;
    uint32_t uiLastShortRelease_ms;

    uint32_t uiVolLastChange_ms;
    uint32_t uiVolDirtySince_ms;
    bool bIsVolumeDirty;

    bool bButtonPressed;
    bool bLongPressHandled;

    _Atomic bool bIsMuteTransitionPending;
    _Atomic bool bIsMuteTransitionReady;
    uint32_t uiMutePendingSince_ms;
    uint32_t uiMuteTransitionDelay_ms;
} sT_AudioSrcScreenContext_t;

static sT_AudioSrcScreenContext_t stTAudioSrcScreenContext;

static bool bCreate_AudioSrcInfoScreen(sT_UIScreenRuntime_t *pstAudioSrcScreen);
static void vEnter_AudioSrcInfoScreen(sT_UIScreenRuntime_t *pstAudioSrcScreen);
static void vExit_AudioSrcInfoScreen(sT_UIScreenRuntime_t *pstAudioSrcScreen);
static void vRun_AudioSrcInfoScreen(sT_UIScreenRuntime_t *pstAudioSrcScreen);

static bool bUpdate_AudioSourceInfo(sT_AudioSrcScreenContext_t *pstScreenContext);
static int iUpdate_AudioSrcFile(sT_AudioSrcScreenContext_t *pstScreenContext, bool bForceUpdate);
static const sT_SelectedAudioSrcViewOperations_t *pstSelect_AudioSrcView(eAudioSrc_Type_t eSourceType);
static void vHandle_SourceLongPress(sT_AudioSrcScreenContext_t *pstScreenContext);

static void vHandle_EncoderInput(sT_UIScreenRuntime_t *pstAudioSrcScreen, const sT_UIInput_t *pstTUIInput);
static void vHandle_EncoderRotation(sT_UIScreenRuntime_t *pstAudioSrcScreen, const sT_UIInput_t *pstTUIInput);
static void vHandle_EncoderButonPress(sT_UIScreenRuntime_t *pstAudioSrcScreen, const sT_UIInput_t *pstTUIInput);
static bool bCanExecute_MuteScreenTransition(sT_AudioSrcScreenContext_t *pstScreenContext);

bool bInit_AudioSrcInfoScreen(sT_UIScreenRuntime_t *pstAudioSrcScreen, sT_UIScreenDescriptor_t *pstScreenDescriptor)
{
    if(pstAudioSrcScreen == NULL || pstScreenDescriptor == NULL)
    {
        FHALT("Null Pointer Reference");
        return false;
    }

    memset(&stTAudioSrcScreenContext, 0, sizeof(stTAudioSrcScreenContext));
    memset(&stTAudioSrcScreenContext.caVolumeText, '\0', sizeof(stTAudioSrcScreenContext.caVolumeText));

    pstAudioSrcScreen->eScreenId = eScreen_AudioSrcInfo;
    pstAudioSrcScreen->pstRootObj = NULL;
    pstAudioSrcScreen->pvScreenData = &stTAudioSrcScreenContext;

    pstScreenDescriptor->eScreenId = eScreen_AudioSrcInfo;
    pstScreenDescriptor->pstScreenRuntime = pstAudioSrcScreen;

    pstScreenDescriptor->bDeleteOnExit = true;
    pstScreenDescriptor->pfCreate = bCreate_AudioSrcInfoScreen;
    pstScreenDescriptor->pfEnter = vEnter_AudioSrcInfoScreen;
    pstScreenDescriptor->pfExit = vExit_AudioSrcInfoScreen;
    pstScreenDescriptor->pfHandleInput = vHandle_EncoderInput;
    pstScreenDescriptor->pfRun = vRun_AudioSrcInfoScreen;

    atomic_store_explicit(&stTAudioSrcScreenContext.bIsMuteTransitionPending, false, memory_order_release);
    atomic_store_explicit(&stTAudioSrcScreenContext.bIsMuteTransitionReady, false, memory_order_release);

    return true;    
}

static void vRun_AudioSrcInfoScreen(sT_UIScreenRuntime_t *pstAudioSrcScreen)
{
    if ((pstAudioSrcScreen == NULL) ||
        (pstAudioSrcScreen->pvScreenData == NULL))
    {
        FHALT("Audio source screen context is null");
        return;
    }

    sT_AudioSrcScreenContext_t *pstScreenContext = (sT_AudioSrcScreenContext_t *)pstAudioSrcScreen->pvScreenData;
    if(pstScreenContext == NULL)
    {
        FHALT("Audio Source Screen Context is Null");
        return;
    }

    (void)iUpdate_AudioSrcFile(pstScreenContext, false);

    if(bCanExecute_MuteScreenTransition(pstScreenContext))
    {
        return;
    }

    const uint32_t uiCurrentTime_ms = k_uptime_get_32();
    if(pstScreenContext->bButtonPressed && !pstScreenContext->bLongPressHandled)
    {
        uint32_t uiHeldtime_ms = uiCurrentTime_ms - pstScreenContext->uiButtonPressStart_ms;
        if(uiHeldtime_ms >= ENCODER_SWITCH_HOLD_TIME_ms)
        {
            pstScreenContext->bLongPressHandled = true;
            pstScreenContext->uiShortPressCount = 0U;
            pstScreenContext->uiLastShortRelease_ms = 0U;

            vHandle_SourceLongPress(pstScreenContext);
        }
    }

    if(!pstScreenContext->bButtonPressed && pstScreenContext->uiShortPressCount == 1U)
    {
        const uint32_t uiWaittime_ms =
            uiCurrentTime_ms - pstScreenContext->uiLastShortRelease_ms;

        if(uiWaittime_ms > ENCODER_DOUBLE_PRESS_WINDOW_ms)
        {
            pstScreenContext->uiShortPressCount = 0U;
            pstScreenContext->uiLastShortRelease_ms = 0U;
        }
    }
}

static bool bCanExecute_MuteScreenTransition(sT_AudioSrcScreenContext_t *pstScreenContext)
{
    if(pstScreenContext == NULL)
    {
        FHALT("Null Pointer Reference");
        return false;
    }

    const bool bIsMuteTransitPending = atomic_load_explicit(&pstScreenContext->bIsMuteTransitionPending,
                                                            memory_order_acquire);
    if(!bIsMuteTransitPending || pstScreenContext->uiVolume != 0U)
    {
        return false;
    }

    bool bIsMuteTransitionReady = atomic_load_explicit(&pstScreenContext->bIsMuteTransitionReady,
                                                        memory_order_acquire);
    if(!bIsMuteTransitionReady)
    {
        /* Do not enter the mute screen until volume zero is stored. */
        if(pstScreenContext->bIsVolumeDirty)
        {
            return false;
        }

        const uint32_t uiElapsedTime_ms = k_uptime_get_32() - pstScreenContext->uiMutePendingSince_ms;
        if(uiElapsedTime_ms < pstScreenContext->uiMuteTransitionDelay_ms)
        {
            return false;
        }

        atomic_store_explicit(
            &pstScreenContext->bIsMuteTransitionReady,
            true,
            memory_order_release);
    }

    if(!bUpdate_AudioSrcMuteStatus(pstScreenContext->eSrcId, true))
    {
        return false;
    }
    if(!bRequest_UIScreen(eScreen_SourceMute))
    {
        return false;
    }

    return true;
}

static int iUpdate_AudioSrcFile(sT_AudioSrcScreenContext_t *pstScreenContext, bool bForceUpdate)
{
    if(pstScreenContext == NULL)
    {
        return -EINVAL;
    }
    if(!pstScreenContext->bIsVolumeDirty)
    {
        return 0;
    }

    const uint32_t uiTimeNow_ms = k_uptime_get_32();
    const uint32_t uiIdleTime_ms = uiTimeNow_ms - pstScreenContext->uiVolLastChange_ms;
    const uint32_t uiDirtyTime_ms = uiTimeNow_ms - pstScreenContext->uiVolDirtySince_ms;

    if(!bForceUpdate && 
       (uiIdleTime_ms < AUDIO_VOLUME_SAVE_DELAY_ms) && 
       (uiDirtyTime_ms < AUDIO_VOLUME_MAX_DIRTY_TIME_ms))
    {
        return 0;
    }

    int iResult = iUpdate_AudioSrcVolume(pstScreenContext->eSrcId, pstScreenContext->uiVolume);
    if(iResult != 0)
    {
        pstScreenContext->uiVolLastChange_ms = uiTimeNow_ms;
        pstScreenContext->uiVolDirtySince_ms = uiTimeNow_ms;
        return iResult;
    }
   
    pstScreenContext->bIsVolumeDirty = false;
    return 0;
}

static bool bCreate_AudioSrcInfoScreen(sT_UIScreenRuntime_t *pstAudioSrcScreen)
{
    if(pstAudioSrcScreen == NULL)
    {
        FHALT("Null Pointer Reference");
        return false;
    }

    sT_AudioSrcScreenContext_t *pstScreenContext = (sT_AudioSrcScreenContext_t *)pstAudioSrcScreen->pvScreenData;
    if(pstScreenContext == NULL)
    {
        FHALT("Audio Source Screen Context is Null");
        return false;
    }

    sT_AudioSourceConfig_t stSelectedAudioSrcConfig = {0};
    //Get the selected Audio Source Configurations
    if(iGet_SelectedAudioSrcConfig(&stSelectedAudioSrcConfig) != 0)
    {
        FHALT("Selected Audio Source Configuration is empty");
        return false;
    }

    const sT_SelectedAudioSrcViewOperations_t *pstViewOperations = pstSelect_AudioSrcView(stSelectedAudioSrcConfig.eSrcType);
    if(pstViewOperations == NULL || pstViewOperations->pfCreate == NULL)
    {
        FHALT("No selected-source view for source type: %d",
              stSelectedAudioSrcConfig.eSrcType);
        return false;
    }

    bool bResult = false;
    lv_obj_t *pstRoot = NULL;

    lvgl_lock();
    if(pstAudioSrcScreen->pstRootObj != NULL &&
       lv_obj_is_valid(pstAudioSrcScreen->pstRootObj))
    {
        lvgl_unlock();
        return true;
    }

    pstAudioSrcScreen->pstRootObj = NULL;
    memset(pstScreenContext, 0, sizeof(*pstScreenContext));

    pstScreenContext->eSrcType = stSelectedAudioSrcConfig.eSrcType;
    pstScreenContext->pstViewOperations = pstViewOperations;

    if(!pstViewOperations->pfCreate(&pstScreenContext->stViewObjects))
    {
        pstRoot = pstScreenContext->stViewObjects.pstRoot;
        goto Exit;
    }

    pstRoot = pstScreenContext->stViewObjects.pstRoot;
    
    pstScreenContext->pstVolSlider = pstScreenContext->stViewObjects.pstVolumeControl;
    pstScreenContext->pstVolLabel = pstScreenContext->stViewObjects.pstVolumeLabel;
    pstScreenContext->pstAudSrcNameLabel = pstScreenContext->stViewObjects.pstSourceNameLabel;

    pstAudioSrcScreen->pstRootObj = pstRoot;
    bResult = true;

    Exit:

    if(!bResult)
    {
        if(pstRoot != NULL && lv_obj_is_valid(pstRoot))
        {
            lv_obj_delete(pstRoot);
        }
        memset(pstScreenContext, 0, sizeof(*pstScreenContext));
        pstAudioSrcScreen->pstRootObj = NULL;
    }

    lvgl_unlock();
    return bResult;
}

static void vHandle_EncoderInput(sT_UIScreenRuntime_t *pstAudioSrcScreen, const sT_UIInput_t *pstTUIInput)
{
    if(pstTUIInput == NULL || pstAudioSrcScreen == NULL)
    {
        FHALT("Null Pointer Reference");
        return;
    }

    switch(pstTUIInput->eType)
    {
        case eUIInput_Rotate:
            vHandle_EncoderRotation(pstAudioSrcScreen, pstTUIInput);
            break;
        case eUIInput_Press:
        case eUIInput_Release:
            vHandle_EncoderButonPress(pstAudioSrcScreen, pstTUIInput);
            break;
        default:
            FHALT("Invalid Encoder Event Type: %d", pstTUIInput->eType);
            break;
    }
}

static void vHandle_EncoderRotation(sT_UIScreenRuntime_t *pstAudioSrcScreen, const sT_UIInput_t *pstTUIInput)
{
    if(pstTUIInput == NULL || pstAudioSrcScreen == NULL)
    {
        FHALT("Null Pointer Reference");
        return;
    }

    sT_AudioSrcScreenContext_t *pstScreenContext = (sT_AudioSrcScreenContext_t *)pstAudioSrcScreen->pvScreenData;
    if(pstScreenContext == NULL)
    {
        FHALT("Audio Source Screen Context is Null");
        return;
    }
    
    int32_t iVol = pstScreenContext->uiVolume;
    
    iVol += pstTUIInput->iDetents;
    if(iVol < 0)
    {
        iVol = 0;
    }
    else if(iVol >= 100)
    {
        iVol = 100;
    }

    uint8_t uiPrevious_Volume = pstScreenContext->uiVolume;
    pstScreenContext->uiVolume = (uint8_t)iVol;

    if(uiPrevious_Volume == pstScreenContext->uiVolume)
    {
        return;
    }

    const uint32_t uiTimeNow_ms = k_uptime_get_32();

    if(uiPrevious_Volume > 0U && pstScreenContext->uiVolume == 0U)
    {
        pstScreenContext->uiMutePendingSince_ms = uiTimeNow_ms;
        pstScreenContext->uiMuteTransitionDelay_ms =
            AUDIO_MUTE_TRANSITION_AFTER_ZERO_DELAY_ms;
        atomic_store_explicit(&pstScreenContext->bIsMuteTransitionPending, true, memory_order_release);
        atomic_store_explicit(&pstScreenContext->bIsMuteTransitionReady, false, memory_order_release);
    }
    else if(pstScreenContext->uiVolume > 0U)
    {
        pstScreenContext->uiMutePendingSince_ms = 0U;
        pstScreenContext->uiMuteTransitionDelay_ms = 0U;
        atomic_store_explicit(&pstScreenContext->bIsMuteTransitionPending, false, memory_order_release);
        atomic_store_explicit(&pstScreenContext->bIsMuteTransitionReady, false, memory_order_release);        
    }

    lv_slider_set_value(pstScreenContext->pstVolSlider, pstScreenContext->uiVolume, LV_ANIM_ON);
    if(pstScreenContext->pstVolLabel != NULL)
    {
        (void)snprintf(pstScreenContext->caVolumeText, sizeof(pstScreenContext->caVolumeText),
                        "%u",
                        (unsigned int)pstScreenContext->uiVolume);
        lv_label_set_text_static(pstScreenContext->pstVolLabel, pstScreenContext->caVolumeText);
    }

    if(!pstScreenContext->bIsVolumeDirty)
    {
        pstScreenContext->uiVolDirtySince_ms = uiTimeNow_ms;
    }

    pstScreenContext->uiVolLastChange_ms = uiTimeNow_ms;
    pstScreenContext->bIsVolumeDirty = true;
    
}

static void vHandle_EncoderButonPress(sT_UIScreenRuntime_t *pstAudioSrcScreen, const sT_UIInput_t *pstTUIInput)
{
    if(pstTUIInput == NULL || pstAudioSrcScreen == NULL)
    {
        FHALT("Null Pointer Reference");
        return;
    }

    sT_AudioSrcScreenContext_t *pstScreenContext = (sT_AudioSrcScreenContext_t *)pstAudioSrcScreen->pvScreenData;
    if(pstScreenContext == NULL)
    {
        FHALT("Audio Source Screen Context is Null");
        return;
    }

    if(pstTUIInput->eType == eUIInput_Press)
    {
        if(!pstScreenContext->bButtonPressed)
        {
            pstScreenContext->bButtonPressed = true;
            pstScreenContext->bLongPressHandled = false;
            pstScreenContext->uiButtonPressStart_ms = pstTUIInput->uiTimestamp_ms;
        }
        return;
    }

    if(pstTUIInput->eType != eUIInput_Release)
    {
        return;
    }
    if(!pstScreenContext->bButtonPressed)
    {
        return;
    }

    pstScreenContext->bButtonPressed = false;
    if(pstScreenContext->bLongPressHandled)
    {
        pstScreenContext->bLongPressHandled = false;
        pstScreenContext->uiButtonPressStart_ms = 0U;
        return;
    }

    const uint32_t uiReleaseTime_ms = pstTUIInput->uiTimestamp_ms;
    const uint32_t uiPressDurationTime_ms = uiReleaseTime_ms - pstScreenContext->uiButtonPressStart_ms;
    pstScreenContext->uiButtonPressStart_ms = 0U;

    if(uiPressDurationTime_ms >= ENCODER_SWITCH_HOLD_TIME_ms)
    {
        pstScreenContext->uiShortPressCount = 0U;
        pstScreenContext->uiLastShortRelease_ms = 0U;
        vHandle_SourceLongPress(pstScreenContext);
        return;
    }

    if ((pstScreenContext->uiShortPressCount == 0U) ||
        ((uiReleaseTime_ms -
          pstScreenContext->uiLastShortRelease_ms) >
         ENCODER_DOUBLE_PRESS_WINDOW_ms))
    {
        pstScreenContext->uiShortPressCount = 1U;
        pstScreenContext->uiLastShortRelease_ms = uiReleaseTime_ms;
        return;
    }

    /*
     * Second short release inside the double-press window.
     */
    pstScreenContext->uiShortPressCount = 0U;
    pstScreenContext->uiLastShortRelease_ms = 0U;

    printk("Double press detected\n\r");

    if (!bRequest_UIScreen(eScreen_SourceSelect))
    {
        printk("Source Select screen is not registered\n\r");
        return;
    }

    printk("Next Window Requested\n\r");
}

static void vEnter_AudioSrcInfoScreen(sT_UIScreenRuntime_t *pstAudioSrcScreen)
{
    if(pstAudioSrcScreen == NULL || pstAudioSrcScreen->pstRootObj == NULL)
    {
        FHALT("Invalid Welcome Screen Runtime Data");
        return;
    }

    sT_AudioSrcScreenContext_t *pstScreenContext = (sT_AudioSrcScreenContext_t *)pstAudioSrcScreen->pvScreenData;
    if(pstScreenContext == NULL)
    {
        FHALT("Audio Source Screen Context is Null");
        return;
    }
    if(pstScreenContext->bIsVolumeDirty)
    {
        int iResult = iUpdate_AudioSrcFile(pstScreenContext, true);
        if(iResult != 0)
        {
            FHALT("Pending volume update failed: %d", iResult);
            return;
        }
    }

    lvgl_lock();

    if(!lv_obj_is_valid(pstAudioSrcScreen->pstRootObj))
    {
        lvgl_unlock();
        FHALT("Invalid Welcome Screen Root Object");
        return;        
    }

    if(!bUpdate_AudioSourceInfo(pstScreenContext))
    {
        lvgl_unlock();
        return;
    }

    if(pstScreenContext->uiVolume == 0U)
    {
        pstScreenContext->uiMutePendingSince_ms = k_uptime_get_32();
        pstScreenContext->uiMuteTransitionDelay_ms =
            AUDIO_MUTE_RETRANSITION_DELAY_ms;
        atomic_store_explicit(&pstScreenContext->bIsMuteTransitionPending, true, memory_order_release);
        atomic_store_explicit(&pstScreenContext->bIsMuteTransitionReady, false, memory_order_release);
    }
    else
    {
        pstScreenContext->uiMutePendingSince_ms = 0U;
        pstScreenContext->uiMuteTransitionDelay_ms = 0U;
        atomic_store_explicit(&pstScreenContext->bIsMuteTransitionPending, false, memory_order_release);
        atomic_store_explicit(&pstScreenContext->bIsMuteTransitionReady, false, memory_order_release);
    }
    
    lv_screen_load(pstAudioSrcScreen->pstRootObj);
    lvgl_unlock();
    
}

static bool bUpdate_AudioSourceInfo(sT_AudioSrcScreenContext_t *pstScreenContext)
{
    sT_AudioSourceConfig_t stSelectedAudioSrcConfig = {0};
    int iresult = iGet_SelectedAudioSrcConfig(&stSelectedAudioSrcConfig);
    if(iresult != 0)
    {
        FHALT("Selected Audio Source Configuration is empty");
        return false;        
    }

    pstScreenContext->uiVolume = stSelectedAudioSrcConfig.uiVolume;
    pstScreenContext->eSrcId = stSelectedAudioSrcConfig.eSrcId;

    if(stSelectedAudioSrcConfig.eSrcType != pstScreenContext->eSrcType)
    {
        FHALT("Selected source type changed while its view was active: %d -> %d",
              pstScreenContext->eSrcType,
              stSelectedAudioSrcConfig.eSrcType);
        return false;
    }

    pstScreenContext->bIsVolumeDirty = false;
    pstScreenContext->uiVolDirtySince_ms = 0U;
    pstScreenContext->uiVolLastChange_ms = 0U;

    if(pstScreenContext->pstAudSrcNameLabel != NULL)
    {
        lv_label_set_text(pstScreenContext->pstAudSrcNameLabel, stSelectedAudioSrcConfig.caSrcName);
    }

    if(pstScreenContext->pstVolSlider != NULL)
    {
        lv_slider_set_value(pstScreenContext->pstVolSlider, pstScreenContext->uiVolume, LV_ANIM_OFF);
    }
    
    if(pstScreenContext->pstVolLabel != NULL)
    {
        (void)snprintf(pstScreenContext->caVolumeText, sizeof(pstScreenContext->caVolumeText),
                        "%u",
                        (unsigned int)pstScreenContext->uiVolume);
        lv_label_set_text_static(pstScreenContext->pstVolLabel, pstScreenContext->caVolumeText);
    }

    return true;
}

static const sT_SelectedAudioSrcViewOperations_t *pstSelect_AudioSrcView(eAudioSrc_Type_t eSourceType)
{
    switch(eSourceType)
    {
        case eAudioSrc_Type_Bluetooth:
            return pstGet_SelectedAudioSrcBluetoothView();

        case eAudioSrc_Type_Wired:
            return pstGet_SelectedAudioSrcWiredView();

        case eAudioSrc_Type_None:
        case eNUMBER_OF_AUDIO_SRC_TYPEs:
        default:
            return NULL;
    }
}

static void vHandle_SourceLongPress(sT_AudioSrcScreenContext_t *pstScreenContext)
{
    if(pstScreenContext == NULL ||
       pstScreenContext->pstViewOperations == NULL ||
       pstScreenContext->pstViewOperations->pfHandleLongPress == NULL)
    {
        return;
    }

    pstScreenContext->pstViewOperations->pfHandleLongPress();
}

static void vExit_AudioSrcInfoScreen(sT_UIScreenRuntime_t *pstAudioSrcScreen)
{ 
    sT_AudioSrcScreenContext_t *pstScreenContext = (sT_AudioSrcScreenContext_t *)pstAudioSrcScreen->pvScreenData;
    if(pstScreenContext == NULL)
    {
        FHALT("Audio Source Screen Context is Null");
        return;
    }

    int iResult = iUpdate_AudioSrcFile(pstScreenContext, true);
    if(iResult != 0)
    {
        FHALT("Volume update failed during screen exit: %d", iResult);    
    }
    
    atomic_store_explicit(&pstScreenContext->bIsMuteTransitionPending, false, memory_order_release);
    atomic_store_explicit(&pstScreenContext->bIsMuteTransitionReady, false, memory_order_release);

    pstScreenContext->bButtonPressed = false;
    pstScreenContext->bLongPressHandled = false;
    
    pstScreenContext->uiLastShortRelease_ms = 0U;
    pstScreenContext->uiButtonPressStart_ms = 0U;
    pstScreenContext->uiShortPressCount = 0U;

}




#include "BluetoothControl.h"
#include "BluetoothControl_UIParam.h"
#include "../../LVGL_Display_Controller.h"
#include "GenericMacro.h"
#include "../../../WallController/WallController_StateMachine/Wallcontroller_StateMachine.h"
#include "GUI_Guider_Generated/generated/gui_guider.h"
#include "../../../ExtFlash_Controller/AudioSrcConfiguration/AudioSrcConfiguration.h"
#include "../../UI_ScreenManager/UI_ScreenManager.h"
#include "../../LVGL_TextAnimator/LVGL_TextAnimate.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <lvgl_zephyr.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/sys/util.h>


typedef struct
{
    gg_ui_t stGuiderUI;

    lv_obj_t *pstBleConnIcon;
    lv_obj_t *pstBleConText;

    eBLEState_t eCurrentBleState;
    bool bAnimationInProgress;
} sT_BluetoothControl_Context_t;

static const char BleConnectionText[eNUMBER_OF_BLE_STATEs][24] = {
    [eBleState_None] = "",
    [eBleState_Pairing] = "Pairing",
    [eBleState_Disconnecting] = "Disconnecting",
    [eBleState_Disconnected] = "Disconnected",
    [eBleState_Connected] = "Connected"
};

static const char * const pcaAnimateString[] = {
    "",
    ".",
    "..",
    "...",
};

sT_BluetoothControl_Context_t stTBluetoothCtrlContext;

static bool bCreate_BluetoothConnControlScreen(sT_UIScreenRuntime_t *pstBleControlScreen);
static void vEnter_BluetoothConnControlScreen(sT_UIScreenRuntime_t *pstBleControlScreen);
static void vExit_BluetoothConnControlScreen(sT_UIScreenRuntime_t *pstBleControlScreen);
static void vHandle_EncoderInput(sT_UIScreenRuntime_t *pstBleControlScreen, const sT_UIInput_t *pstTUIInput);
static void vRun_BluetoothConnControlScreen(sT_UIScreenRuntime_t *pstBleControlScreen);

static bool bCreate_BluetoothIcon(sT_BluetoothControl_Context_t *pstScreenContext);
static void vUpdate_BLuetoothConnection_UI( sT_UIScreenRuntime_t *pstBleControlScreen );
static void vTextAnimator_Callback(const char *pcaUpdateText);
static void vStop_BluetoothIconAnimation(sT_BluetoothControl_Context_t *pstScreenContext);
static void vStart_BluetoothPairingAnimation(sT_BluetoothControl_Context_t *pstScreenContext);
static void vSet_BluetoothIconOpacity( void *pvObject, int32_t iOpacity);

bool bInit_BluetoothControlScreen(sT_UIScreenRuntime_t *pstBleControlScreen, 
                                  sT_UIScreenDescriptor_t *pstScreenDescriptor)
{
    if(pstBleControlScreen == NULL || pstScreenDescriptor == NULL)
    {
        FHALT("Null Pointer Reference");
        return false;
    }

    memset(&stTBluetoothCtrlContext, 0, sizeof(stTBluetoothCtrlContext));

    pstBleControlScreen->eScreenId = eScreen_BluetoothControl;
    pstBleControlScreen->pstRootObj = NULL;
    pstBleControlScreen->pvScreenData = &stTBluetoothCtrlContext;

    pstScreenDescriptor->eScreenId = eScreen_BluetoothControl;
    pstScreenDescriptor->pstScreenRuntime = pstBleControlScreen;
    pstScreenDescriptor->bDeleteOnExit = true;
    pstScreenDescriptor->pfCreate = bCreate_BluetoothConnControlScreen;
    pstScreenDescriptor->pfEnter = vEnter_BluetoothConnControlScreen;
    pstScreenDescriptor->pfExit = vExit_BluetoothConnControlScreen;
    pstScreenDescriptor->pfHandleInput = vHandle_EncoderInput;
    pstScreenDescriptor->pfRun = vRun_BluetoothConnControlScreen;

    stTBluetoothCtrlContext.bAnimationInProgress = false;
    return true;
}

static bool bCreate_BluetoothConnControlScreen(sT_UIScreenRuntime_t *pstBleControlScreen)
{
    if(pstBleControlScreen == NULL)
    {
        FHALT("Null Pointer Reference");
        return false;
    }

    sT_BluetoothControl_Context_t *pstScreenContext = (sT_BluetoothControl_Context_t *)pstBleControlScreen->pvScreenData;
    if(pstScreenContext == NULL)
    {
        FHALT("Bluetooth Menu Screen Context is Null");
        return false;
    }

    bool bResult = false;
    lv_obj_t *pstRoot = NULL;

    lvgl_lock();

    if(pstBleControlScreen->pstRootObj != NULL &&
       lv_obj_is_valid(pstBleControlScreen->pstRootObj))
    {
        lvgl_unlock();
        return true;
    }

    pstBleControlScreen->pstRootObj = NULL;
    memset(pstScreenContext, 0, sizeof(*pstScreenContext));

    setup_Connected_revised(&pstScreenContext->stGuiderUI);
    pstRoot = pstScreenContext->stGuiderUI.Connected_revised.screen;

    if(pstRoot == NULL ||
       pstScreenContext->stGuiderUI.Connected_revised.Options_Option_2 == NULL ||
       pstScreenContext->stGuiderUI.Connected_revised.Option_3_Option_label_2 == NULL)
    {
        FHALT("GUI Guider failed to create the Bluetooth menu objects");
        goto Exit;
    }

    pstScreenContext->pstBleConText = pstScreenContext->stGuiderUI.Connected_revised.Option_3_Option_label_2;

    if(!bCreate_BluetoothIcon(pstScreenContext))
    {
        goto Exit;
    }

    pstBleControlScreen->pstRootObj = pstRoot;
    bResult = true;

Exit:
    if(!bResult)
    {
        if(pstRoot != NULL && lv_obj_is_valid(pstRoot))
        {
            lv_obj_delete(pstRoot);
        }

        memset(pstScreenContext, 0, sizeof(*pstScreenContext));
        pstBleControlScreen->pstRootObj = NULL;
    }

    lvgl_unlock();
    return bResult; 
}

static bool bCreate_BluetoothIcon(sT_BluetoothControl_Context_t *pstScreenContext)
{
    if(pstScreenContext == NULL)
    {
        FHALT("Null Pointer Reference");
        return false;
    }

    lv_obj_t *pstBleIcon = NULL;
    lv_obj_t *pstIconSlot = pstScreenContext->stGuiderUI.Connected_revised.Options_Option_2;
    if(pstIconSlot == NULL || !lv_obj_is_valid(pstIconSlot))
    {
        FHALT("Invalid Bluetooth icon container");
        return false;
    }

    lv_obj_remove_flag(pstIconSlot, LV_OBJ_FLAG_SCROLLABLE);

    lv_image_header_t stTImageHeader = {0};
    lv_result_t eResult = lv_image_decoder_get_info(BLUETOOTH_CONNECTION_ICON_IMAGE_PATH, &stTImageHeader);
    if(eResult != LV_RESULT_OK)
    {
        FHALT("Welcome image validation failed: %s, result: %d",
              BLUETOOTH_CONNECTION_ICON_IMAGE_PATH,
              (int)eResult);
        return false;        
    }

    pstBleIcon = lv_image_create(pstIconSlot);
    if(pstBleIcon == NULL)
    {
        FHALT("Failed to create Welcome logo image");
        return false;
    }
    
    lv_image_set_src(pstBleIcon, BLUETOOTH_CONNECTION_ICON_IMAGE_PATH);
    lv_obj_set_align(pstBleIcon, LV_ALIGN_CENTER);
    pstScreenContext->pstBleConnIcon = pstBleIcon;

    return true;    
}

static void vEnter_BluetoothConnControlScreen(sT_UIScreenRuntime_t *pstBleControlScreen)
{
    if(pstBleControlScreen == NULL || pstBleControlScreen->pstRootObj == NULL)
    {
        FHALT("Invalid Bluetooth Menu Screen Runtime Data");
        return;
    }

    sT_BluetoothControl_Context_t *pstScreenContext = (sT_BluetoothControl_Context_t *)pstBleControlScreen->pvScreenData;
    if(pstScreenContext == NULL)
    {
        FHALT("Bluetooth Menu Screen Context is Null");
        return;
    }

    lvgl_lock();

    if(!lv_obj_is_valid(pstBleControlScreen->pstRootObj))
    {
        lvgl_unlock();
        FHALT("Invalid Bluetooth Menu Screen Root Object");
        return;
    }

    vUpdate_BLuetoothConnection_UI(pstBleControlScreen);

    lv_screen_load(pstBleControlScreen->pstRootObj);

    lvgl_unlock();
}

static void vUpdate_BLuetoothConnection_UI( sT_UIScreenRuntime_t *pstBleControlScreen )
{
    if(pstBleControlScreen == NULL)
    {
        FHALT("Null Pointer Refrence");
        return;
    }

    sT_BluetoothControl_Context_t *pstScreenContext = (sT_BluetoothControl_Context_t *)pstBleControlScreen->pvScreenData;
    if(pstScreenContext == NULL)
    {
        FHALT("Bluetooth Menu Screen Context is Null");
        return;
    }

    eBLEState_t eState = eGet_BleConnectionState();
    lv_obj_t *pstLabel = pstScreenContext->pstBleConText;
    if(pstLabel == NULL || !lv_obj_is_valid(pstLabel) ||
       pstScreenContext->pstBleConnIcon == NULL ||
       !lv_obj_is_valid(pstScreenContext->pstBleConnIcon))
    {
        FHALT("Invalid Bluetooth connection UI object");
        return;        
    }

    if(eState <= eBleState_None || eState >= eNUMBER_OF_BLE_STATEs)
    {
        FHALT("Invalid Bluetooth state: %d", eState);
        return;
    }

    lv_label_set_text(pstLabel, BleConnectionText[eState]);

    switch (eState)
    {
        case eBleState_Pairing:
            lv_obj_set_style_image_recolor(pstScreenContext->pstBleConnIcon, lv_color_hex(BLUETOOTH_PAIRING_COLOR), LV_PART_MAIN);
            lv_obj_set_style_image_recolor_opa(pstScreenContext->pstBleConnIcon, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_text_color(pstLabel, lv_color_hex(BLUETOOTH_PAIRING_COLOR), LV_PART_MAIN);
            break;
        case eBleState_Disconnecting:
        case eBleState_Disconnected:
            lv_obj_set_style_image_recolor(pstScreenContext->pstBleConnIcon, lv_color_hex(BLUETOOTH_DISCONNECTED_COLOR), LV_PART_MAIN);
            lv_obj_set_style_image_recolor_opa(pstScreenContext->pstBleConnIcon, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_text_color(pstLabel, lv_color_hex(BLUETOOTH_DISCONNECTED_COLOR), LV_PART_MAIN);
            break;
        case eBleState_Connected:
            lv_obj_set_style_image_recolor(pstScreenContext->pstBleConnIcon, lv_color_hex(BLUETOOTH_CONNECTED_COLOR), LV_PART_MAIN);
            lv_obj_set_style_image_recolor_opa(pstScreenContext->pstBleConnIcon, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_text_color(pstLabel, lv_color_hex(BLUETOOTH_CONNECTED_COLOR), LV_PART_MAIN);
            break;   
        default:
            break;
    }

    if(eState == eBleState_Pairing)
    {
        vStart_BluetoothPairingAnimation(pstScreenContext);
        vStart_TextAnimator(pcaAnimateString, BleConnectionText[eState], 
                            ARRAY_SIZE(pcaAnimateString), vTextAnimator_Callback, 
                            BLE_PAIRING_TEXT_ANIMATE_DELAY_ms);
        pstScreenContext->bAnimationInProgress = true;
    }

    pstScreenContext->eCurrentBleState = eState;    
}

static void vTextAnimator_Callback(const char *pcaUpdateText)
{
    if(pcaUpdateText == NULL)
    {
        FHALT("Text Animator Populated String is Null");
        return;
    }

    lv_obj_t *pstLabel = stTBluetoothCtrlContext.pstBleConText;
    if(pstLabel == NULL || !lv_obj_is_valid(pstLabel))
    {
        FHALT("Label is Null");
        return;        
    }

    lv_label_set_text(pstLabel, pcaUpdateText);
}

static void vStart_BluetoothPairingAnimation(sT_BluetoothControl_Context_t *pstScreenContext)
{
    if(pstScreenContext == NULL)
    {
        FHALT("Bluetooth Menu Screen Context is Null");
        return;
    }

    lv_obj_t *pstBleImage = pstScreenContext->pstBleConnIcon;
    if(pstBleImage == NULL || !lv_obj_is_valid(pstBleImage))
    {
        FHALT("Label is Null");
        return;        
    }

    (void)lv_anim_delete(pstBleImage, vSet_BluetoothIconOpacity);

    lv_anim_t stAnimation;
    lv_anim_init(&stAnimation);

    lv_anim_set_var(&stAnimation, pstBleImage);
    lv_anim_set_exec_cb(&stAnimation, vSet_BluetoothIconOpacity);

    lv_anim_set_values(&stAnimation, LV_OPA_40, LV_OPA_COVER);
    lv_anim_set_duration(&stAnimation, 650U);
    lv_anim_set_reverse_duration(&stAnimation, 650U);
    lv_anim_set_repeat_count(&stAnimation, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&stAnimation, lv_anim_path_ease_in_out);

    (void)lv_anim_start(&stAnimation);

}

static void vStop_BluetoothIconAnimation(sT_BluetoothControl_Context_t *pstScreenContext)
{
    if(pstScreenContext == NULL)
    {
        FHALT("Bluetooth Menu Screen Context is Null");
        return;
    }

    lv_obj_t *pstBleImage = pstScreenContext->pstBleConnIcon;
    if(pstBleImage == NULL || !lv_obj_is_valid(pstBleImage))
    {
        FHALT("Label is Null");
        return;        
    }

    (void)lv_anim_delete(pstBleImage, vSet_BluetoothIconOpacity);
    lv_obj_set_style_image_opa(pstBleImage, LV_OPA_COVER, LV_PART_MAIN);
    pstScreenContext->bAnimationInProgress = false;
}

static void vSet_BluetoothIconOpacity( void *pvObject, int32_t iOpacity)
{
    lv_obj_t *pstImage = (lv_obj_t *)pvObject;
    if(pstImage == NULL || !lv_obj_is_valid(pstImage))
    {
        return;
    }

    lv_obj_set_style_image_opa(pstImage, (lv_opa_t)iOpacity, LV_PART_MAIN);
}

static void vExit_BluetoothConnControlScreen(sT_UIScreenRuntime_t *pstBleControlScreen)
{
    sT_BluetoothControl_Context_t *pstScreenContext = (sT_BluetoothControl_Context_t *)pstBleControlScreen->pvScreenData;
    if(pstScreenContext == NULL)
    {
        FHALT("Bluetooth Menu Screen Context is Null");
        return;
    }

    if(pstScreenContext->bAnimationInProgress)
    {
        vStop_BluetoothIconAnimation(pstScreenContext);
        vStop_TextAnimator();
        pstScreenContext->bAnimationInProgress = false;
    }
}

static void vHandle_EncoderInput(sT_UIScreenRuntime_t *pstBleControlScreen, const sT_UIInput_t *pstTUIInput)
{
    if(pstBleControlScreen == NULL || pstTUIInput == NULL)
    {
        FHALT("Null Pointer Reference");
        return;
    }

    if(pstTUIInput->eType != eUIInput_Press)
    {
        return;
    }

    sT_BluetoothControl_Context_t *pstScreenContext = (sT_BluetoothControl_Context_t *)pstBleControlScreen->pvScreenData;
    if(pstScreenContext == NULL)
    {
        FHALT("Bluetooth Menu Screen Context is Null");
        return;
    }

    if(!bRequest_UIScreen(eScreen_BluetoothMenu))
    {
        FHALT("Loading New Screen[%d] failed.", eScreen_BluetoothMenu);
        return;
    }

    if(pstScreenContext->bAnimationInProgress)
    {
        vStop_BluetoothIconAnimation(pstScreenContext);
        vStop_TextAnimator();
        pstScreenContext->bAnimationInProgress = false;
    }

}

static void vRun_BluetoothConnControlScreen(sT_UIScreenRuntime_t *pstAudSrcSelectionScreen)
{

}

#include "BluetoothMenu.h"

#include "GUI_Guider_Generated/generated/gui_guider.h"
#include "../../LVGL_Display_Controller.h"
#include "../../UI_ScreenManager/UI_ScreenManager.h"
#include "GenericMacro.h"
#include "../SelectedAudioSource/SelectedAudioSrc_Bluetooth/SelectedAudioSrcBluetooth_UIParam.h"
#include "../AudioSourceSelect_Menu/AudioSrcSelection_UIParam.h"
#include "../../../ExtFlash_Controller/AudioSrcConfiguration/AudioSrcConfiguration.h"

#include <lvgl_zephyr.h>
#include <string.h>
#include <zephyr/kernel.h>

#define BLUETOOTH_MENU_SELECTED_COLOR       0x066CFAU
#define BLUETOOTH_MENU_DEFAULT_COLOR        0x000000U
#define BLUETOOTH_MENU_NAVIGATION_DELAY_MS  200U

typedef enum
{
    eBluetoothMenuItem_Back = 0,
    eBluetoothMenuItem_ConnectPair,
    eBluetoothMenuItem_Disconnect,
    eNUMBER_OF_BLUETOOTH_MENU_ITEMs
} eBluetoothMenuItem_t;

typedef struct
{
    gg_ui_t stGuiderUI;

    lv_obj_t *pstBluetooth_Icon;
    lv_obj_t *pstBackIcon;

    eBluetoothMenuItem_t eFocusedItem;
    eBluetoothMenuItem_t ePreviouslyFocusedItem;
    uint32_t uiLastNavigation_ms;
} sT_BluetoothMenuScreen_Context_t;

static sT_BluetoothMenuScreen_Context_t stBluetoothMenuContext;

static bool bCreate_BluetoothMenuScreen(sT_UIScreenRuntime_t *pstBluetoothMenuScreen);
static void vEnter_BluetoothMenuScreen(sT_UIScreenRuntime_t *pstBluetoothMenuScreen);
static void vExit_BluetoothMenuScreen(sT_UIScreenRuntime_t *pstBluetoothMenuScreen);
static void vHandle_BluetoothMenuInput(sT_UIScreenRuntime_t *pstBluetoothMenuScreen,
                                       const sT_UIInput_t *pstUIInput);
static void vRun_BluetoothMenuScreen(sT_UIScreenRuntime_t *pstBluetoothMenuScreen);
static void vHandle_EncoderRotation(sT_UIScreenRuntime_t *pstBluetoothMenuScreen,
                                    const sT_UIInput_t *pstUIInput);
static void vHandle_EncoderPress(sT_UIScreenRuntime_t *pstBluetoothMenuScreen,
                                 const sT_UIInput_t *pstUIInput);
static void vSet_FocusedItem(sT_BluetoothMenuScreen_Context_t *pstScreenContext,
                             eBluetoothMenuItem_t eNewItem);
static lv_obj_t *pstGet_MenuItem(const sT_BluetoothMenuScreen_Context_t *pstScreenContext,
                                eBluetoothMenuItem_t eItem);

static bool bCreate_BluetoothIcon(sT_BluetoothMenuScreen_Context_t *pstScreenContext);
static bool bCreate_BackIcon(sT_BluetoothMenuScreen_Context_t *pstScreenContext);

bool bInit_BluetoothMenuScreen(sT_UIScreenRuntime_t *pstBluetoothMenuScreen,
                               sT_UIScreenDescriptor_t *pstScreenDescriptor)
{
    if(pstBluetoothMenuScreen == NULL || pstScreenDescriptor == NULL)
    {
        FHALT("Null Pointer Reference");
        return false;
    }

    memset(&stBluetoothMenuContext, 0, sizeof(stBluetoothMenuContext));

    pstBluetoothMenuScreen->eScreenId = eScreen_BluetoothMenu;
    pstBluetoothMenuScreen->pstRootObj = NULL;
    pstBluetoothMenuScreen->pvScreenData = &stBluetoothMenuContext;

    pstScreenDescriptor->eScreenId = eScreen_BluetoothMenu;
    pstScreenDescriptor->pstScreenRuntime = pstBluetoothMenuScreen;
    pstScreenDescriptor->bDeleteOnExit = true;
    pstScreenDescriptor->pfCreate = bCreate_BluetoothMenuScreen;
    pstScreenDescriptor->pfEnter = vEnter_BluetoothMenuScreen;
    pstScreenDescriptor->pfExit = vExit_BluetoothMenuScreen;
    pstScreenDescriptor->pfHandleInput = vHandle_BluetoothMenuInput;
    pstScreenDescriptor->pfRun = vRun_BluetoothMenuScreen;

    stBluetoothMenuContext.eFocusedItem = eBluetoothMenuItem_ConnectPair;
    stBluetoothMenuContext.uiLastNavigation_ms = 0U;

    return true;
}

static bool bCreate_BluetoothMenuScreen(sT_UIScreenRuntime_t *pstBluetoothMenuScreen)
{
    if(pstBluetoothMenuScreen == NULL)
    {
        FHALT("Null Pointer Reference");
        return false;
    }

    sT_BluetoothMenuScreen_Context_t *pstScreenContext =
        (sT_BluetoothMenuScreen_Context_t *)pstBluetoothMenuScreen->pvScreenData;
    if(pstScreenContext == NULL)
    {
        FHALT("Bluetooth Menu Screen Context is Null");
        return false;
    }

    bool bResult = false;
    lv_obj_t *pstRoot = NULL;

    lvgl_lock();

    if(pstBluetoothMenuScreen->pstRootObj != NULL &&
       lv_obj_is_valid(pstBluetoothMenuScreen->pstRootObj))
    {
        lvgl_unlock();
        return true;
    }

    pstBluetoothMenuScreen->pstRootObj = NULL;
    memset(pstScreenContext, 0, sizeof(*pstScreenContext));

    setup_Bluetooth_Menu(&pstScreenContext->stGuiderUI);
    pstRoot = pstScreenContext->stGuiderUI.Menu_revised.screen;

    if(pstRoot == NULL ||
       pstScreenContext->stGuiderUI.Menu_revised.Option_2 == NULL ||
       pstScreenContext->stGuiderUI.Menu_revised.Option_3 == NULL ||
       pstScreenContext->stGuiderUI.Menu_revised.Option_4 == NULL)
    {
        FHALT("GUI Guider failed to create the Bluetooth menu objects");
        goto Exit;
    }

    if(!bCreate_BluetoothIcon(pstScreenContext))
    {
        goto Exit;
    }
    if(!bCreate_BackIcon(pstScreenContext))
    {
        goto Exit;
    }

    pstBluetoothMenuScreen->pstRootObj = pstRoot;
    pstScreenContext->eFocusedItem = eBluetoothMenuItem_ConnectPair;
    bResult = true;

Exit:
    if(!bResult)
    {
        if(pstRoot != NULL && lv_obj_is_valid(pstRoot))
        {
            lv_obj_delete(pstRoot);
        }

        memset(pstScreenContext, 0, sizeof(*pstScreenContext));
        pstBluetoothMenuScreen->pstRootObj = NULL;
    }

    lvgl_unlock();
    return bResult;
}


static bool bCreate_BluetoothIcon(sT_BluetoothMenuScreen_Context_t *pstScreenContext)
{
    if(pstScreenContext == NULL)
    {
        FHALT("Null Pointer Reference");
        return false;
    }

    lv_obj_t *pstBleIcon = NULL;
    lv_obj_t *pstIconSlot = pstScreenContext->stGuiderUI.Menu_revised.Option;
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

static bool bCreate_BackIcon(sT_BluetoothMenuScreen_Context_t *pstScreenContext)
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
    //lv_obj_set_x(pstBackIconImage, 6);
    pstScreenContext->pstBackIcon = pstBackIconImage;
    
    return true;    
}

static void vEnter_BluetoothMenuScreen(sT_UIScreenRuntime_t *pstBluetoothMenuScreen)
{
    if(pstBluetoothMenuScreen == NULL || pstBluetoothMenuScreen->pstRootObj == NULL)
    {
        FHALT("Invalid Bluetooth Menu Screen Runtime Data");
        return;
    }

    sT_BluetoothMenuScreen_Context_t *pstScreenContext =
        (sT_BluetoothMenuScreen_Context_t *)pstBluetoothMenuScreen->pvScreenData;
    if(pstScreenContext == NULL)
    {
        FHALT("Bluetooth Menu Screen Context is Null");
        return;
    }

    lvgl_lock();

    if(!lv_obj_is_valid(pstBluetoothMenuScreen->pstRootObj))
    {
        lvgl_unlock();
        FHALT("Invalid Bluetooth Menu Screen Root Object");
        return;
    }

    pstScreenContext->eFocusedItem = eBluetoothMenuItem_ConnectPair;
    pstScreenContext->uiLastNavigation_ms = 0U;
    vSet_FocusedItem(pstScreenContext, eBluetoothMenuItem_ConnectPair);
    lv_screen_load(pstBluetoothMenuScreen->pstRootObj);

    lvgl_unlock();
}

static void vExit_BluetoothMenuScreen(sT_UIScreenRuntime_t *pstBluetoothMenuScreen)
{
    ARG_UNUSED(pstBluetoothMenuScreen);
}

static void vRun_BluetoothMenuScreen(sT_UIScreenRuntime_t *pstBluetoothMenuScreen)
{
    ARG_UNUSED(pstBluetoothMenuScreen);
}

static void vHandle_BluetoothMenuInput(sT_UIScreenRuntime_t *pstBluetoothMenuScreen,
                                       const sT_UIInput_t *pstUIInput)
{
    if(pstBluetoothMenuScreen == NULL || pstUIInput == NULL)
    {
        FHALT("Null Pointer Reference");
        return;
    }

    switch(pstUIInput->eType)
    {
        case eUIInput_Rotate:
            vHandle_EncoderRotation(pstBluetoothMenuScreen, pstUIInput);
            break;

        case eUIInput_Press:
        case eUIInput_Release:
            vHandle_EncoderPress(pstBluetoothMenuScreen, pstUIInput);
            break;

        default:
            FHALT("Invalid Encoder Event Type: %d", pstUIInput->eType);
            break;
    }
}

static void vHandle_EncoderRotation(sT_UIScreenRuntime_t *pstBluetoothMenuScreen,
                                    const sT_UIInput_t *pstUIInput)
{
    sT_BluetoothMenuScreen_Context_t *pstScreenContext =
        (sT_BluetoothMenuScreen_Context_t *)pstBluetoothMenuScreen->pvScreenData;
    if(pstScreenContext == NULL)
    {
        FHALT("Bluetooth Menu Screen Context is Null");
        return;
    }

    const uint32_t uiNow_ms = k_uptime_get_32();
    if((uiNow_ms - pstScreenContext->uiLastNavigation_ms) <
       BLUETOOTH_MENU_NAVIGATION_DELAY_MS)
    {
        return;
    }

    if(pstUIInput->iDetents == 0)
    {
        pstScreenContext->uiLastNavigation_ms = uiNow_ms;
        return;
    }

    eBluetoothMenuItem_t eNewItem;
    if(pstUIInput->iDetents > 0)
    {
        eNewItem = (pstScreenContext->eFocusedItem == eBluetoothMenuItem_Disconnect) ?
            eBluetoothMenuItem_Back :
            (eBluetoothMenuItem_t)(pstScreenContext->eFocusedItem + 1);
    }
    else
    {
        eNewItem = (pstScreenContext->eFocusedItem == eBluetoothMenuItem_Back) ?
            eBluetoothMenuItem_Disconnect :
            (eBluetoothMenuItem_t)(pstScreenContext->eFocusedItem - 1);
    }

    lvgl_lock();
    vSet_FocusedItem(pstScreenContext, eNewItem);
    lvgl_unlock();

    pstScreenContext->uiLastNavigation_ms = uiNow_ms;
}

static void vHandle_EncoderPress(sT_UIScreenRuntime_t *pstBluetoothMenuScreen,
                                 const sT_UIInput_t *pstUIInput)
{
    if(pstUIInput->eType != eUIInput_Press)
    {
        return;
    }

    sT_BluetoothMenuScreen_Context_t *pstScreenContext =
        (sT_BluetoothMenuScreen_Context_t *)pstBluetoothMenuScreen->pvScreenData;
    if(pstScreenContext == NULL)
    {
        FHALT("Bluetooth Menu Screen Context is Null");
        return;
    }

    switch(pstScreenContext->eFocusedItem)
    {
        case eBluetoothMenuItem_Back:
            if(!bRequest_UIScreen(eScreen_MainMenu))
            {
                FHALT("Screen Navigation Failed");
            }
            break;

        case eBluetoothMenuItem_ConnectPair:            
            if(!bSet_BleConnectionState(eBleState_Pairing))
            {
                FHALT("Bluetooth source is unavailable");
                break;
            }

            if(!bRequest_UIScreen(eScreen_BluetoothControl))
            {
                FHALT("Screen Navigation Failed");
            }
            break;

        case eBluetoothMenuItem_Disconnect:
            printk("Bluetooth Disconnect Requested...\n\r");
            break;

        default:
            FHALT("Invalid Bluetooth Menu Item: %d", pstScreenContext->eFocusedItem);
            break;
    }
}

static void vSet_FocusedItem(sT_BluetoothMenuScreen_Context_t *pstScreenContext,
                             eBluetoothMenuItem_t eNewItem)
{
    if(pstScreenContext == NULL || eNewItem >= eNUMBER_OF_BLUETOOTH_MENU_ITEMs)
    {
        FHALT("Invalid Bluetooth Menu Focus Request: %d", eNewItem);
        return;
    }

    for(eBluetoothMenuItem_t eItem = eBluetoothMenuItem_Back;
        eItem < eNUMBER_OF_BLUETOOTH_MENU_ITEMs;
        eItem++)
    {
        lv_obj_t *pstItem = pstGet_MenuItem(pstScreenContext, eItem);
        if(pstItem == NULL)
        {
            return;
        }

        const bool bIsFocused = (eItem == eNewItem);
        lv_obj_set_style_bg_color(
            pstItem,
            lv_color_hex(bIsFocused ? BLUETOOTH_MENU_SELECTED_COLOR :
                                      BLUETOOTH_MENU_DEFAULT_COLOR),
            LV_PART_MAIN);
        lv_obj_set_style_bg_opa(pstItem,
                                bIsFocused ? LV_OPA_COVER : LV_OPA_TRANSP,
                                LV_PART_MAIN);
    }

    pstScreenContext->eFocusedItem = eNewItem;
}

static lv_obj_t *pstGet_MenuItem(const sT_BluetoothMenuScreen_Context_t *pstScreenContext,
                                eBluetoothMenuItem_t eItem)
{
    if(pstScreenContext == NULL)
    {
        FHALT("Bluetooth Menu Screen Context is Null");
        return NULL;
    }

    switch(eItem)
    {
        case eBluetoothMenuItem_Back:
            return pstScreenContext->stGuiderUI.Menu_revised.Option_2;

        case eBluetoothMenuItem_ConnectPair:
            return pstScreenContext->stGuiderUI.Menu_revised.Option_3;

        case eBluetoothMenuItem_Disconnect:
            return pstScreenContext->stGuiderUI.Menu_revised.Option_4;

        default:
            FHALT("Invalid Bluetooth Menu Item: %d", eItem);
            return NULL;
    }
}

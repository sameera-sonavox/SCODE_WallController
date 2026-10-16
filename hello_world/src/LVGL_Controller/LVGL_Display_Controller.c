
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/input/input.h>
#include <zephyr/drivers/pinctrl.h>
#include <lvgl_zephyr.h>

#include "LVGL_Display_Controller.h"
#include "GenericMacro.h"
#include "UI_ScreenManager/UI_ScreenManager.h"
#include "Screens/Screen_Welcome/WelcomeScreen.h"
#include "FileSystem_Adapter/LVGL_ZephyrFS_Adapter.h"
#include "QDC/NXP_eQDC_API.h"

#include "UI_InputController/UI_InputController.h"

static const struct gpio_dt_spec stLVGL_BKPWEn = GPIO_DT_SPEC_GET(LVGL_BACKLIGHT_EN_PIN_NODE, gpios);

_Atomic eHostSystemType_t eHostSystemType = eHostSystem_None;

static void vUpdate_HostSystemType( void );
static void vNotify_DMA_LMA_SrcVolumeChange( eAudioSrc_Id_t eSrcId, uint8_t uiVol );
static void vNotify_DCM_SrcVolumeChange( eAudioSrc_Id_t eSrcId, uint8_t uiVol );

static bool bConfigure_LVGL_BKCtrlPin( void );
static inline bool bEn_Display_Backlight( void );
static inline bool bDis_Display_Backlight( void );

sT_HostSystem_t stTHostSystem = {
    .eHostSystem = eHostSystem_None
};

bool bInit_LVGLDisplay( void )
{
    if(!bInit_LVGL_ZephyrFSAdapter())
    {
        FHALT("LVGL: LVGL FileSystem Adapter initialization failed.");
        return false;
    }

    if(!bConfigure_LVGL_BKCtrlPin())
    {
        return false;
    }

    if(!bInitialize_eQDC())
    {
        return false;
    }

    if(!bInit_UIScreenManager())
    {
        FHALT("LVGL: LVGL Screen initialization failed.");
        return false;        
    }

    vUpdate_HostSystemType();

    if(!bRequest_UIScreen(eScreen_Welcome))
    {
        FHALT("LVGL: Welcome screen request failed");
        return false;        
    }

    if(!bEn_Display_Backlight())
    {
        FHALT("LVGL: Display backlight could not be enabled.");
        return false;
    }

    return true;
}

static void vUpdate_HostSystemType( void )
{
    vSet_HostSystemType(eHostSystem_DCM);

    switch(eGet_HostSystemType())
    {
        case eHostSystem_DCM:
            /* vUpdate_AudioSourceIndicatorVisibility( true ); */
            break;
        case eHostSystem_LMA:
        case eHostSystem_DMA:
            /* vUpdate_AudioSourceIndicatorVisibility( false ); */
            break;
        default:           
            break;
    }
}

void vNotify_SrcVolumeChange( eAudioSrc_Id_t eSrcId, uint8_t uiVol )
{
    eHostSystemType_t eHostType = eGet_HostSystemType();

    switch(eHostType)
    {
        case eHostSystem_DCM:
            vNotify_DCM_SrcVolumeChange(eSrcId, uiVol);
            break;
        case eHostSystem_LMA:
        case eHostSystem_DMA:
            vNotify_DMA_LMA_SrcVolumeChange(eSrcId, uiVol);
            break;
        default:
            FHALT("Invalid Host System Type: %d, for Volume Change Notification", eHostType);
            break;
    }
}

static void vNotify_DMA_LMA_SrcVolumeChange( eAudioSrc_Id_t eSrcId, uint8_t uiVol )
{
    //Notify the host system about the volume change for the source
    printf("Host System: %d, Source: %d, Volume: %d\n\r", eGet_HostSystemType(), eSrcId, uiVol);
}

static void vNotify_DCM_SrcVolumeChange( eAudioSrc_Id_t eSrcId, uint8_t uiVol )
{
    //Notify the host system about the volume change for the source
    printf("Host System: %d, Source: %d, Volume: %d\n\r", eGet_HostSystemType(), eSrcId, uiVol);
}

static bool bConfigure_LVGL_BKCtrlPin( void )
{
    if(!gpio_is_ready_dt(&stLVGL_BKPWEn))
    {
        FHALT("GPIO for BackLight Control is not ready");
        return false;
    }

    int ret = gpio_pin_configure_dt(&stLVGL_BKPWEn, GPIO_OUTPUT);
    if(ret != 0)
    {
        FHALT("GPIO for BackLight Control cannot be configured as output");
        return false;        
    }

    if(!bDis_Display_Backlight())
    {
        FHALT("GPIO for BackLight Control cannot be driven");
        return false;         
    }
    return true;
}

static inline bool bEn_Display_Backlight( void )
{
    int ret = gpio_pin_set_dt(&stLVGL_BKPWEn, 1);
    if(ret != 0)
        return false;
    return true;
}

static inline bool bDis_Display_Backlight( void )
{
    int ret = gpio_pin_set_dt(&stLVGL_BKPWEn, 0);
    if(ret != 0)
        return false;
    return true;
}

void vSet_HostSystemType( eHostSystemType_t eType )
{
    if(eType >= eNUMBER_OF_HOST_SYSTEMs)
    {
        FHALT("Host System : %d, is not supported by the current implementation", eType);
        return;
    }

    atomic_store_explicit(&stTHostSystem.eHostSystem, eType, memory_order_release);
}

eHostSystemType_t eGet_HostSystemType( void )
{
    eHostSystemType_t eType = atomic_load_explicit(&stTHostSystem.eHostSystem, memory_order_acquire);
    return eType;
}

void vRun_UI( void )
{
    vRun_UIScreenManager();
/*     int ret = k_msgq_get(pstGetMessageQueue(), &stTEncData.stTEncPhaseData, K_NO_WAIT);

    #warning Need to find a way how this cna be done inside the UI_InputController
     if(ret !=0 && !bIsEncSWPressed())
    {
        vCheck_UIMode_Timeout(&stTEncData);
        return;
    }
    if(bIsEncSWPressed() && ret !=0)
    {
        vMark_EncDataInvalid(&stTEncData.stTEncPhaseData);
    } 

    k_mutex_lock(&stTEncData.stkIsLocked, K_FOREVER);
    eScreenId_t eActiveScreen = eGetActiveScreen();

    switch(eActiveScreen)
    {
        case eScreen_Welcome:
            break;
        case eScreen_SourceSelect:
            vRun_AudioSourceScreen(&staUIScreens[eScreen_SourceSelect], &stTEncData);
            break;
        default:
            FHALT("Screen : %d, is not yet implemented", eActiveScreen);
            break;
    }
    k_mutex_unlock(&stTEncData.stkIsLocked); */
}

void vDelete_LVGLObject(lv_obj_t **ppstObj)
{
    if(ppstObj == NULL || *ppstObj == NULL)
    {
        return;
    }

    lv_obj_delete(*ppstObj);
    *ppstObj = NULL;
}




#include "API_Usage_Definition.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <lvgl_zephyr.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/input/input.h>
#include <zephyr/drivers/pinctrl.h>

#include "LVGL_Display_Controller.h"
#include "GenericMacro.h"
#include "Screen_Parameters/LVGL_AudioSources_UIParam.h"
#include "Screen_Parameters/LVGL_AudioSource_UIScreen.h"
#include "Screens/Screen_Welcome/WelcomeScreen.h"
#include "FileSystem_Adapter/LVGL_ZephyrFS_Adapter.h"
#include "QDC/NXP_eQDC_API.h"

PINCTRL_DT_DEFINE(EQDC0_PHASE_PIN_NODE);

static const struct device *const pstEncSWInputDevice = DEVICE_DT_GET(DT_PARENT(ENC_SW_PIN_NODE));
static const struct gpio_dt_spec stLVGL_BKPWEn = GPIO_DT_SPEC_GET(LVGL_BACKLIGHT_EN_PIN_NODE, gpios);

static sT_eQDCConfig_t eQDCUIScreen = {0};
static sT_QEncData_t stTEncData = {0};
_Atomic eHostSystemType_t eHostSystemType = eHostSystem_None;

static void vUpdate_HostSystemType( void );
static void vNotify_DMA_LMA_SrcVolumeChange( eAudioSrc_Id_t eSrcId, uint8_t uiVol );
static void vNotify_DCM_SrcVolumeChange( eAudioSrc_Id_t eSrcId, uint8_t uiVol );

static bool bInit_Screens(void);
static void vInitialize_eQDC( void );
static bool bConfigure_eQDC_PhasePins( void );
static void vEncoder_Callback(sT_eQDC_PosChangeNotify_t stTeQDCData);
static inline void vMark_EncDataInvalid(sT_eQDC_PosChangeNotify_t *pstNotifyCtrl);

static bool bConfigure_EncSWPin( void );
static void vEnc_SWPressed_InterruptHandler(struct input_event *pstEvent, void *puserData);
static bool bConfigure_LVGL_BKCtrlPin( void );
static inline bool bEn_Display_Backlight( void );
static inline bool bDis_Display_Backlight( void );

static inline void vSet_EncSWPressed( void );

K_MSGQ_DEFINE(kmsgq_eQDC_MessageQueue, sizeof(sT_eQDC_PosChangeNotify_t), MAX_ENCODER_MESSAGEs, 4);
INPUT_CALLBACK_DEFINE(pstEncSWInputDevice, vEnc_SWPressed_InterruptHandler, NULL);

sT_HostSystem_t stTHostSystem = {
    .eHostSystem = eHostSystem_None
};

sT_UIScreen_t staUIScreens[eNUMBER_OF_SCREENs] = {
    
    {
        .eScreenId = eScreen_Welcome,
        .bIsActive = false,
    },
    {
        .eScreenId = eScreen_SourceSelect,
        .bIsActive = false,
        .stTDisplayInfo.screenType.stTAudioSrcDisplay.staAudioSources = {
            
            {
                .acName = "BT Audio",
                .bIsActive = true,
                .bIsVisible = true,
                .sourceHoriz_SeparatorPoints[0] = {.x = 0U, .y = DEFAULT_AUDIOSRC_SEPARATOR_COORD_Y},
                .sourceHoriz_SeparatorPoints[1] = {.x = AUDIO_SOURCE_LIST_DISPLAY_WIDTH, .y = DEFAULT_AUDIOSRC_SEPARATOR_COORD_Y},
                .uiVolume = 20,
            },
            {
                .acName = "MIC",
                .bIsActive = true,
                .bIsVisible = true,
                .sourceHoriz_SeparatorPoints[0] = {.x = 0U, .y = DEFAULT_AUDIOSRC_SEPARATOR_COORD_Y},
                .sourceHoriz_SeparatorPoints[1] = {.x = AUDIO_SOURCE_LIST_DISPLAY_WIDTH, .y = DEFAULT_AUDIOSRC_SEPARATOR_COORD_Y},
                .uiVolume = 70,
            },
            {
                .acName = "Conference RM",
                .bIsActive = true,
                .bIsVisible = true,
                .sourceHoriz_SeparatorPoints[0] = {.x = 0U, .y = DEFAULT_AUDIOSRC_SEPARATOR_COORD_Y},
                .sourceHoriz_SeparatorPoints[1] = {.x = AUDIO_SOURCE_LIST_DISPLAY_WIDTH, .y = DEFAULT_AUDIOSRC_SEPARATOR_COORD_Y},
                .uiVolume = 50,
            },
            {
                .acName = "Office",
                .bIsActive = true,
                .bIsVisible = true,
                .sourceHoriz_SeparatorPoints[0] = {.x = 0U, .y = DEFAULT_AUDIOSRC_SEPARATOR_COORD_Y},
                .sourceHoriz_SeparatorPoints[1] = {.x = AUDIO_SOURCE_LIST_DISPLAY_WIDTH, .y = DEFAULT_AUDIOSRC_SEPARATOR_COORD_Y},
                .uiVolume = 90,
            }
        },        
    }
};

bool bInit_LVGLDisplay( void )
{
    if(!bInit_LVGL_ZephyrFSAdapter())
    {
        FHALT("LVGL: LVGL FileSystem Adapter initialization failed.");
        return false;
    }
    
    vInitialize_eQDC();

    if(!bInit_Screens())
    {
        FHALT("LVGL: LVGL Screen initialization failed.");
        return false;        
    }

    vUpdate_HostSystemType();

    bEn_Display_Backlight();

    vLoad_Screen(eScreen_Welcome);

    return true;
}

static void vUpdate_HostSystemType( void )
{
    vSet_HostSystemType(eHostSystem_DCM);

    switch(eGet_HostSystemType())
    {
        case eHostSystem_DCM:
            vUpdate_AudioSourceIndicatorVisibility( true );
            break;
        case eHostSystem_LMA:
        case eHostSystem_DMA:
            vUpdate_AudioSourceIndicatorVisibility( false );
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

static void vInitialize_eQDC( void )
{
    if(!bConfigure_eQDC_PhasePins())
        return;

    eQDCUIScreen.bIsConfigOk = false;
    eQDCUIScreen.eID = eQDC_0;
    eQDCUIScreen.eQDCCountMode = eQDC_QuadratureCycle_1Count;
    eQDCUIScreen.eQuadratureMode = eQDCMode_NormalQuadrature;
    eQDCUIScreen.eInpRoute_PhaseA = eQDCIN_4;
    eQDCUIScreen.eInpRoute_PhaseB = eQDCIN_9;

    eQDCUIScreen.stTHWTrigCtrl.bUseHWTriggerForEncRead = true;
    eQDCUIScreen.stTHWTrigCtrl.eCTimerSrc = eTrigSrc_CTIMER0_MAT2;
    eQDCUIScreen.stTHWTrigCtrl.uiTrigFrequency_Hz = 500;

    eQDCUIScreen.stTInpFiltConfig.bIsEnabled = false;
    eQDCUIScreen.stTInpFiltConfig.eFilterSampleCount = eQDC_FiltSampleCOunt_3U;
    eQDCUIScreen.stTInpFiltConfig.eRefClock_PreScalar = eQDC_Prescalar_4096U;
    eQDCUIScreen.stTInpFiltConfig.uiSamplePeriod_us = 250U;
    eQDCUIScreen.pvUserCallback = vEncoder_Callback;

    k_mutex_init(&stTEncData.stkIsLocked);

    if(!bConfigure_EncSWPin())
        return;

    vInit_eQDC(&eQDCUIScreen);

    if(!eQDCUIScreen.bIsConfigOk)
    {
        FHALT("Quadrature Encoder Configuration Failed");
        return;
    }
    //
    switch(eQDCUIScreen.eQDCCountMode)
    {
        case eQDC_QuadratureCycle_1Count:
            uiNAV_COUNTS_PER_DETENT = 1U;
            break;
        case eQDC_QuadratureCycle_2Count:
            uiNAV_COUNTS_PER_DETENT = 2U;
            break;
        case eQDC_QuadratureCycle_4Count:
            uiNAV_COUNTS_PER_DETENT = 4U;
            break;
        default:
            uiNAV_COUNTS_PER_DETENT = 1U;            
            break;
    }
    printf("eQDC Initialized\n\r");
}

static bool bConfigure_eQDC_PhasePins( void )
{
    static const struct pinctrl_dev_config *pstPinConfig = PINCTRL_DT_DEV_CONFIG_GET(EQDC0_PHASE_PIN_NODE);

    int res = pinctrl_apply_state(pstPinConfig, PINCTRL_STATE_DEFAULT);
    if(res != 0)
    {
        FHALT("eQDC phase pin configuration failed: %d", res);
        return false;        
    }

    return true;
}

static bool bConfigure_EncSWPin(void)
{
    if (!device_is_ready(pstEncSWInputDevice))
    {
        FHALT("Encoder switch input device is not ready");
        return false;
    }

    return true;
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

static void vEnc_SWPressed_InterruptHandler(struct input_event *pstEvent, void *puserData)
{
    ARG_UNUSED(puserData);

    if ((pstEvent->type == INPUT_EV_KEY) &&
        (pstEvent->code == INPUT_KEY_0) &&
        (pstEvent->value == 1))
    {
        vSet_EncSWPressed();
    }    
}

static void vEncoder_Callback(sT_eQDC_PosChangeNotify_t stTeQDCData)
{
    uint32_t uiFreeMsgs = k_msgq_num_free_get(&kmsgq_eQDC_MessageQueue);
    if(uiFreeMsgs > 0)
    {
        k_msgq_put(&kmsgq_eQDC_MessageQueue, &stTeQDCData, K_NO_WAIT);
    }
    else
    {
        sT_eQDC_PosChangeNotify_t stTTemp = {0};
        (void)k_msgq_get(&kmsgq_eQDC_MessageQueue, &stTTemp, K_NO_WAIT);
        (void)k_msgq_put(&kmsgq_eQDC_MessageQueue, &stTeQDCData, K_NO_WAIT);
    }
}

struct k_msgq *pstGetMessageQueue( void )
{
    return &kmsgq_eQDC_MessageQueue;
}

void vRun_UI( void )
{
    int ret = k_msgq_get(pstGetMessageQueue(), &stTEncData.stTEncPhaseData, K_NO_WAIT);
    
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
    k_mutex_unlock(&stTEncData.stkIsLocked);
}

void vClear_eQDCMessageQueue( void )
{
    k_msgq_purge(&kmsgq_eQDC_MessageQueue);
}

sT_UIScreenDisplay *pstGetDisplayScreen(eScreenId_t eId)
{
    if(eId >= eNUMBER_OF_SCREENs)
    {
        FHALT("Invalid Screen Id : %d", eId);
        return NULL;
    }

    return &staUIScreens[eId].stTDisplayInfo;
}

static inline void vMark_EncDataInvalid(sT_eQDC_PosChangeNotify_t *pstNotifyCtrl)
{
    if(pstNotifyCtrl == NULL)
        return;
    atomic_store_explicit(&pstNotifyCtrl->bIsValid, false, memory_order_release);
}

static inline void vSet_EncSWPressed( void )
{
    atomic_store_explicit(&stTEncData.bIsEncPressed, true, memory_order_release);
}

void vClear_EncSWPressed( void )
{
    atomic_store_explicit(&stTEncData.bIsEncPressed, false, memory_order_release);
}

bool bIsEncSWPressed( void )
{
    bool bIsPressed = atomic_load_explicit(&stTEncData.bIsEncPressed, memory_order_acquire);
    return bIsPressed;
}

void vClear_EncoderRotationData(sT_QEncData_t *pstQEncData)
{
    if(pstQEncData == NULL)
        return;

    vMark_EncDataInvalid(&pstQEncData->stTEncPhaseData);    
}

void vLoad_Screen( eScreenId_t eID )
{
    if(eID >= eNUMBER_OF_SCREENs)
    {
        FHALT("Invalid Screen Id: %d", eID);
        return;
    }
    if(staUIScreens[eID].pstScreenObj == NULL)
    {
        FHALT("Scrren[%d] could not be loaded, since it is not registered.", eID);
        return;
    }

    lvgl_lock();
    lv_screen_load(staUIScreens[eID].pstScreenObj);
    lvgl_unlock();

    vSet_ScreenActive(eID);

    switch(eID)
    {
        case eScreen_Welcome:
            vStart_WelcomeImageFadeIn();
            break;
        case eScreen_SourceSelect:
            vSetup_AudioSrc_ScreenStartup();
            break;
        default:
            FHALT("Screen Not Implemented Screen: %d", eID);
            break;
    }
}

static bool bInit_Screens(void)
{    
    if(!bConfigure_LVGL_BKCtrlPin())
        return false;
    
    bool bResult = false;

    for(uint8_t i = eScreen_Welcome; i < eNUMBER_OF_SCREENs; i++)
    {
        switch(i)
        {
            case eScreen_Welcome:
                bResult = bInit_WelcomeScreen(&staUIScreens[i]);
                break;
            case eScreen_SourceSelect:
                bResult = bInit_SourceSelectScreen_Controls(&staUIScreens[i]);
                break;
            default:
                FHALT("Invalid UI Screen : %d", i);
                return false;
        }

        if(!bResult)
            return bResult;
    }

    return bResult;
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

bool bSet_AudioSource_MuteState( eAudioSrc_Id_t eSrcId, bool bIsMute )
{
    if(eSrcId >= eNUMBER_OF_AUDIO_SOURCES)
    {
        FHALT("Invalid Audio Src : %d", eSrcId);
        return false;
    }
    sT_UIScreen_t *pstAudioScreen = &staUIScreens[eScreen_SourceSelect];
    sT_AudioSource_t *pstAudSrc =
        &pstAudioScreen->stTDisplayInfo.screenType.stTAudioSrcDisplay.staAudioSources[eSrcId];
    if(!bIsAudioSourceVisible(pstAudSrc))
    {
        FHALT("Invalid Request while Audion Souce is removed");
        return false;
    }    
    
    if(bIsMute)
        vSet_AudioSourceMutedFlag(pstAudSrc);
    else
        vClear_AudioSourceMutedFlag(pstAudSrc);

    if(pstAudSrc->pstAudSrcObj == NULL)
    {
        FHALT("The Audio Src is NULL and Not Permitted.");
        return false;
    }
    
    lvgl_lock();

    for(uint8_t i = 0; i < NUMBER_OF_UI_CONTROLS_PER_AUDIO_SOURCE; i++)
    {
        sT_UIControl *pstUICtrl = &pstAudSrc->staUIControls[i];
        if(pstUICtrl->pstUIObj == NULL)
        {
            FHALT("Null Pointer reference for ObjType: %d", pstUICtrl->eObjType);
            continue;
        }

        switch(pstUICtrl->eObjType)
        {
            case eUIObj_Label:                
                if(!bIsAudioSourceActive(pstAudSrc))
                {
                    pstUICtrl->uiObject.stTObj_Label.lcolor_Text = lv_color_hex(INACTIVE_AUDIO_SRC_TEXT_COLOR);        
                }
                else
                {
                    pstUICtrl->uiObject.stTObj_Label.lcolor_Text = (bIsMute)? lv_color_hex(MUTE_AUDIO_SRC_TEXT_COLOR):
                                                                            lv_color_hex(ACTIVE_AUDIO_SRC_TEXT_COLOR);
                }
                lv_obj_set_style_text_color(pstUICtrl->pstUIObj, 
                                            pstUICtrl->uiObject.stTObj_Label.lcolor_Text, LV_PART_MAIN);
                break;
            default:
                break;
        }
    }

    lvgl_unlock();
    return true;
}

bool bSet_AudioSource_ActiveState( eAudioSrc_Id_t eSrcId, bool bIsActive )
{
    if(eSrcId >= eNUMBER_OF_AUDIO_SOURCES)
    {
        FHALT("Invalid Audio Src : %d", eSrcId);
        return false;
    }
    sT_UIScreen_t *pstAudioScreen = &staUIScreens[eScreen_SourceSelect];
    sT_AudioSource_t *pstAudSrc =
        &pstAudioScreen->stTDisplayInfo.screenType.stTAudioSrcDisplay.staAudioSources[eSrcId];
    if(!bIsAudioSourceVisible(pstAudSrc))
    {
        FHALT("Invalid Request while Audion Souce is removed");
        return false;
    }

    if(bIsActive)
        vSet_AudioSourceActiveFlag(pstAudSrc);
    else
        vClear_AudioSourceActiveFlag(pstAudSrc);
            
    if(pstAudSrc->pstAudSrcObj == NULL)
    {
        FHALT("The Audio Src is NULL and Not Permitted.");
        return false;
    }
    
    lvgl_lock();

    for(uint8_t i = 0; i < NUMBER_OF_UI_CONTROLS_PER_AUDIO_SOURCE; i++)
    {
        sT_UIControl *pstUICtrl = &pstAudSrc->staUIControls[i];
        if(pstUICtrl->pstUIObj == NULL)
        {
            FHALT("Null Pointer reference for ObjType: %d", pstUICtrl->eObjType);
            continue;
        }

        switch(pstUICtrl->eObjType)
        {
            case eUIObj_Label:
                if(bIsAudioSrcMuted(pstAudSrc))
                {
                    pstUICtrl->uiObject.stTObj_Label.lcolor_Text = lv_color_hex(MUTE_AUDIO_SRC_TEXT_COLOR);
                }
                else
                {
                    pstUICtrl->uiObject.stTObj_Label.lcolor_Text = (bIsActive)? lv_color_hex(ACTIVE_AUDIO_SRC_TEXT_COLOR):
                                                                                lv_color_hex(INACTIVE_AUDIO_SRC_TEXT_COLOR);
                }
                lv_obj_set_style_text_color(pstUICtrl->pstUIObj, 
                                            pstUICtrl->uiObject.stTObj_Label.lcolor_Text, LV_PART_MAIN);
                break;
            default:
                break;
        }
    }

    lvgl_unlock();
    return true;
}

bool bRemove_AudioSource( eAudioSrc_Id_t eSrcId )
{
    if(eSrcId >= eNUMBER_OF_AUDIO_SOURCES)
    {
        FHALT("Invalid Audio Src : %d", eSrcId);
        return false;
    }
    sT_UIScreen_t *pstAudioScreen = &staUIScreens[eScreen_SourceSelect];
    sT_AudioSource_t *pstAudSrc =
        &pstAudioScreen->stTDisplayInfo.screenType.stTAudioSrcDisplay.staAudioSources[eSrcId];
    if(!bIsAudioSourceVisible(pstAudSrc))
        return true;
    
    if(pstAudSrc->pstAudSrcObj == NULL)
    {
        vClear_AudioSourceVisibilityFlag(pstAudSrc);
        FHALT("The Audio Src is NULL and Not Permitted.");
        return false;
    }

    vClear_AudioSourceVisibilityFlag(pstAudSrc);

    lvgl_lock();
    lv_obj_delete(pstAudSrc->pstAudSrcObj);
    lvgl_unlock();

    pstAudSrc->pstAudSrcObj = NULL;

    for(uint8_t i = 0; i < NUMBER_OF_UI_CONTROLS_PER_AUDIO_SOURCE; i++)
    {
        pstAudSrc->staUIControls[i].pstUIObj = NULL;
    }

    return true;
}

void vSet_ScreenActive(eScreenId_t eId)
{
    for(uint8_t i = 0; i < eNUMBER_OF_SCREENs; i++)
    {
        if((eScreenId_t)i == eId)
        {
            atomic_store_explicit(&staUIScreens[eId].bIsActive, true, memory_order_release);
        }
        else
        {
            vSet_ScreenInactive((eScreenId_t)i);
        }
    }
    
}

void vSet_ScreenInactive(eScreenId_t eId)
{
    atomic_store_explicit(&staUIScreens[eId].bIsActive, false, memory_order_release);
}

eScreenId_t eGetActiveScreen( void )
{
    for(uint8_t i = 0; i < eNUMBER_OF_SCREENs; i++)
    {
        bool bIsActive = atomic_load_explicit(&staUIScreens[i].bIsActive, memory_order_acquire);
        if(bIsActive)
            return (eScreenId_t)i;
    }
    return eNUMBER_OF_SCREENs;
}

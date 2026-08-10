#include "LVGL_AudioSources_UIParam.h"
#include "LVGL_AudioSource_UIScreen.h"
#include "../LVGL_Display_Controller.h"
#include "../LVGL_Display_Types.h"
#include "../LVGL_ProjDef.h"
#include "../Lib/QDC/NXP_eQDC_API.h"
#include "GenericMacro.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <lvgl_zephyr.h>
#include <stdlib.h>

typedef enum
{
    eUI_Mode_None,
    eUI_Mode_SourceSelect,
    eUI_Mode_VolumeUpdate,
    eNUMBER_OF_SCREEN_EVENTs
} eUI_Mode_t;

typedef struct
{
    _Atomic eUI_Mode_t eCurrentUIMode;
    _Atomic uint32_t uiEventTrigTime;
} sT_ScreenEvent_Control_t;

typedef struct
{
    uint32_t uiPosition_Previous;
    uint32_t uiPosition_Current;
    int32_t iPosition_Delta;
    int32_t iNavigationCountAccumulator;
    eQDC_Direction_t eDir;

    bool bIsSWPressed;
    bool bIsRotationValid;
} sT_EncPosition_Update_t;

typedef struct
{
    uint32_t uiLastStepTime;
    int32_t iCountAccumulator;
    eQDC_Direction_t ePreviousDirection;
    bool bHasLastStepTime;
    bool bHasPreviousDirection;
} sT_VolumeEditState_t;

static sT_VolumeEditState_t stTVolumeEditState = {0};

sT_ScreenEvent_Control_t stTScreenEventCtrl = {
    .eCurrentUIMode = eUI_Mode_None,
    .uiEventTrigTime = 0U
};

sT_EncPosition_Update_t stTEncPosition = {
    .uiPosition_Previous = 0U,
    .uiPosition_Current = 0U,
    .iPosition_Delta = 0,
    .iNavigationCountAccumulator = 0,
    .bIsRotationValid = false,
    .bIsSWPressed = false
};

uint32_t uiNAV_COUNTS_PER_DETENT = 0U;

static lv_point_precise_t screenVertical_SeparatorPoints[] = {
    {AUDIO_SOURCE_LIST_DISPLAY_WIDTH + 1U, 0U},
    {AUDIO_SOURCE_LIST_DISPLAY_WIDTH + 1U, DISPLAY_HEIGHT}
};

/* static lv_point_precise_t sourceHoriz_SeparatorPoints[] = {
    {0U, DEFAULT_AUDIOSRC_SEPARATOR_COORD_Y},
    {AUDIO_SOURCE_LIST_DISPLAY_WIDTH, DEFAULT_AUDIOSRC_SEPARATOR_COORD_Y}
}; */

static sT_UIScreen_t *pstAudioSrcScreen = NULL;

static void vCreate_SourceList( sT_UIScreen_t *pstSoucreScreen );
static lv_obj_t *pstCreate_UILabel(lv_obj_t *pstParent, sT_UIObj_Label *pstTObj_Label, sT_AudioSource_t *pstAudioSrc);
static lv_obj_t *pstCreate_UIIndicator(lv_obj_t *pstParent, sT_UIObj_Indicator *pstObj_Indicator, sT_AudioSource_t *pstAudioSrc);
static lv_obj_t *pstCreate_UIVolControl(lv_obj_t *pstParent, sT_UIObj_VolControl *pstObj_VolCtrl);
static void vSet_Indicator_StateHide(lv_obj_t *pstIndicator);
static void vSet_Indicator_StateVisible(lv_obj_t *pstIndicator);
static void vSet_Indicator_AtSourceSelected(sT_AudioSource_t *pstAudioSource);
static void vUpdate_IndicatorLocation_AtScrollVisible( lv_obj_t *pstAudioSrcList );
static void vUpdate_VolumeBar_AtSrcSelected(sT_AudioSource_t *pstAudioSource);
static void vUpdate_UI_VolControl(lv_obj_t *pstVolParent, uint8_t uiVol);

void vUpdate_ContainerHeight_RefTo_LabelHeight(lv_obj_t *pstContainer, lv_obj_t *pstLabel, sT_AudioSource_t *pstAudioSrc, lv_coord_t stRefY);
static const lv_font_t * const pstGetText_Font( eFontSize_t eFSize );
static sT_AudioSource_t *pstGetSelectedAudioSource( void );
static inline eAudioSrc_Id_t eGetFirstActiveSource( void );
static void vUpdate_UI_ForSourceSelection(eAudioSrc_Id_t ePrevSelectedSrc, eAudioSrc_Id_t eNewSelectedSrc);
static void vClear_PrevSelectedSource_Style(sT_AudioSource_t *pstAudioSource);
static void vSet_CurrentSelectedSource_Style(sT_AudioSource_t *pstAudioSource);

static inline void vSet_UIIndicator_Visibility(sT_UIObj_Indicator *pstObj_Indicator);
static inline bool bIs_UIIndicator_Visible(sT_UIObj_Indicator *pstObj_Indicator);
static inline void vClear_UIIndicator_Visibility(sT_UIObj_Indicator *pstObj_Indicator);

//Helper Functions
static sT_UIControl *pstGetUIObject(sT_UIControl *pstUIControls, eUI_Obj_Type_t eObjType);

static inline void vSet_UIMode(eUI_Mode_t eMode);
static inline eUI_Mode_t eGet_UIMode( void );
static inline void vUpdate_TrigTime(uint32_t uiTime);
static inline uint32_t uiGet_TrigTime( void );

static inline void vSet_AudioSrc_Volume(sT_AudioSource_t *pstAudioSource, uint8_t uiVol);
static inline uint8_t uiGet_AudioSrc_Volume(sT_AudioSource_t *pstAudioSource);
static void vIndicate_VolUpdate_InVolControl( void );
static void vVolumeBar_OpacityAnim(void *pvObj, int32_t iOpacity);
static void vNotify_VolBar_BorderStyleChange(lv_obj_t *pstVolBar, bool bIsExiting);

static void vProcess_Encoder_Data(sT_QEncData_t *pstTEncData);
static void vProcess_Initial_State( sT_QEncData_t *pstTEncData );
static void vProcess_UIMode_SourceSelection( sT_QEncData_t *pstTEncData );
static void vProcess_UIMode_VolumeUpdate( sT_QEncData_t *pstTEncData );
static void vClear_EncoderTransientState( sT_QEncData_t *pstQEncData );
static bool bIsVolEditTimeOut( void );
static void vNavigate_Sources( void );
static eAudioSrc_Id_t eFindNextSource(eAudioSrc_Id_t eCurrentId, 
                                      sT_AudioSource_t *pstCurrentAudioSrcs,
                                      eQDC_Direction_t eDir);

static void vUpdate_SelectedSourceVOlume(void);
static void vReset_VolumeEditState( void );

static void vClear_EncData(sT_QEncData_t *pstQEncData);

#define bIsENCRotationValid()                   (stTEncPosition.bIsRotationValid)
#define bIsENCSWPressed()                       (stTEncPosition.bIsSWPressed)

void vRun_AudioSourceScreen( sT_UIScreen_t *pstUIScreen, sT_QEncData_t *pstQEncData)
{
    if(pstUIScreen == NULL || pstQEncData == NULL)
    {
        vClear_EncData(pstQEncData);
        FHALT("Null Pointer reference");
        return;
    }

    vProcess_Encoder_Data(pstQEncData);

    sT_AudioSource_t *pstAudioSrc = pstGetSelectedAudioSource();
    if(pstAudioSrc == NULL)
    {
        vClear_EncData(pstQEncData);
        return;
    }

    eUI_Mode_t eMode = eGet_UIMode();

    switch(eMode)
    {
        case eUI_Mode_None:
            vProcess_Initial_State( pstQEncData );         
            break;
        case eUI_Mode_SourceSelect://Select the sources
            vProcess_UIMode_SourceSelection( pstQEncData );
            break;
        case eUI_Mode_VolumeUpdate://Updating the parameters
            vProcess_UIMode_VolumeUpdate( pstQEncData );
            break;
        default:
            break;
    }
}

static void vProcess_Encoder_Data(sT_QEncData_t *pstTEncData)
{
    if(pstTEncData == NULL)
    {
        FHALT("Null Pointer reference");
        return;
    }

    stTEncPosition.bIsRotationValid = false;

    sT_eQDC_PosChangeNotify_t *pstTEncPhaseData = &pstTEncData->stTEncPhaseData;
    if(bIs_EncDataValid(pstTEncPhaseData))
    {
        stTEncPosition.uiPosition_Previous = stTEncPosition.uiPosition_Current;
        stTEncPosition.uiPosition_Current = pstTEncPhaseData->uiEncPos;
        stTEncPosition.iPosition_Delta = (int32_t)(stTEncPosition.uiPosition_Current - stTEncPosition.uiPosition_Previous);

        stTEncPosition.eDir = pstTEncPhaseData->eDir;
        stTEncPosition.bIsRotationValid = (stTEncPosition.iPosition_Delta != 0);
    }

    stTEncPosition.bIsSWPressed = bIsEncSWPressed();
}

static void vProcess_UIMode_VolumeUpdate( sT_QEncData_t *pstTEncData )
{
    if(bIsVolEditTimeOut())
    {
        vClear_EncoderTransientState(pstTEncData);
        vSet_UIMode(eUI_Mode_SourceSelect);
        vNotify_VolBar_BorderStyleChange(NULL, true);
        return;
    }

    if(bIsENCSWPressed())
    {
        vClear_EncoderTransientState(pstTEncData);
        vSet_UIMode(eUI_Mode_SourceSelect);
        vNotify_VolBar_BorderStyleChange(NULL, true);
        return;
    }

    if(bIsENCRotationValid())
    {
        vUpdate_SelectedSourceVOlume();
    }
}

static void vUpdate_SelectedSourceVOlume(void)
{
    sT_AudioSource_t *pstAudioSrc = pstGetSelectedAudioSource();
    if(pstAudioSrc == NULL)
    {
        return;
    }
    if(uiNAV_COUNTS_PER_DETENT == 0U)
    {
        FHALT("Invalid encoder counts per detent");
        return;
    }

    uint32_t uiCurrentTime = k_uptime_get_32();

    if(stTVolumeEditState.bHasPreviousDirection && 
       stTVolumeEditState.ePreviousDirection != stTEncPosition.eDir)
    {
        stTVolumeEditState.iCountAccumulator = 0;
        stTVolumeEditState.bHasLastStepTime = false;
    }

    stTVolumeEditState.ePreviousDirection = stTEncPosition.eDir;
    stTVolumeEditState.bHasPreviousDirection = true;

    if(stTVolumeEditState.bHasLastStepTime)
    {
        uint32_t uiInactiveTime = uiCurrentTime - stTVolumeEditState.uiLastStepTime;
        if(uiInactiveTime > VOLUME_ACCEL_RESET_TIME_ms)
        {
            stTVolumeEditState.iCountAccumulator = 0;
            stTVolumeEditState.bHasLastStepTime = false;
        }        
    }

    stTVolumeEditState.iCountAccumulator += stTEncPosition.iPosition_Delta;
    int32_t iDetents = stTVolumeEditState.iCountAccumulator / (int32_t)uiNAV_COUNTS_PER_DETENT;
    if(iDetents == 0)
    {
        vUpdate_TrigTime(uiCurrentTime);
        return;
    }

    stTVolumeEditState.iCountAccumulator %= (int32_t)uiNAV_COUNTS_PER_DETENT;
    int32_t iMultiplier = 1;

    if(stTVolumeEditState.bHasLastStepTime)
    {
        uint32_t uiElapsedTime = uiCurrentTime - stTVolumeEditState.uiLastStepTime;
        
        if(uiElapsedTime > 0U)
        {
            uint32_t uiDetentCount = (uint32_t)abs((int)iDetents);
            uint32_t uiDetent_Per_Second = uiDetentCount * 1000U / uiElapsedTime;

            if(uiDetent_Per_Second >= VOLUME_VERY_FAST_SPEED_DPS)
            {
                iMultiplier = VOLUME_VERY_FAST_MULTIPLIER;
            }
            else if(uiDetent_Per_Second >= VOLUME_FAST_SPEED_DPS)
            {
                iMultiplier = VOLUME_FAST_MULTIPLIER;
            }
            else if(uiDetent_Per_Second >= VOLUME_MEDIUM_SPEED_DPS)
            {
                iMultiplier = VOLUME_MEDIUM_MULTIPLIER;
            }
        }
    }

    int32_t iVolumeChange = iDetents * iMultiplier;
    //Clamp the volume
    if(iVolumeChange > MAX_VOLUME_CHANGE_PER_UPDATE)
    {
        iVolumeChange = MAX_VOLUME_CHANGE_PER_UPDATE;
    }
    else if(iVolumeChange < -MAX_VOLUME_CHANGE_PER_UPDATE)
    {
        iVolumeChange = -MAX_VOLUME_CHANGE_PER_UPDATE;
    }

    int32_t iCurrentVolume = (int32_t)uiGet_AudioSrc_Volume(pstAudioSrc);    
    int32_t iNewVolume = iCurrentVolume + iVolumeChange;

    if(iNewVolume > (int32_t)MAX_AUDIO_SRC_VOLUME)
    {
        iNewVolume = (int32_t)MAX_AUDIO_SRC_VOLUME;
    }
    else if(iNewVolume < 0)
    {
        iNewVolume = 0;
    }
    
    vUpdate_SrcVolume(pstAudioSrc, (uint8_t)iNewVolume);
    //Notify the host
    vNotify_SrcVolumeChange(pstAudioSrc->eSrcId, (uint8_t)iNewVolume);

    stTVolumeEditState.uiLastStepTime = uiCurrentTime;
    stTVolumeEditState.bHasLastStepTime = true;
    vUpdate_TrigTime(uiCurrentTime);
}

static void vReset_VolumeEditState( void )
{
    stTVolumeEditState.uiLastStepTime = 0U;
    stTVolumeEditState.iCountAccumulator = 0;
    stTVolumeEditState.ePreviousDirection = eQDC_Dir_CW;
    stTVolumeEditState.bHasPreviousDirection = false;
    stTVolumeEditState.bHasLastStepTime = false;
}

static void vProcess_UIMode_SourceSelection( sT_QEncData_t *pstTEncData )
{
    if(bIsENCSWPressed())
    {
        vClear_EncoderTransientState(pstTEncData);
        vSet_UIMode(eUI_Mode_VolumeUpdate);
        vIndicate_VolUpdate_InVolControl();
        printf("Encoder SW Pressed, Setting Event to Update\n\r");
        return;
    }

    if(bIsENCRotationValid())
    {
        vNavigate_Sources();
        vUpdate_TrigTime(k_uptime_get_32());
    }
}

static void vIndicate_VolUpdate_InVolControl( void )
{
    if(eGet_UIMode() != eUI_Mode_VolumeUpdate)
    {
        FHALT("Invalid state for volume bar animation");
        return;
    }
    if(pstAudioSrcScreen == NULL)
    {
        FHALT("Invalid audio source screen");
        return;
    }

    lv_obj_t *pstVolParent = pstAudioSrcScreen->stTDisplayInfo.screenType.stTAudioSrcDisplay.pstSrcVolObj;
    if(pstVolParent == NULL)
    {
        FHALT("Invalid volume bar object");
        return;
    }

    lvgl_lock();
    lv_obj_t *pstVolBar = lv_obj_get_child_by_type(pstVolParent, 0, &lv_bar_class);
    if(pstVolBar == NULL)
    {
        FHALT("Invalid volume bar object");
        lvgl_unlock();
        return;
    }
    
    lv_anim_delete(pstVolBar, vVolumeBar_OpacityAnim);

    //Change bvolbar border color
    vNotify_VolBar_BorderStyleChange(pstVolBar, false);

    lv_anim_t stTVolBarAnim;
    lv_anim_init(&stTVolBarAnim);

    lv_anim_set_var(&stTVolBarAnim, pstVolBar);
    lv_anim_set_exec_cb(&stTVolBarAnim, vVolumeBar_OpacityAnim);
    lv_anim_set_values(&stTVolBarAnim, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_duration(&stTVolBarAnim, VOL_BAR_ANIM_DURATION_ms);
    lv_anim_set_path_cb(&stTVolBarAnim, lv_anim_path_ease_out);
    lv_anim_start(&stTVolBarAnim);
    lvgl_unlock();    
}

static void vNotify_VolBar_BorderStyleChange(lv_obj_t *pstVolBar, bool bIsExiting)
{
    lvgl_lock();

    if(pstVolBar == NULL)
    {
        lv_obj_t *pstVolParent = pstAudioSrcScreen->stTDisplayInfo.screenType.stTAudioSrcDisplay.pstSrcVolObj;
        pstVolBar = lv_obj_get_child_by_type(pstVolParent, 0, &lv_bar_class);
        if(pstVolBar == NULL)
        {
            lvgl_unlock();
            FHALT("Invalid volume bar object");
            return;
        }
    }

    if(bIsExiting)
    {
        lv_obj_set_style_outline_width(pstVolBar, 0U, LV_PART_MAIN);
        lv_obj_set_style_border_color(pstVolBar, lv_color_hex(VOL_BAR_BORDER_COLOR), LV_PART_MAIN);
        lv_obj_set_style_border_width(pstVolBar, VOL_BAR_BORDER_WIDTH, LV_PART_MAIN);
    }
    else
    {
        lv_obj_set_style_outline_color(pstVolBar, lv_color_hex(VOL_BAR_SELECTED_BORDER_COLOR), LV_PART_MAIN);
        lv_obj_set_style_outline_width(pstVolBar, VOL_BAR_SELECTED_BORDER_WIDTH, LV_PART_MAIN);
        lv_obj_set_style_outline_opa(pstVolBar, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_outline_pad(pstVolBar, 1, LV_PART_MAIN);
    }

    lvgl_unlock();
}

static void vVolumeBar_OpacityAnim(void *pvObj, int32_t iOpacity)
{
    lv_obj_t *pstBar = pvObj;

    lv_obj_set_style_opa(
        pstBar,
        (lv_opa_t)iOpacity,
        LV_PART_INDICATOR);
}

static void vProcess_Initial_State( sT_QEncData_t *pstTEncData )
{
    if (bIsENCSWPressed())
    {
        printf("Encoder SW Pressed, Setting Event to Update\n\r");
        vClear_EncoderTransientState(pstTEncData);;
        vSet_UIMode(eUI_Mode_VolumeUpdate);
        return;
    }

    if (!bIsENCRotationValid())
    {
        vClear_EncData(pstTEncData);
        return;
    }

    int32_t iMagnitude = abs((int)stTEncPosition.iPosition_Delta);
    if(iMagnitude < MIN_POS_DIFFERENCE_FOR_SOURCE_SELECT)
    {
        vClear_EncData(pstTEncData);
        return;        
    }

    vSet_UIMode(eUI_Mode_SourceSelect);
    vNavigate_Sources();
}

static void vClear_EncoderTransientState( sT_QEncData_t *pstQEncData )
{
    uint32_t uiCurrentPosition;
    eQDCModule_t eQDCModule = eQDC_0;

    if(pstQEncData != NULL)
    {
        eQDCModule = pstQEncData->stTEncPhaseData.eID;
    }

    if(bGet_eQDCPosition(eQDCModule, &uiCurrentPosition))
    {
        stTEncPosition.uiPosition_Previous = uiCurrentPosition;
        stTEncPosition.uiPosition_Current = uiCurrentPosition;
    }

    stTEncPosition.iPosition_Delta = 0;
    stTEncPosition.iNavigationCountAccumulator = 0;
    stTEncPosition.bIsSWPressed = false;
    stTEncPosition.bIsRotationValid = false;

    if(pstQEncData != NULL)
    {
        vClear_EncoderRotationData(pstQEncData);
    }

    vClear_eQDCMessageQueue();
    vClear_EncSWPressed();    
    vReset_VolumeEditState();
}

static bool bIsVolEditTimeOut( void )
{
    uint32_t uiCurrentTime = k_uptime_get_32();
    uint32_t uiElapsedTime = uiCurrentTime - uiGet_TrigTime();

    return (uiElapsedTime > VOLUME_EDIT_TIMEOUT_ms);
}

void vCheck_UIMode_Timeout( sT_QEncData_t *pstQEncData )
{
    if(eGet_UIMode() != eUI_Mode_VolumeUpdate || !bIsVolEditTimeOut())
    {
        return;
    }

    vClear_EncoderTransientState(pstQEncData);
    vNotify_VolBar_BorderStyleChange(NULL, true);
    vSet_UIMode(eUI_Mode_SourceSelect);
}

static void vNavigate_Sources( void )
{
    if(pstAudioSrcScreen == NULL || !bIsENCRotationValid())
        return;

    sT_AudioSource_t *pstCurrentAudioSrc = pstGetSelectedAudioSource();
    stTEncPosition.iNavigationCountAccumulator += stTEncPosition.iPosition_Delta;

    int32_t iAvailableSteps = stTEncPosition.iNavigationCountAccumulator / uiNAV_COUNTS_PER_DETENT;
    if(iAvailableSteps == 0)
        return;

    eQDC_Direction_t eNavigationDirection = (iAvailableSteps > 0) ? eQDC_Dir_CW : eQDC_Dir_CCW;
    uint32_t uiSteps = (uint32_t)abs((int)iAvailableSteps);
    if(uiSteps > NAV_MAX_STEPS_PER_UPDATE)
    {
        uiSteps = NAV_MAX_STEPS_PER_UPDATE;
    }

    stTEncPosition.iNavigationCountAccumulator %= uiNAV_COUNTS_PER_DETENT;
    
    eAudioSrc_Id_t eCurrentSrcId = (pstCurrentAudioSrc == NULL)?  eGetFirstActiveSource() : pstCurrentAudioSrc->eSrcId;
    if(eCurrentSrcId == eNUMBER_OF_AUDIO_SOURCES)
        return;
    
    eAudioSrc_Id_t eNextSrcId = eCurrentSrcId;
    sT_AudioSource_t *pstAudioSources =
        pstAudioSrcScreen->stTDisplayInfo.screenType.stTAudioSrcDisplay.staAudioSources;

    for(uint32_t i = 0; i < uiSteps; i++)
    {
        eNextSrcId = eFindNextSource(eNextSrcId, pstAudioSources, eNavigationDirection);
    }

    if(eNextSrcId == eCurrentSrcId)
        return;
        
    vUpdate_UI_ForSourceSelection(eCurrentSrcId, eNextSrcId);
    vReset_VolumeEditState();

}

static void vUpdate_UI_ForSourceSelection(eAudioSrc_Id_t ePrevSelectedSrc, eAudioSrc_Id_t eNewSelectedSrc)
{
    sT_AudioSource_t *pstAudioSources =
        pstAudioSrcScreen->stTDisplayInfo.screenType.stTAudioSrcDisplay.staAudioSources;
    if(ePrevSelectedSrc != eNUMBER_OF_AUDIO_SOURCES)
    {
        vClear_AudioSourceSelectedFlag(&pstAudioSources[ePrevSelectedSrc]);
    }
    if(eNewSelectedSrc != eNUMBER_OF_AUDIO_SOURCES)
    {
        vSet_AudioSourceSelectedFlag(&pstAudioSources[eNewSelectedSrc]);
    }    

    if(pstGetSelectedAudioSource() == NULL)
        return;

    lvgl_lock();

    if(ePrevSelectedSrc != eNUMBER_OF_AUDIO_SOURCES)
    {
        vClear_PrevSelectedSource_Style(&pstAudioSources[ePrevSelectedSrc]);
    }
    if(eNewSelectedSrc != eNUMBER_OF_AUDIO_SOURCES)
    {
        vSet_CurrentSelectedSource_Style(&pstAudioSources[eNewSelectedSrc]);
    }

    lvgl_unlock();
}

/// @brief No LVGL Lock/Unlock carried out in this function. So it has to be done from the caller
/// @param pstAudioSource 
static void vClear_PrevSelectedSource_Style(sT_AudioSource_t *pstAudioSource)
{
    if(pstAudioSource == NULL)
        return;
    
    sT_UIControl *pstUIIndicator_Control = pstGetUIObject(pstAudioSource->staUIControls, eUIObj_Indicator);
    if(pstUIIndicator_Control != NULL)
    {
        if(!bIs_UIIndicator_Visible(&pstUIIndicator_Control->uiObject.stTObj_Indicator))
        {
            vSet_Indicator_StateHide(pstUIIndicator_Control->pstUIObj);
        }
        else
        {
            vSet_Indicator_StateVisible(pstUIIndicator_Control->pstUIObj);
        }
    }

    sT_UIControl *pstUILabel = pstGetUIObject(pstAudioSource->staUIControls, eUIObj_Label);    
    lv_obj_set_style_text_color(pstUILabel->pstUIObj, lv_color_hex(ACTIVE_AUDIO_SRC_TEXT_COLOR), LV_PART_MAIN);

    lv_obj_set_style_bg_color(pstAudioSource->pstAudSrcObj, lv_color_hex(SOURCE_LIST_BACKGROUND_COLOR), LV_PART_MAIN);    
}

/// @brief No LVGL Lock/Unlock carried out in this function. So it has to be done from the caller
/// @param pstAudioSource 
static void vSet_CurrentSelectedSource_Style(sT_AudioSource_t *pstAudioSource)
{
    if(pstAudioSource == NULL)
        return;
    
    vSet_Indicator_AtSourceSelected(pstAudioSource);
    
    sT_UIControl *pstUILabel = pstGetUIObject(pstAudioSource->staUIControls, eUIObj_Label);    
    lv_obj_set_style_text_color(pstUILabel->pstUIObj, lv_color_hex(AUDIO_SRC_SELECTED_TEXT_COLOR), LV_PART_MAIN);

    lv_obj_set_style_bg_color(pstAudioSource->pstAudSrcObj, lv_color_hex(AUDIO_SRC_SELECTED_COLOR), LV_PART_MAIN);    
    lv_obj_scroll_to_view(pstAudioSource->pstAudSrcObj, LV_ANIM_ON);
    
    vUpdate_VolumeBar_AtSrcSelected(pstAudioSource);
}

static inline void vSet_AudioSrc_Volume(sT_AudioSource_t *pstAudioSource, uint8_t uiVol)
{
    if(pstAudioSource == NULL)
        return;

    atomic_store_explicit(&pstAudioSource->uiVolume, uiVol, memory_order_release);    
}

static inline uint8_t uiGet_AudioSrc_Volume(sT_AudioSource_t *pstAudioSource)
{
    if(pstAudioSource == NULL)
        return 0U;
    uint8_t uiVol = atomic_load_explicit(&pstAudioSource->uiVolume, memory_order_acquire);
    return uiVol;
}

static void vUpdate_VolumeBar_AtSrcSelected(sT_AudioSource_t *pstAudioSource)
{   
    if(!bIsAudioSrcSelected(pstAudioSource))
        return;

    sT_UIScreenDisplay *pstDisplayScreen = pstGetDisplayScreen(eScreen_SourceSelect);

    sT_AudioSrc_Display_t *pstAudioDisplay = &pstDisplayScreen->screenType.stTAudioSrcDisplay;

    lv_obj_t *pstVolParent = pstAudioDisplay->pstSrcVolObj;

    uint8_t uiVol = uiGet_AudioSrc_Volume(pstAudioSource);
    vUpdate_UI_VolControl(pstVolParent, uiVol);
}

static void vUpdate_UI_VolControl(lv_obj_t *pstVolParent, uint8_t uiVol)
{
    if(pstVolParent == NULL)
        return;

    lvgl_lock();

    lv_obj_t *pstVolBar = lv_obj_get_child_by_type(pstVolParent, 0, &lv_bar_class);
    if(pstVolBar != NULL)
    {
        lv_bar_set_value(pstVolBar, uiVol, LV_ANIM_ON);
    }

    lv_obj_t *pstVolumeLabel = lv_obj_get_child_by_type(pstVolParent, 1, &lv_label_class);
    if(pstVolumeLabel != NULL)
    {
        lv_label_set_text_fmt(
            pstVolumeLabel,
            "%u",
            (unsigned int)uiVol);
    }

    lvgl_unlock();
}

void vUpdate_SrcVolume(sT_AudioSource_t *pstAudioSource, uint8_t uiVol)
{
    if(pstAudioSource == NULL)
        return;

    if(uiVol > MAX_AUDIO_SRC_VOLUME)
    {
        uiVol = MAX_AUDIO_SRC_VOLUME;
        FHALT("Invalid Vol(%d) received for Src: %d", uiVol, pstAudioSource->eSrcId);
    }

    vSet_AudioSrc_Volume(pstAudioSource, uiVol);
    if(!bIsAudioSrcSelected(pstAudioSource))
        return;

    sT_UIScreenDisplay *pstDisplayScreen = pstGetDisplayScreen(eScreen_SourceSelect);

    sT_AudioSrc_Display_t *pstAudioDisplay = &pstDisplayScreen->screenType.stTAudioSrcDisplay;

    sT_UIObj_VolControl *pstVolCtrl = &pstAudioDisplay->stTVolCtrl;

    atomic_store_explicit(&pstVolCtrl->uiVol,uiVol,memory_order_release);

    lv_obj_t *pstVolParent = pstAudioDisplay->pstSrcVolObj;
    vUpdate_UI_VolControl(pstVolParent, uiVol);
}

void vUpdate_AudioSourceIndicatorVisibility( bool bIsVisible )
{
    if(pstAudioSrcScreen == NULL)
    {
        FHALT("Audio source screen is not initialized");
        return;
    }

    for(uint8_t i = eAudio_Src_0; i < eNUMBER_OF_AUDIO_SOURCES; i++)
    {
        sT_AudioSource_t *pstAudioSrc = &pstAudioSrcScreen->stTDisplayInfo.screenType.stTAudioSrcDisplay.staAudioSources[i];
        if(!bIsAudioSourceVisible(pstAudioSrc))
            continue;

        sT_UIControl *pstUIIndicator_Control = pstGetUIObject(pstAudioSrc->staUIControls, eUIObj_Indicator);
        if(pstUIIndicator_Control == NULL)
            continue;

        if(bIsVisible)
        {
            vSet_UIIndicator_Visibility(&pstUIIndicator_Control->uiObject.stTObj_Indicator);
        }
        else
        {
            vClear_UIIndicator_Visibility(&pstUIIndicator_Control->uiObject.stTObj_Indicator);
        }
    }
}

static sT_UIControl *pstGetUIObject(sT_UIControl *pstUIControls, eUI_Obj_Type_t eObjType)
{
    if(pstUIControls == NULL)
        return NULL;
    for(uint8_t i = 0; i < NUMBER_OF_UI_CONTROLS_PER_AUDIO_SOURCE; i++)
    {
        if(pstUIControls[i].eObjType == eObjType)
            return &pstUIControls[i];
    }
    return NULL;
}

static inline void vSet_UIIndicator_Visibility(sT_UIObj_Indicator *pstObj_Indicator)
{
    if(pstObj_Indicator == NULL)
        return;
    atomic_store_explicit(&pstObj_Indicator->bIsVisible, true, memory_order_release);
}

static inline bool bIs_UIIndicator_Visible(sT_UIObj_Indicator *pstObj_Indicator)
{
    if(pstObj_Indicator == NULL)
        return false;
    bool bRes = atomic_load_explicit(&pstObj_Indicator->bIsVisible, memory_order_acquire);
    return bRes;
}

static inline void vClear_UIIndicator_Visibility(sT_UIObj_Indicator *pstObj_Indicator)
{
    if(pstObj_Indicator == NULL)
        return;
    atomic_store_explicit(&pstObj_Indicator->bIsVisible, false, memory_order_release);
}

static eAudioSrc_Id_t eFindNextSource(eAudioSrc_Id_t eCurrentId, 
                                      sT_AudioSource_t *pstCurrentAudioSrcs,
                                      eQDC_Direction_t eDir)
{
    int32_t iIndex = (int32_t)eCurrentId;

    for(uint8_t i = 0U; i < eNUMBER_OF_AUDIO_SOURCES; i++)
    {
        if(eDir == eQDC_Dir_CW)
        {
            iIndex++;
            if(iIndex >= (int32_t)eNUMBER_OF_AUDIO_SOURCES)
                iIndex = eAudio_Src_0;                
        }
        else
        {
            iIndex--;
            if(iIndex < (int32_t)eAudio_Src_0)
                iIndex = eNUMBER_OF_AUDIO_SOURCES - 1;
        }

        if(bIsAudioSourceActive(&pstCurrentAudioSrcs[iIndex]) && 
           bIsAudioSourceVisible(&pstCurrentAudioSrcs[iIndex]))
           return (eAudioSrc_Id_t)iIndex;
    }

    return eCurrentId;
}

static void vClear_EncData(sT_QEncData_t *pstQEncData)
{
    if(pstQEncData == NULL)
        return;
    vClear_EncSWPressed();
}

static inline eAudioSrc_Id_t eGetFirstActiveSource( void )
{
    sT_AudioSource_t *pstAudioSrc =
        pstAudioSrcScreen->stTDisplayInfo.screenType.stTAudioSrcDisplay.staAudioSources;
    for(uint8_t i = eAudio_Src_0; i < eNUMBER_OF_AUDIO_SOURCES; i++)
    {
        if(!bIsAudioSourceActive(&pstAudioSrc[i]) ||
           !bIsAudioSourceVisible(&pstAudioSrc[i]))
            continue;
        if(bIsAudioSourceActive(&pstAudioSrc[i]))
            return pstAudioSrc[i].eSrcId;
    }
    return eNUMBER_OF_AUDIO_SOURCES;    
}

static sT_AudioSource_t *pstGetSelectedAudioSource( void )
{
    sT_AudioSource_t *pstAudioSrc =
        pstAudioSrcScreen->stTDisplayInfo.screenType.stTAudioSrcDisplay.staAudioSources;

    for(uint8_t i = eAudio_Src_0; i < eNUMBER_OF_AUDIO_SOURCES; i++)
    {
        if(!bIsAudioSourceActive(&pstAudioSrc[i]) ||
           !bIsAudioSourceVisible(&pstAudioSrc[i]))
            continue;
        if(bIsAudioSrcSelected(&pstAudioSrc[i]))
            return &pstAudioSrc[i];
    }

    return NULL;
}

static inline void vSet_UIMode(eUI_Mode_t eMode)
{
    if(eMode >= eNUMBER_OF_SCREEN_EVENTs)
    {
        FHALT("Invalid Mode : %d", eMode);
        return;
    }

    atomic_store_explicit(&stTScreenEventCtrl.eCurrentUIMode, eMode, memory_order_release);
    vUpdate_TrigTime(k_uptime_get_32());
}

static inline eUI_Mode_t eGet_UIMode( void )
{
    eUI_Mode_t eMode = atomic_load_explicit(&stTScreenEventCtrl.eCurrentUIMode, memory_order_acquire);
    return eMode;
}

static inline void vUpdate_TrigTime(uint32_t uiTime)
{
    atomic_store_explicit(&stTScreenEventCtrl.uiEventTrigTime, uiTime, memory_order_release);
}

static inline uint32_t uiGet_TrigTime( void )
{
    uint32_t uiTime = atomic_load_explicit(&stTScreenEventCtrl.uiEventTrigTime, memory_order_acquire);
    return uiTime;
}

void vSetup_AudioSrc_ScreenStartup( void )
{    
    sT_AudioSource_t *pstAudioSrc =
        pstAudioSrcScreen->stTDisplayInfo.screenType.stTAudioSrcDisplay.staAudioSources;

    for(uint8_t i = eAudio_Src_0; i < eNUMBER_OF_AUDIO_SOURCES; i++)
    {
        if(!bIsAudioSourceActive(&pstAudioSrc[i]) ||
           !bIsAudioSourceVisible(&pstAudioSrc[i]))
            continue;
        vUpdate_UI_ForSourceSelection(eNUMBER_OF_AUDIO_SOURCES, i);
        vSet_UIMode(eUI_Mode_SourceSelect);
        return;
    }    
}

//*********************************************************************************** */
#pragma region UI Creation

void vSetup_AudioSourceList( sT_UIScreen_t *pstUIScreen )
{
    pstUIScreen->pfCreate(pstUIScreen);
    vSet_ScreenActive(eScreen_SourceSelect);
}

void vInit_SourceSelectScreen_Controls(sT_UIScreen_t *pstAudioSrcSelect)
{
    if(pstAudioSrcSelect == NULL)
    {
        FHALT("Audio source screen is NULL");
        return;
    }

    pstAudioSrcScreen = pstAudioSrcSelect;
    pstAudioSrcSelect->pfCreate = vCreate_SourceList;
    pstAudioSrcSelect->stTDisplayInfo.eScreenId = eScreen_SourceSelect;

    for(uint8_t uiSrc = eAudio_Src_0; uiSrc < eNUMBER_OF_AUDIO_SOURCES; uiSrc++)
    {
        sT_AudioSource_t *pstAudSrc =
            &pstAudioSrcSelect->stTDisplayInfo.screenType.stTAudioSrcDisplay.staAudioSources[uiSrc];
        sT_UIControl *pstUIControl = pstAudSrc->staUIControls;

        pstAudSrc->eSrcId = uiSrc;
        
        //Setup Label Physical Positions
        pstUIControl[0].eObjType = eUIObj_Label;
        sT_UIObj_Label *pstLabel = &pstUIControl[0].uiObject.stTObj_Label;
        memcpy(pstLabel, &stTAdSrc_DefaultLabelSettings, sizeof(sT_UIObj_Label));

        //Setup Indicator Physical Positions
        pstUIControl[1].eObjType = eUIObj_Indicator;
        sT_UIObj_Indicator *pstIndicator = &pstUIControl[1].uiObject.stTObj_Indicator;
        memcpy(pstIndicator, &stTAdSrc_DefaultIndicatorSettings, sizeof(sT_UIObj_Indicator));
        
    }
    
    sT_UIObj_VolControl *pstVolCtrl = &pstAudioSrcSelect->stTDisplayInfo.screenType.stTAudioSrcDisplay.stTVolCtrl;
    memcpy(pstVolCtrl, &stTAdSrc_DefaultVolCtrlSettings, sizeof(sT_UIObj_VolControl));
}

static void vCreate_SourceList( sT_UIScreen_t *pstSoucreScreen )
{
    lv_obj_t *pstAudioSrcList;

    lvgl_lock();
    pstSoucreScreen->pstScreenObj = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(pstSoucreScreen->pstScreenObj, lv_color_hex(SCREEN_BACKGROUND_COLOR_HEX), LV_PART_MAIN);

    lv_obj_t *pstSeparator = lv_line_create(pstSoucreScreen->pstScreenObj);
    lv_line_set_points(pstSeparator, screenVertical_SeparatorPoints, 2);
    lv_obj_set_style_line_width(pstSeparator, SEPARATOR_WIDTH, LV_PART_MAIN);
    lv_obj_set_style_line_color(pstSeparator, lv_color_hex(SEPARATOR_COLOR), LV_PART_MAIN);
    lv_obj_set_style_line_rounded(pstSeparator, false, LV_PART_MAIN);

    pstAudioSrcList = lv_list_create(pstSoucreScreen->pstScreenObj);

    lv_obj_set_size(pstAudioSrcList, AUDIO_SOURCE_LIST_DISPLAY_WIDTH, AUDIO_SOURCE_LIST_DISPLAY_HEIGHT);
    lv_obj_set_style_radius(pstAudioSrcList, 0U, LV_PART_MAIN);
    lv_obj_set_style_border_width(pstAudioSrcList, 0U, LV_PART_MAIN);
    lv_obj_set_style_pad_all(pstAudioSrcList, 0U, LV_PART_MAIN);
    lv_obj_set_style_bg_color(pstAudioSrcList, lv_color_hex(SOURCE_LIST_BACKGROUND_COLOR), LV_PART_MAIN);
    lv_obj_set_style_pad_row(pstAudioSrcList, PADDING_BETWEEN_TWO_SOURCES, LV_PART_MAIN);
    lv_obj_add_flag(pstAudioSrcList, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_width(pstAudioSrcList, AUDIO_SRC_SCROLLBAR_WIDTH, LV_PART_SCROLLBAR);
    lv_obj_set_scroll_dir(pstAudioSrcList, LV_DIR_VER);
    lv_obj_align(pstAudioSrcList, LV_ALIGN_LEFT_MID, 0, 0);

    sT_AudioSrc_Display_t *pstAudioSrcDisplay =
        &pstSoucreScreen->stTDisplayInfo.screenType.stTAudioSrcDisplay;
    sT_AudioSource_t *pstAudioSrc = pstAudioSrcDisplay->staAudioSources;
    for(uint8_t i = 0; i < eNUMBER_OF_AUDIO_SOURCES; i++)
    {
        if(!bIsAudioSourceVisible(&pstAudioSrc[i]))
            continue;
        
        sT_UIControl *pstUIControl = pstAudioSrc[i].staUIControls;
        lv_obj_t *pstSrcContainer = lv_obj_create(pstAudioSrcList);
        lv_obj_clear_flag(pstSrcContainer, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scrollbar_mode(pstSrcContainer, LV_SCROLLBAR_MODE_OFF);

        lv_obj_set_size(pstSrcContainer, AUDIO_SRC_CONTAINER_WIDTH, AUDIO_SRC_CONTAINER_HEIGHT);
        lv_obj_set_style_radius(pstSrcContainer, 0U, LV_PART_MAIN);
        lv_obj_set_style_border_width(pstSrcContainer, 0U, LV_PART_MAIN);
        lv_obj_set_style_pad_all(pstSrcContainer, 0U, LV_PART_MAIN);
        lv_obj_set_style_bg_color(pstSrcContainer, lv_color_hex(SOURCE_LIST_BACKGROUND_COLOR), LV_PART_MAIN);
                
        for(uint8_t j = 0; j < NUMBER_OF_UI_CONTROLS_PER_AUDIO_SOURCE; j++)
        {
            switch(pstUIControl[j].eObjType)
            {
                case eUIObj_Label:
                    pstUIControl[j].pstUIObj = pstCreate_UILabel(pstSrcContainer, 
                                                                 &pstUIControl[j].uiObject.stTObj_Label, 
                                                                 &pstAudioSrc[i]);
                    vUpdate_ContainerHeight_RefTo_LabelHeight(pstSrcContainer, 
                                                              pstUIControl[j].pstUIObj, 
                                                              &pstAudioSrc[i], pstUIControl[j].uiObject.stTObj_Label.ly);                    
                    break;
                case eUIObj_Indicator:
                    pstUIControl[j].pstUIObj = pstCreate_UIIndicator(pstSrcContainer,
                                                                     &pstUIControl[j].uiObject.stTObj_Indicator,
                                                                     &pstAudioSrc[i]);
                    break;
                default:
                    FHALT("Obj Type: %d, not implemented yet.", pstUIControl[j].eObjType);
                    break;
            }
        }

        lv_obj_t *pstSourceSeparator = lv_line_create(pstSrcContainer);
        lv_line_set_points(pstSourceSeparator, pstAudioSrc[i].sourceHoriz_SeparatorPoints, 2);
        lv_obj_set_style_line_width(pstSourceSeparator, SEPARATOR_WIDTH, LV_PART_MAIN);
        lv_obj_set_style_line_color(pstSourceSeparator, lv_color_hex(SEPARATOR_COLOR), LV_PART_MAIN);
        lv_obj_set_style_line_rounded(pstSourceSeparator, false, LV_PART_MAIN);

        pstAudioSrc[i].pstAudSrcObj = pstSrcContainer;
    }

    sT_UIObj_VolControl *pstVolCtrl = &pstSoucreScreen->stTDisplayInfo.screenType.stTAudioSrcDisplay.stTVolCtrl;
    pstAudioSrcDisplay->pstSrcVolObj =
        pstCreate_UIVolControl(pstSoucreScreen->pstScreenObj,
                               pstVolCtrl);
    vUpdate_IndicatorLocation_AtScrollVisible(pstAudioSrcList);

    lvgl_unlock();
}

static lv_obj_t *pstCreate_UIVolControl(lv_obj_t *pstParent, sT_UIObj_VolControl *pstObj_VolCtrl)
{
    lv_obj_t *pstVolInfoContainer = lv_bar_create(pstParent);
    lv_obj_set_size(pstVolInfoContainer, pstObj_VolCtrl->iWidth, pstObj_VolCtrl->iHeight);
    lv_obj_set_pos(pstVolInfoContainer, pstObj_VolCtrl->lx_Top, pstObj_VolCtrl->ly_Top);
    lv_obj_set_style_bg_color(pstVolInfoContainer, lv_color_hex(SCREEN_BACKGROUND_COLOR_HEX), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(pstVolInfoContainer, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_t *pstLabel_VolTitle = lv_label_create(pstVolInfoContainer);
    lv_obj_set_size(pstLabel_VolTitle, pstObj_VolCtrl->iWidth, VOL_BAR_TITLE_LABEL_HEIGHT);
    lv_obj_align(pstLabel_VolTitle, LV_ALIGN_TOP_MID, 0U, 3U);
    lv_label_set_text(pstLabel_VolTitle, (const char*)"Vol");
    lv_obj_set_style_text_align(pstLabel_VolTitle, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(pstLabel_VolTitle, pstGetText_Font(VOL_BAR_TEXT_FONT_SIZE), LV_PART_MAIN);
    lv_obj_set_style_text_color(pstLabel_VolTitle, lv_color_hex(VOL_BAR_TEXT_COLOR), LV_PART_MAIN);

    lv_obj_t *pstLabel_VolValue = lv_label_create(pstVolInfoContainer);
    lv_obj_set_size(pstLabel_VolValue, pstObj_VolCtrl->iWidth, VOL_BAR_TITLE_LABEL_HEIGHT);
    lv_obj_align(pstLabel_VolValue, LV_ALIGN_BOTTOM_MID, 0U, -1U);
    lv_label_set_text_fmt(pstLabel_VolValue, "%u", (unsigned int)pstObj_VolCtrl->uiVol);
    lv_obj_set_style_text_align(pstLabel_VolValue, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(pstLabel_VolValue, pstGetText_Font(VOL_BAR_TEXT_FONT_SIZE), LV_PART_MAIN);
    lv_obj_set_style_text_color(pstLabel_VolValue, lv_color_hex(VOL_BAR_TEXT_COLOR), LV_PART_MAIN);    

    lv_obj_t *pstVolBar = lv_bar_create(pstVolInfoContainer);

    lv_obj_set_size(pstVolBar, VOL_BAR_WIDTH, VOL_BAR_HEIGHT);
    lv_obj_align(pstVolBar, LV_ALIGN_CENTER, 0U, 2U);
    lv_bar_set_orientation(pstVolBar, LV_BAR_ORIENTATION_VERTICAL);
    lv_bar_set_range(pstVolBar, 0, 100);
    lv_bar_set_value(pstVolBar, pstObj_VolCtrl->uiVol, LV_ANIM_ON);

    lv_obj_set_style_border_width(pstVolBar, VOL_BAR_BORDER_WIDTH, LV_PART_MAIN);
    lv_obj_set_style_border_color(pstVolBar, lv_color_hex(VOL_BAR_BORDER_COLOR), LV_PART_MAIN);
    lv_obj_set_style_border_opa(pstVolBar, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_set_style_radius(pstVolBar, 0U, LV_PART_MAIN);
    lv_obj_set_style_radius(pstVolBar, 0U, LV_PART_INDICATOR);

    lv_obj_set_style_bg_color(pstVolBar, lv_color_hex(SCREEN_BACKGROUND_COLOR_HEX), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(pstVolBar, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_set_style_bg_color(pstVolBar, lv_color_hex(VOL_BAR_INDICATOR_COLOR), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(pstVolBar, LV_OPA_COVER, LV_PART_INDICATOR);

    lv_obj_clear_flag(pstVolBar, LV_OBJ_FLAG_SCROLLABLE);
    return pstVolInfoContainer;
}

static lv_obj_t *pstCreate_UIIndicator(lv_obj_t *pstParent, sT_UIObj_Indicator *pstObj_Indicator, sT_AudioSource_t *pstAudioSrc)
{
    ARG_UNUSED(pstAudioSrc);

    lv_obj_t *pstIndicator = lv_obj_create(pstParent);

    lv_obj_set_size(pstIndicator, pstObj_Indicator->iwidth, pstObj_Indicator->iheight);
    lv_obj_set_pos(pstIndicator, pstObj_Indicator->lx, pstObj_Indicator->ly);
    lv_obj_set_style_radius(pstIndicator, 0U, LV_PART_MAIN);

    if(!bIs_UIIndicator_Visible(pstObj_Indicator))
    {
        vSet_Indicator_StateHide(pstIndicator);
    }
    else
    {
        vSet_Indicator_StateVisible(pstIndicator);
    }
    
    lv_obj_clear_flag(pstIndicator, LV_OBJ_FLAG_SCROLLABLE);

    return pstIndicator;
}

static void vUpdate_IndicatorLocation_AtScrollVisible( lv_obj_t *pstAudioSrcList )
{
    sT_UIScreenDisplay *pstAudioSrcScreen = pstGetDisplayScreen(eScreen_SourceSelect);
    if(pstAudioSrcScreen == NULL)
        return;

    sT_AudioSource_t *pstAudioSrc =
        pstAudioSrcScreen->screenType.stTAudioSrcDisplay.staAudioSources;
    lv_obj_update_layout(pstAudioSrcList);

    bool bScrollbarRequired = (lv_obj_get_scroll_top(pstAudioSrcList) > 0) || (lv_obj_get_scroll_bottom(pstAudioSrcList) > 0);   
    if(!bScrollbarRequired)
        return;
    
    lv_coord_t _newX = stTAdSrc_DefaultIndicatorSettings.lx;
    _newX -= (AUDIO_SRC_SCROLLBAR_WIDTH + INDICATOR_SCROLLBAR_GAP);

    for(uint8_t i = 0; i < eNUMBER_OF_AUDIO_SOURCES; i++)
    {
        if(!bIsAudioSourceVisible(&pstAudioSrc[i]))
            continue;
        
        sT_UIControl *pstUIControl = pstGetUIObject(pstAudioSrc[i].staUIControls, eUIObj_Indicator);
        if(pstUIControl == NULL)
            continue;
        lv_obj_set_x(pstUIControl->pstUIObj, _newX);
    }    
}

static void vSet_Indicator_StateHide(lv_obj_t *pstIndicator)
{
    lv_obj_add_flag(pstIndicator, LV_OBJ_FLAG_HIDDEN);
}

static void vSet_Indicator_StateVisible(lv_obj_t *pstIndicator)
{
    lv_obj_clear_flag(pstIndicator, LV_OBJ_FLAG_HIDDEN);

    lv_obj_set_style_border_width(pstIndicator, INDICATOR_BORDER_WIDTH, LV_PART_MAIN);
    lv_obj_set_style_border_color(pstIndicator, lv_color_hex(INDICATOR_BORDER_COLOR), LV_PART_MAIN);
    lv_obj_set_style_border_opa(pstIndicator, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_set_style_bg_color(pstIndicator, lv_color_hex(SOURCE_LIST_BACKGROUND_COLOR), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(pstIndicator, LV_OPA_COVER, LV_PART_MAIN);    
}

static void vSet_Indicator_AtSourceSelected(sT_AudioSource_t *pstAudioSource)
{
    sT_UIControl *pstUIIndicator_Control = pstGetUIObject(pstAudioSource->staUIControls, eUIObj_Indicator);
    if(pstUIIndicator_Control == NULL)
        return;

    lv_obj_t *pstIndicator = pstUIIndicator_Control->pstUIObj;
    if(!bIs_UIIndicator_Visible(&pstUIIndicator_Control->uiObject.stTObj_Indicator))
    {
        vSet_Indicator_StateHide(pstIndicator);
    }
    else
    {
        vSet_Indicator_StateVisible(pstIndicator);
    }
}

static lv_obj_t *pstCreate_UILabel(lv_obj_t *pstParent, sT_UIObj_Label *pstTObj_Label, sT_AudioSource_t *pstAudioSrc)
{
    lv_obj_t *pstLabel = lv_label_create(pstParent);

    lv_obj_set_width(pstLabel, pstTObj_Label->iwidth);
    lv_label_set_long_mode(pstLabel, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_align(pstLabel, LV_ALIGN_LEFT_MID, 0U, 0U);
    lv_label_set_text(pstLabel, (const char*)pstAudioSrc->acName);
    lv_obj_set_style_text_align(pstLabel, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
    lv_obj_set_style_text_font(pstLabel, pstGetText_Font(AUDIO_SRC_TEXT_FONT_SIZE), LV_PART_MAIN);
    lv_obj_set_pos(pstLabel, pstTObj_Label->lx, pstTObj_Label->ly);

    if(!bIsAudioSourceActive(pstAudioSrc))
    {
        pstTObj_Label->lcolor_Text = lv_color_hex(INACTIVE_AUDIO_SRC_TEXT_COLOR);
    }
    else if(bIsAudioSrcMuted(pstAudioSrc))
    {
        pstTObj_Label->lcolor_Text = lv_color_hex(MUTE_AUDIO_SRC_TEXT_COLOR);
    }
    else
    {
        pstTObj_Label->lcolor_Text = lv_color_hex(ACTIVE_AUDIO_SRC_TEXT_COLOR);
    }

    lv_obj_set_style_text_color(pstLabel, pstTObj_Label->lcolor_Text, LV_PART_MAIN);

    return pstLabel;
}

void vUpdate_ContainerHeight_RefTo_LabelHeight(lv_obj_t *pstContainer, lv_obj_t *pstLabel, sT_AudioSource_t *pstAudioSrc, lv_coord_t stRefY)
{

    lv_obj_update_layout(pstLabel);
    int32_t iLabelHeight = lv_obj_get_height(pstLabel);

    lv_obj_update_layout(pstContainer);
    int32_t iContainer_Height = lv_obj_get_height(pstContainer);
    
    int32_t iDiff = (stRefY + iLabelHeight + 2U) - iContainer_Height;
    if(iDiff <= 0)
    {
        return;
    }
    
    iContainer_Height += (iDiff + 2U);
    lv_obj_set_height(pstContainer, iContainer_Height);
    pstAudioSrc->sourceHoriz_SeparatorPoints[0].y = iContainer_Height - 1U;
    pstAudioSrc->sourceHoriz_SeparatorPoints[1].y = pstAudioSrc->sourceHoriz_SeparatorPoints[0].y;

    printf("Height Violated @Name: %s\n\r", pstAudioSrc->acName);
       
}

static const lv_font_t * const pstGetText_Font( eFontSize_t eFSize )
{
    switch(eFSize)
    {
        case eFont_Size_10:
            return &lv_font_montserrat_10; 
        case eFont_Size_12:
            return &lv_font_montserrat_12; 
        case eFont_Size_14:
            return &lv_font_montserrat_14; 
        case eFont_Size_16:
            return &lv_font_montserrat_16; 
        case eFont_Size_18:
            return &lv_font_montserrat_18; 
        case eFont_Size_20:
            return &lv_font_montserrat_20; 
/*         case eFont_Size_22:
            return &lv_font_montserrat_22; 
        case eFont_Size_24:
            return &lv_font_montserrat_24; 
        case eFont_Size_26:
            return &lv_font_montserrat_26; 
        case eFont_Size_28:
            return &lv_font_montserrat_28; 
        case eFont_Size_30:
            return &lv_font_montserrat_30; 
        case eFont_Size_32:
            return &lv_font_montserrat_32; 
        case eFont_Size_34:
            return &lv_font_montserrat_34; 
        case eFont_Size_36:
            return &lv_font_montserrat_36; 
        case eFont_Size_38:
            return &lv_font_montserrat_38; 
        case eFont_Size_40:
            return &lv_font_montserrat_40; 
        case eFont_Size_42:
            return &lv_font_montserrat_42; 
        case eFont_Size_44:
            return &lv_font_montserrat_44; 
        case eFont_Size_46:
            return &lv_font_montserrat_46; 
        case eFont_Size_48:
            return &lv_font_montserrat_48; */ 
        default:
            FHALT("Invalid Font Size: %d", eFSize);
            return &lv_font_montserrat_10;        
    }
}

#pragma endregion

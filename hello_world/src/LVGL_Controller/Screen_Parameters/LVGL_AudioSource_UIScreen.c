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
    eSource_Event_None,
    eSource_Event_Select,
    eSource_Event_Update,
    eNUMBER_OF_SCREEN_EVENTs
} eScreen_Event_t;

typedef struct
{
    _Atomic eScreen_Event_t eCurrentEvent;
    _Atomic uint32_t uiEventTrigTime;
} sT_ScreenEvent_Control_t;

typedef struct
{
    uint32_t uiPosition_Previous;
    uint32_t uiPosition_Current;
    int32_t iPosition_Delta;
    eQDC_Direction_t eDir;

    bool bIsSWPressed;
    bool bIsRotationValid;
} sT_EncPosition_Update_t;

sT_ScreenEvent_Control_t stTScreenEventCtrl = {
    .eCurrentEvent = eSource_Event_None,
    .uiEventTrigTime = 0U
};

sT_EncPosition_Update_t stTEncPosition = {
    .uiPosition_Previous = 0U,
    .uiPosition_Current = 0U,
    .iPosition_Delta = 0,
    .bIsRotationValid = false,
    .bIsSWPressed = false
};


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
void vUpdate_ContainerHeight_RefTo_LabelHeight(lv_obj_t *pstContainer, lv_obj_t *pstLabel, sT_AudioSource_t *pstAudioSrc, lv_coord_t stRefY);
static const lv_font_t * const pstGetText_Font( eFontSize_t eFSize );
static sT_AudioSource_t *pstGetSelectedAudioSource( void );
static inline eAudioSrc_Id_t eGetFirstActiveSource( void );
static void vUpdate_UI_ForSourceSelection(eAudioSrc_Id_t ePrevSelectedSrc, eAudioSrc_Id_t eNewSelectedSrc);
static void vClear_PrevSelectedSource_Style(sT_AudioSource_t *pstAudioSource);
static void vSet_CurrentSelectedSource_Style(sT_AudioSource_t *pstAudioSource);

static inline void vSet_ScreenEvent(eScreen_Event_t eEvent);
static inline eScreen_Event_t eGet_ScreenEvent( void );
static inline void vUpdate_CurrentEventState( void );
static inline void vUpdate_TrigTime(uint32_t uiTime);
static inline uint32_t uiGet_TrigTime( void );

static void vProcess_Encoder_Data(sT_QEncData_t *pstTEncData);
static void vProcess_Initial_State( sT_QEncData_t *pstTEncData );
static void vProcess_SourceSelection_State( sT_QEncData_t *pstTEncData );
static void vNavigate_Sources( void );
static eAudioSrc_Id_t eFindNextSource(eAudioSrc_Id_t eCurrentId, 
                                      sT_AudioSource_t *pstCurrentAudioSrcs,
                                      eQDC_Direction_t eDir);

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

    vUpdate_CurrentEventState();
    eScreen_Event_t eEvent = eGet_ScreenEvent();

    switch(eEvent)
    {
        case eSource_Event_None:
            vProcess_Initial_State( pstQEncData );         
            break;
        case eSource_Event_Select://Select the sources
            vProcess_SourceSelection_State( pstQEncData );
            break;
        case eSource_Event_Update://Updating the parameters
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

static void vProcess_SourceSelection_State( sT_QEncData_t *pstTEncData )
{
    if(bIsENCSWPressed())
    {
        vClear_EncSWPressed();
        vSet_ScreenEvent(eSource_Event_Update);
        return;
    }

    if(bIsENCRotationValid())
    {
        vNavigate_Sources();
        vUpdate_TrigTime(k_uptime_get_32());
    }
}

static void vNavigate_Sources( void )
{
    if(pstAudioSrcScreen == NULL || !bIsENCRotationValid())
        return;

    sT_AudioSource_t *pstCurrentAudioSrc = pstGetSelectedAudioSource();
    uint32_t uiMagnitude = 0U;

    if(stTEncPosition.iPosition_Delta < 0)
    {
        uiMagnitude = (uint32_t)(-stTEncPosition.iPosition_Delta);
    }
    else
    {
        uiMagnitude = (uint32_t)(stTEncPosition.iPosition_Delta);
    }

    uint32_t uiSteps = uiMagnitude / NAV_COUNT_PER_DETENT;
    if(uiSteps == 0U)
        return;
    if(uiSteps > NAV_MAX_STEPS_PER_UPDATE)
        uiSteps = NAV_MAX_STEPS_PER_UPDATE;
    
    eAudioSrc_Id_t eCurrentSrcId = (pstCurrentAudioSrc == NULL)?  eGetFirstActiveSource() : pstCurrentAudioSrc->eSrcId;
    if(eCurrentSrcId == eNUMBER_OF_AUDIO_SOURCES)
        return;
    
    eAudioSrc_Id_t eNextSrcId = eCurrentSrcId;
    sT_AudioSource_t *pstAudioSources = pstAudioSrcScreen->stTDisplayInfo.screenType.staAudioSources;

    for(uint32_t i = 0; i < uiSteps; i++)
    {
        eNextSrcId = eFindNextSource(eNextSrcId, pstAudioSources, stTEncPosition.eDir);
    }

    if(eNextSrcId == eCurrentSrcId)
        return;
        
    vClear_AudioSourceSelectedFlag(&pstAudioSources[eCurrentSrcId]);
    vSet_AudioSourceSelectedFlag(&pstAudioSources[eNextSrcId]);

    vUpdate_UI_ForSourceSelection(eCurrentSrcId, eNextSrcId);
    //Need to update the style for the currently selected src and clear the style for the
    //previously selected src
    printf("Current Source: %d\n\r", eNextSrcId);
}

static void vUpdate_UI_ForSourceSelection(eAudioSrc_Id_t ePrevSelectedSrc, eAudioSrc_Id_t eNewSelectedSrc)
{
    if(pstGetSelectedAudioSource() == NULL)
        return;

    sT_AudioSource_t *pstAudioSources = pstAudioSrcScreen->stTDisplayInfo.screenType.staAudioSources;

    lvgl_lock();

    vClear_PrevSelectedSource_Style(&pstAudioSources[ePrevSelectedSrc]);
    vSet_CurrentSelectedSource_Style(&pstAudioSources[eNewSelectedSrc]);

    lvgl_unlock();
}

/// @brief No LVGL Lock/Unlock carried out in this function. So it has to be done from the caller
/// @param pstAudioSource 
static void vClear_PrevSelectedSource_Style(sT_AudioSource_t *pstAudioSource)
{
    if(pstAudioSource == NULL)
        return;
    
}

/// @brief No LVGL Lock/Unlock carried out in this function. So it has to be done from the caller
/// @param pstAudioSource 
static void vSet_CurrentSelectedSource_Style(sT_AudioSource_t *pstAudioSource)
{
    if(pstAudioSource == NULL)
        return;
    
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
            if(i >= (int32_t)eNUMBER_OF_AUDIO_SOURCES)
                iIndex = eAudio_Src_0;                
        }
        else
        {
            iIndex--;
            if(i < (int32_t)eAudio_Src_0)
                iIndex = eNUMBER_OF_AUDIO_SOURCES - 1;
        }

        if(bIsAudioSourceActive(&pstCurrentAudioSrcs[iIndex]) && 
           bIsAudioSourceVisible(&pstCurrentAudioSrcs[iIndex]))
           return (eAudioSrc_Id_t)iIndex;
    }

    return eCurrentId;
}

static void vProcess_Initial_State( sT_QEncData_t *pstTEncData )
{
    if (bIsENCSWPressed())
    {
        vClear_EncSWPressed();
        vSet_ScreenEvent(eSource_Event_Update);
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

    vSet_ScreenEvent(eSource_Event_Select);
    vNavigate_Sources();
}

static void vClear_EncData(sT_QEncData_t *pstQEncData)
{
    if(pstQEncData == NULL)
        return;
    vClear_EncSWPressed();
}

static inline eAudioSrc_Id_t eGetFirstActiveSource( void )
{
    sT_AudioSource_t *pstAudioSrc = pstAudioSrcScreen->stTDisplayInfo.screenType.staAudioSources;
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
    sT_AudioSource_t *pstAudioSrc = pstAudioSrcScreen->stTDisplayInfo.screenType.staAudioSources;

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

static inline void vSet_ScreenEvent(eScreen_Event_t eEvent)
{
    if(eEvent >= eNUMBER_OF_SCREEN_EVENTs)
    {
        FHALT("Invalid Event : %d", eEvent);
    }

    atomic_store_explicit(&stTScreenEventCtrl.eCurrentEvent, eEvent, memory_order_release);
    vUpdate_TrigTime(k_uptime_get_32());
}

static inline eScreen_Event_t eGet_ScreenEvent( void )
{
    eScreen_Event_t eEvent = atomic_load_explicit(&stTScreenEventCtrl.eCurrentEvent, memory_order_acquire);
    return eEvent;
}

static inline void vUpdate_CurrentEventState( void )
{
    uint32_t uiElapsedTime = k_uptime_get_32() - uiGet_TrigTime();
    if(uiElapsedTime > MAX_TIME_BETWEEN_SCREEN_EVENTs_ms)
    {
        vSet_ScreenEvent(eSource_Event_None);
        vUpdate_TrigTime(k_uptime_get_32());
    }
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

void vSetup_AudioSrc_ScreenStartup( sT_UIScreen_t *pstUIScreen )
{    
    pstAudioSrcScreen = pstUIScreen;
    sT_AudioSource_t *pstAudioSrc = pstAudioSrcScreen->stTDisplayInfo.screenType.staAudioSources;

    for(uint8_t i = eAudio_Src_0; i < eNUMBER_OF_AUDIO_SOURCES; i++)
    {
        if(!bIsAudioSourceActive(&pstAudioSrc[i]) ||
           !bIsAudioSourceVisible(&pstAudioSrc[i]))
            continue;
        vSet_AudioSourceSelectedFlag(&pstAudioSrc[i]);
        return;
    }    
}

#pragma region UI Creation

void vSetup_AudioSourceList( sT_UIScreen_t *pstUIScreen )
{
    pstUIScreen->pfCreate(pstUIScreen);
    vSet_ScreenActive(eScreen_SourceSelect);
}

void vInit_SourceSelectScreen_Controls(sT_UIScreen_t *pstAudioSrcSelect)
{
    pstAudioSrcSelect->pfCreate = vCreate_SourceList;
    pstAudioSrcSelect->stTDisplayInfo.eScreenId = eScreen_SourceSelect;

    for(uint8_t uiSrc = eAudio_Src_0; uiSrc < eNUMBER_OF_AUDIO_SOURCES; uiSrc++)
    {
        sT_AudioSource_t *pstAudSrc = &pstAudioSrcSelect->stTDisplayInfo.screenType.staAudioSources[uiSrc];
        sT_UIControl *pstUIControl = pstAudSrc->staUIControls;

        pstAudSrc->eSrcId = uiSrc;
        
        //Setup Label Physical Positions
        pstUIControl[0].eObjType = eUIObj_Label;
        sT_UIObj_Label *pstLabel = &pstUIControl[0].uiObject.stTObj_Label;
        memcpy(pstLabel, &stTAudioSrcList_FirstLabel, sizeof(sT_UIObj_Label));
        if(uiSrc != 0)
        {
            pstLabel->ly = stTAudioSrcList_FirstLabel.ly;
            pstLabel->lx = stTAudioSrcList_FirstLabel.lx;
            pstLabel->iwidth = stTAudioSrcList_FirstLabel.iwidth;
            pstLabel->iheight = stTAudioSrcList_FirstLabel.iheight;
        }
        
    }


}

static void vCreate_SourceList( sT_UIScreen_t *pstSoucreScreen )
{
    lv_obj_t *pstAudioSrcList;

    lvgl_lock();
    pstSoucreScreen->pstScreenObj = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(pstSoucreScreen->pstScreenObj, lv_color_hex(SCREEN_BACKGROUND_COLOR), LV_PART_MAIN);

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
    lv_obj_align(pstAudioSrcList, LV_ALIGN_LEFT_MID, 0, 0);

    sT_AudioSource_t *pstAudioSrc = pstSoucreScreen->stTDisplayInfo.screenType.staAudioSources;
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
                case eUIObj_Button:
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
    lvgl_unlock();
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

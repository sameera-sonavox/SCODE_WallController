
#include "../Lib/API_Usage_Definition.h"

#ifdef USE_LVGL_DISPLAY

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <lvgl_zephyr.h>

#include "LVGL_Display_Controller.h"
#include "../Lib/GenericMacro.h"
#include "Screen_Parameters/LVGL_AudioSources_UIParam.h"

static void vInit_Screens(void);
static void vInit_SourceSelectScreen_Controls(sT_UIScreen_t *pstAudioSrcSelect);

static void vSetup_AudioSourceList( void );
static void vCreate_SourceList( void );
static lv_obj_t *pstCreate_UILabel(lv_obj_t *pstParent, sT_UIObj_Label *pstTObj_Label, sT_AudioSource_t *pstAudioSrc);

static inline void vSet_ScreenActive(eScreenId_t eId);
static inline void vSet_ScreenInactive(eScreenId_t eId);
static inline eScreenId_t eGetActiveScreen( void );

static inline bool bIsAudioSourceVisible(sT_AudioSource_t *pstAudioSrc);
static inline bool bIsAudioSourceActive(sT_AudioSource_t *pstAudioSrc);
static inline bool bIsAudioSrcSelected(sT_AudioSource_t *pstAudioSrc);
static inline bool bIsAudioSrcMuted(sT_AudioSource_t *pstAudioSrc);

static inline void vSet_AudioSourceVisibilityFlag(sT_AudioSource_t *pstAudioSrc);
static inline void vSet_AudioSourceActiveFlag(sT_AudioSource_t *pstAudioSrc);
static inline void vSet_AudioSourceSelectedFlag(sT_AudioSource_t *pstAudioSrc);
static inline void vSet_AudioSourceMutedFlag(sT_AudioSource_t *pstAudioSrc);

static inline void vClear_AudioSourceVisibilityFlag(sT_AudioSource_t *pstAudioSrc);
static inline void vClear_AudioSourceActiveFlag(sT_AudioSource_t *pstAudioSrc);
static inline void vClear_AudioSourceSelectedFlag(sT_AudioSource_t *pstAudioSrc);
static inline void vClear_AudioSourceMutedFlag(sT_AudioSource_t *pstAudioSrc);

sT_UIScreen_t staUIScreens[eNUMBER_OF_SCREENs] = {
    
    {
        .eScreenId = eScreen_Welcome,
        .bIsActive = false,
    },
    {
        .eScreenId = eScreen_SourceSelect,
        .bIsActive = false,
        .stTDisplayInfo.screenType.staAudioSources = {
            
            {
                .acName = "BT Audio",
                .bIsActive = true,
                .bIsVisible = true,
            },
            {
                .acName = "MIC",
                .bIsActive = true,
                .bIsVisible = true,
            }
        },        
    }
};

void vInit_UI( void )
{
    vInit_Screens();

    vSetup_AudioSourceList();

    vLoad_Screen(eScreen_SourceSelect);
}

void vLoad_Screen( eScreenId_t eID )
{
    if(eID >= eNUMBER_OF_SCREENs)
    {
        FHALT("Invalid Screen Id: %d", eID);
        return;
    }

    lv_screen_load(staUIScreens[eID].pstScreenObj);
    vSet_ScreenActive(eID);
}

static void vInit_Screens(void)
{
    for(uint8_t i = eScreen_Welcome; i < eNUMBER_OF_SCREENs; i++)
    {
        switch(i)
        {
            case eScreen_Welcome:
                break;
            case eScreen_SourceSelect:
                vInit_SourceSelectScreen_Controls(&staUIScreens[i]);
                break;
            default:
                FHALT("Invalid UI Screen : %d", i);
                break;
        }
    }
}

bool bSet_AudioSource_MuteState( eAudioSrc_Id_t eSrcId, bool bIsMute )
{
    if(eSrcId >= eNUMBER_OF_AUDIO_SOURCES)
    {
        FHALT("Invalid Audio Src : %d", eSrcId);
        return false;
    }
    sT_UIScreen_t *pstAudioScreen = &staUIScreens[eScreen_SourceSelect];
    sT_AudioSource_t *pstAudSrc = &pstAudioScreen->stTDisplayInfo.screenType.staAudioSources[eSrcId];
    if(!bIsAudioSourceVisible(pstAudSrc))
    {
        FHALT("Invalid Request while Audion Souce is removed");
        return false;
    }
    if(!bIsAudioSourceActive(pstAudSrc))
    {
        return true;//Should not change color, since the source is already disabled
    }  
    if(pstAudSrc->pstAudSrcObj == NULL)
    {
        vClear_AudioSourceActiveFlag(pstAudSrc);
        FHALT("The Audio Src is NULL and Not Permitted.");
        return false;
    }
    
    if(bIsMute)
        vSet_AudioSourceMutedFlag(pstAudSrc);
    else
        vClear_AudioSourceMutedFlag(pstAudSrc);

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
                pstUICtrl->uiObject.stTObj_Label.lcolor_Text = (bIsMute)? lv_color_hex(MUTE_AUDIO_SRC_TEXT_COLOR):
                                                                          lv_color_hex(ACTIVE_AUDIO_SRC_TEXT_COLOR);
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
    sT_AudioSource_t *pstAudSrc = &pstAudioScreen->stTDisplayInfo.screenType.staAudioSources[eSrcId];
    if(!bIsAudioSourceVisible(pstAudSrc))
    {
        FHALT("Invalid Request while Audion Souce is removed");
        return false;
    }
    
    if(pstAudSrc->pstAudSrcObj == NULL)
    {
        vClear_AudioSourceActiveFlag(pstAudSrc);
        FHALT("The Audio Src is NULL and Not Permitted.");
        return false;
    }

    if(bIsActive)
        vSet_AudioSourceActiveFlag(pstAudSrc);
    else
        vClear_AudioSourceActiveFlag(pstAudSrc);
    
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
                pstUICtrl->uiObject.stTObj_Label.lcolor_Text = (bIsActive)? lv_color_hex(ACTIVE_AUDIO_SRC_TEXT_COLOR):
                                                                            lv_color_hex(INACTIVE_AUDIO_SRC_TEXT_COLOR);
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
    sT_AudioSource_t *pstAudSrc = &pstAudioScreen->stTDisplayInfo.screenType.staAudioSources[eSrcId];
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

static void vInit_SourceSelectScreen_Controls(sT_UIScreen_t *pstAudioSrcSelect)
{
    pstAudioSrcSelect->pfCreate = vCreate_SourceList;
    pstAudioSrcSelect->stTDisplayInfo.eScreenId = eScreen_SourceSelect;

    for(uint8_t uiSrc = eAudio_Src_0; uiSrc < eNUMBER_OF_AUDIO_SOURCES; uiSrc++)
    {
        sT_AudioSource_t *pstAudSrc = &pstAudioSrcSelect->stTDisplayInfo.screenType.staAudioSources[uiSrc];
        sT_UIControl *pstUIControl = pstAudSrc->staUIControls;
        
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

static void vSetup_AudioSourceList( void )
{
    staUIScreens[eScreen_SourceSelect].pfCreate();
    vSet_ScreenActive(eScreen_SourceSelect);
}

static void vCreate_SourceList( void )
{
    sT_UIScreen_t *pstSoucreScreen = &staUIScreens[eScreen_SourceSelect];
    lv_obj_t *pstAudioSrcList;

    pstSoucreScreen->pstScreenObj = lv_obj_create(NULL);

    pstAudioSrcList = lv_list_create(pstSoucreScreen->pstScreenObj);
    lv_obj_set_size(pstAudioSrcList, AUDIO_SOURCE_LIST_DISPLAY_WIDTH, AUDIO_SOURCE_LIST_DISPLAY_HEIGHT);
    lv_obj_align(pstAudioSrcList, LV_ALIGN_CENTER, 0, 0);

    sT_AudioSource_t *pstAudioSrc = pstSoucreScreen->stTDisplayInfo.screenType.staAudioSources;
    for(uint8_t i = 0; i < eNUMBER_OF_AUDIO_SOURCES; i++)
    {
        if(!bIsAudioSourceVisible(&pstAudioSrc[i]))
            continue;
        
        sT_UIControl *pstUIControl = pstAudioSrc[i].staUIControls;
        lv_obj_t *pstSrcContainer = lv_obj_create(pstAudioSrcList);

        lv_obj_set_size(pstSrcContainer, AUDIO_SRC_CONTAINER_WIDTH, AUDIO_SRC_CONTAINER_HEIGHT);
                
        for(uint8_t j = 0; j < NUMBER_OF_UI_CONTROLS_PER_AUDIO_SOURCE; j++)
        {
            switch(pstUIControl[j].eObjType)
            {
                case eUIObj_Label:
                     pstUIControl[j].pstUIObj = pstCreate_UILabel(pstSrcContainer, 
                                                                  &pstUIControl[j].uiObject.stTObj_Label, 
                                                                  &pstAudioSrc[i]);                   
                    break;
                case eUIObj_Button:
                    break;
                default:
                    FHALT("Obj Type: %d, not implemented yet.", pstUIControl[j].eObjType);
                    break;
            }
        }

        pstAudioSrc[i].pstAudSrcObj = pstSrcContainer;
    }
}

static lv_obj_t *pstCreate_UILabel(lv_obj_t *pstParent, sT_UIObj_Label *pstTObj_Label, sT_AudioSource_t *pstAudioSrc)
{
    lv_obj_t *pstLabel = lv_label_create(pstParent);

    lv_obj_set_size(pstLabel, pstTObj_Label->iwidth, pstTObj_Label->iheight);
    lv_label_set_text(pstLabel, (const char*)pstAudioSrc->acName);
    lv_obj_set_style_text_align(pstLabel, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
    lv_obj_set_pos(pstLabel, pstTObj_Label->lx, pstTObj_Label->ly);

    bool bIsActive = bIsAudioSourceActive(pstAudioSrc);
    if(bIsActive)
    {
        pstTObj_Label->lcolor_Text = lv_color_hex(ACTIVE_AUDIO_SRC_TEXT_COLOR);
    }
    else
    {
        pstTObj_Label->lcolor_Text = lv_color_hex(INACTIVE_AUDIO_SRC_TEXT_COLOR);
    }

    lv_obj_set_style_text_color(pstLabel, pstTObj_Label->lcolor_Text, LV_PART_MAIN);
    return pstLabel;
}

static inline void vSet_ScreenActive(eScreenId_t eId)
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

static inline void vSet_ScreenInactive(eScreenId_t eId)
{
    atomic_store_explicit(&staUIScreens[eId].bIsActive, false, memory_order_release);
}

static inline eScreenId_t eGetActiveScreen( void )
{
    for(uint8_t i = 0; i < eNUMBER_OF_SCREENs; i++)
    {
        bool bIsActive = atomic_load_explicit(&staUIScreens[i].bIsActive, memory_order_acquire);
        if(bIsActive)
            return (eScreenId_t)i;
    }
    return eNUMBER_OF_SCREENs;
}

static inline bool bIsAudioSourceVisible(sT_AudioSource_t *pstAudioSrc)
{
    bool bVisible = atomic_load_explicit(&pstAudioSrc->bIsVisible, memory_order_acquire);
    return bVisible;
}

static inline void vSet_AudioSourceVisibilityFlag(sT_AudioSource_t *pstAudioSrc)
{
    atomic_store_explicit(&pstAudioSrc->bIsVisible, true, memory_order_release);
}

static inline void vClear_AudioSourceVisibilityFlag(sT_AudioSource_t *pstAudioSrc)
{
    atomic_store_explicit(&pstAudioSrc->bIsVisible, false, memory_order_release);
}

static inline bool bIsAudioSourceActive(sT_AudioSource_t *pstAudioSrc)
{
    bool bIsActive= atomic_load_explicit(&pstAudioSrc->bIsActive, memory_order_acquire);
    return bIsActive;
}

static inline void vSet_AudioSourceActiveFlag(sT_AudioSource_t *pstAudioSrc)
{
    atomic_store_explicit(&pstAudioSrc->bIsActive, true, memory_order_release);
}

static inline void vClear_AudioSourceActiveFlag(sT_AudioSource_t *pstAudioSrc)
{
    atomic_store_explicit(&pstAudioSrc->bIsActive, false, memory_order_release);
}

static inline bool bIsAudioSrcSelected(sT_AudioSource_t *pstAudioSrc)
{
    bool bIsSelected = atomic_load_explicit(&pstAudioSrc->bIsSelected, memory_order_acquire);
    return bIsSelected;
}

static inline void vSet_AudioSourceSelectedFlag(sT_AudioSource_t *pstAudioSrc)
{
    atomic_store_explicit(&pstAudioSrc->bIsSelected, true, memory_order_release);
}

static inline void vClear_AudioSourceSelectedFlag(sT_AudioSource_t *pstAudioSrc)
{
    atomic_store_explicit(&pstAudioSrc->bIsSelected, false, memory_order_release);
}

static inline bool bIsAudioSrcMuted(sT_AudioSource_t *pstAudioSrc)
{
    bool bIsMute = atomic_load_explicit(&pstAudioSrc->bIsMute, memory_order_acquire);
    return bIsMute;
}

static inline void vSet_AudioSourceMutedFlag(sT_AudioSource_t *pstAudioSrc)
{
    atomic_store_explicit(&pstAudioSrc->bIsMute, true, memory_order_release);
}

static inline void vClear_AudioSourceMutedFlag(sT_AudioSource_t *pstAudioSrc)
{
    atomic_store_explicit(&pstAudioSrc->bIsMute, false, memory_order_release);
}
#endif
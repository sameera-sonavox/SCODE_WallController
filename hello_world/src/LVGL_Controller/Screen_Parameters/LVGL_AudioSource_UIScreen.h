#ifndef LVGL_AUDIOSOURCE_UISCREEN_H
#define LVGL_AUDIOSOURCE_UISCREEN_H

#include "../LVGL_Display_Types.h"
#include "../Lib/QDC/NXP_eQDC_Types.h"
#include "../LVGL_ProjDef.h"

extern void vSetup_AudioSourceList( sT_UIScreen_t *pstUIScreen );
extern void vInit_SourceSelectScreen_Controls(sT_UIScreen_t *pstAudioSrcSelect);
extern void vRun_AudioSourceScreen( sT_UIScreen_t *pstUIScreen, sT_QEncData_t *pstQEncData );
extern void vSetup_AudioSrc_ScreenStartup( sT_UIScreen_t *pstUIScreen );
extern struct k_msgq *pstGetMessageQueue( void );
extern void vUpdate_SrcVolume(sT_AudioSource_t *pstAudioSource, uint8_t uiVol);
extern void vCheck_UIMode_Timeout( sT_QEncData_t *pstQEncData );

static inline bool TryClaim_SourceModifyOwnership( sT_AudioSource_t *pstAudioSrc )
{
    bool bOwnershipSuccess = atomic_exchange_explicit(&pstAudioSrc->bIsExlusivelyOwned, true, memory_order_acq_rel);
    return !bOwnershipSuccess;
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
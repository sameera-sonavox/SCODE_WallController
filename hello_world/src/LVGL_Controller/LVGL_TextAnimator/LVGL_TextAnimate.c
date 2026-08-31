#include "LVGL_TextAnimate.h"
#include "GenericMacro.h"

#include <lvgl.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>

#define MAX_ANIMATION_VARIANTS       4U
#define MAX_ANIMATED_TEXT_LENGTH     16U

static char caAnimatorStrings[MAX_ANIMATION_VARIANTS][MAX_ANIMATED_TEXT_LENGTH];

static lv_timer_t *pstLoadingTextTimer = NULL;
static uint8_t uiLoadingTextIndex = 0U;
static uint8_t uiMaxAnimationCount = 0U;
static TextAnimator_Callback_t TextAnimatorCallback_Fn = NULL;
static _Atomic bool bIsAnimatorRunning = false;

static void vAnimator_TimerCallback(lv_timer_t *plv_Timer);
static inline void vSet_AnimatorStartFlag( void );
static inline void vClear_AnimatorStartFlag( void );
static inline bool bIsAnimatorActive( void );

void vStart_TextAnimator(const char *const pcaStringAnimate[],
                        const char *pcaBaseString,
                        uint8_t uiCount,
                        TextAnimator_Callback_t animatorCallback,
                        uint32_t uiPeriod_ms)
{
    if((pcaStringAnimate == NULL) ||
       (pcaBaseString == NULL) ||
       (uiCount == 0U) ||
       (uiCount > MAX_ANIMATION_VARIANTS) ||
       (animatorCallback == NULL) ||
       (uiPeriod_ms == 0U))
    {
        FHALT("Invalid Arguments");
        return;
    }

    memset(caAnimatorStrings, 0, sizeof(caAnimatorStrings));

    for(uint8_t i = 0U; i < uiCount; i++)
    {
        if(pcaStringAnimate[i] == NULL)
        {
            FHALT("Invalid animation string at index: %u", i);
            return;
        }

        int iLength = snprintf(caAnimatorStrings[i],
                               sizeof(caAnimatorStrings[i]),
                               "%s%s",
                               pcaBaseString,
                               pcaStringAnimate[i]);
        if((iLength < 0) ||
           ((size_t)iLength >= sizeof(caAnimatorStrings[i])))
        {
            FHALT("Animated text is too long at index: %u", i);
            return;
        }
    }

    if(pstLoadingTextTimer != NULL)
    {
        lv_timer_delete(pstLoadingTextTimer);
        pstLoadingTextTimer = NULL;
        vClear_AnimatorStartFlag();
    }

    uiLoadingTextIndex = 0U;
    uiMaxAnimationCount = uiCount;
    TextAnimatorCallback_Fn = animatorCallback;

    pstLoadingTextTimer = lv_timer_create(vAnimator_TimerCallback, uiPeriod_ms, NULL);
    if(pstLoadingTextTimer == NULL)
    {
        TextAnimatorCallback_Fn = NULL;
        uiMaxAnimationCount = 0U;
        FHALT("Failed to create text animator timer");
    }
    vSet_AnimatorStartFlag();
}

static void vAnimator_TimerCallback(lv_timer_t *plv_Timer)
{
    (void)plv_Timer;

    if((TextAnimatorCallback_Fn == NULL) ||
       (uiMaxAnimationCount == 0U))
    {
        return;
    }

    TextAnimatorCallback_Fn(caAnimatorStrings[uiLoadingTextIndex]);

    uiLoadingTextIndex++;

    if(uiLoadingTextIndex >= uiMaxAnimationCount)
    {
        uiLoadingTextIndex = 0U;
    }
}

void vStop_TextAnimator( void )
{
    if(!bIsAnimatorActive())
    {
        FHALT("Text Animator is not running to be stopped");
        return;
    }

    //lv_timer_delete()
}

static inline void vSet_AnimatorStartFlag( void )
{
    atomic_store_explicit(&bIsAnimatorRunning, true, memory_order_release);
}

static inline void vClear_AnimatorStartFlag( void )
{
    atomic_store_explicit(&bIsAnimatorRunning, false, memory_order_release);
}

static inline bool bIsAnimatorActive( void )
{
    bool bRes = atomic_load_explicit(&bIsAnimatorRunning, memory_order_acquire);
    return bRes;
}


#include "UI_ScreenManager.h"
#include "../LVGL_ProjDef.h"
#include "../UI_InputController/UI_InputController.h"
#include "GenericMacro.h"
#include <stdatomic.h>

#include "../Screens/Screen_Welcome/WelcomeScreen.h"

#include <zephyr/kernel.h>

typedef struct
{
    sT_UIScreenDescriptor_t *pstActiveDescriptor;
    bool bIsInitialized;
    _Atomic bool bIsTransitionPending;
    eScreenId_t ePendingScreenId;
} sT_UIScreenManager_t;

sT_UIScreenManager_t stTUIScreenManager;

#define UI_SCREEN_SLOT_COUNT \
    ((size_t)eNUMBER_OF_SCREENs - (size_t)eScreen_Welcome)
#define UI_SCREEN_SLOT_INDEX(eScreenId) \
    ((size_t)(eScreenId) - (size_t)eScreen_Welcome)

static sT_UIScreenRuntime_t staScreenRuntimeInfo[UI_SCREEN_SLOT_COUNT];
static sT_UIScreenDescriptor_t staScreenDescriptors[UI_SCREEN_SLOT_COUNT];

static bool bInit_ScreenData( void );
static inline bool bIsScreenTransitionPending( void );
static bool bActivate_RequestedScreen( void );

bool bInit_UIScreenManager( void )
{
    stTUIScreenManager.pstActiveDescriptor = NULL;

    if(!bInit_ScreenData())
    {
        stTUIScreenManager.bIsInitialized = false;
        return false;
    }

    atomic_store_explicit(&stTUIScreenManager.bIsTransitionPending, false, memory_order_relaxed);
    stTUIScreenManager.ePendingScreenId = eScreen_None;
    stTUIScreenManager.bIsInitialized = true;
    return true;
}

void vRun_UIScreenManager( void )
{
    sT_UIInput_t stTUIInput = {0};

    if(bIsScreenTransitionPending())
    {
        if(!bActivate_RequestedScreen())
        {
            FHALT("New Screen Activattion Failed");            
        }
    }

    while(iGet_NextUIEvent(&stTUIInput, K_NO_WAIT) == 0)
    {
        if(!stTUIScreenManager.bIsInitialized ||
           stTUIScreenManager.pstActiveDescriptor == NULL)
        {
            continue;
        }
        if(stTUIScreenManager.pstActiveDescriptor->pfHandleInput != NULL)
        {
            stTUIScreenManager.pstActiveDescriptor->pfHandleInput(stTUIScreenManager.pstActiveDescriptor->pstScreenRuntime,
                                                                  &stTUIInput);
        }
    }

    if(stTUIScreenManager.pstActiveDescriptor != NULL && 
       stTUIScreenManager.pstActiveDescriptor->pfRun != NULL)
    {
        sT_UIScreenRuntime_t *pstScreenRuntime = stTUIScreenManager.pstActiveDescriptor->pstScreenRuntime;
        stTUIScreenManager.pstActiveDescriptor->pfRun(pstScreenRuntime);
    }
}

static bool bActivate_RequestedScreen( void )
{
    eScreenId_t eId = stTUIScreenManager.ePendingScreenId;
    size_t uiScreenIndex = UI_SCREEN_SLOT_INDEX(eId);

    sT_UIScreenDescriptor_t *pstActiveDescriptor = &staScreenDescriptors[uiScreenIndex];
    sT_UIScreenRuntime_t *pstScrRuntime = pstActiveDescriptor->pstScreenRuntime;

    if(pstScrRuntime == NULL)
    {
        FHALT("Screen runtime is not registered for Screen Id: %d", eId);
        return false;
    }

    if(pstScrRuntime->pstRootObj == NULL)
    {
        if(pstActiveDescriptor->pfCreate == NULL || !pstActiveDescriptor->pfCreate(pstScrRuntime))
        {
            FHALT("Screen creation failed for Screen Id: %d", eId);
            return false;
        }
    }

    if(pstScrRuntime->pstRootObj == NULL)
    {
        FHALT("Screen Root is Null");
        return false;        
    }

    if(stTUIScreenManager.pstActiveDescriptor != NULL)
    {
        sT_UIScreenRuntime_t *pstRuntimeScreen = stTUIScreenManager.pstActiveDescriptor->pstScreenRuntime;
        stTUIScreenManager.pstActiveDescriptor->pfExit(pstRuntimeScreen);
    }

    pstActiveDescriptor->pfEnter(pstScrRuntime);
    stTUIScreenManager.pstActiveDescriptor = pstActiveDescriptor;

    stTUIScreenManager.ePendingScreenId = eScreen_None;
    atomic_store_explicit(&stTUIScreenManager.bIsTransitionPending, false, memory_order_release);
    return true;
}

bool bRequest_UIScreen(eScreenId_t eID)
{   
    if(!stTUIScreenManager.bIsInitialized)
    {
        FHALT("Invalid Request for Screen Id = %d, when screen manager is not initialized", eID);
        return false;
    }
    if(eID <= eScreen_None || eID >= eNUMBER_OF_SCREENs)
    {
        FHALT("Invalid Screen Id = %d", eID);
        return false;
    }

    size_t uiScreenIndex = UI_SCREEN_SLOT_INDEX(eID);
    const sT_UIScreenDescriptor_t *pstDescriptor = &staScreenDescriptors[uiScreenIndex];
    if(pstDescriptor->eScreenId != eID ||
       pstDescriptor->pstScreenRuntime == NULL)
    {
        FHALT("Screen Id = %d is not registered", eID);
        return false;
    }

    bool bispending = atomic_exchange_explicit(&stTUIScreenManager.bIsTransitionPending, true, memory_order_acq_rel);
    if(bispending)
    {
        return false;
    }

    stTUIScreenManager.ePendingScreenId = eID;
    return true;
}

static inline bool bIsScreenTransitionPending( void )
{
    bool res= atomic_load_explicit(&stTUIScreenManager.bIsTransitionPending, memory_order_acquire);
    return res;
}

static bool bInit_ScreenData( void )
{
    for(eScreenId_t eScreenId = eScreen_Welcome;
        eScreenId < eNUMBER_OF_SCREENs;
        eScreenId++)
    {
        const size_t uiScreenIndex = UI_SCREEN_SLOT_INDEX(eScreenId);
        bool bResult = false;

        switch(eScreenId)
        {
            case eScreen_Welcome:
                bResult = bInit_WelcomeScreen(
                    &staScreenRuntimeInfo[uiScreenIndex],
                    &staScreenDescriptors[uiScreenIndex]);
                break;

            case eScreen_SourceSelect:
                /* Temporary: keep the screen slot unregistered until the
                 * descriptor-based Source Select screen is implemented.
                 * bRequest_UIScreen() rejects unregistered screen IDs.
                 */
                continue;

            case eScreen_None:
            case eNUMBER_OF_SCREENs:
            default:
                FHALT("Invalid screen ID during initialization: %d",
                      eScreenId);
                break;
        }

        if(!bResult)
        {
            return false;
        }
    }

    return true;
}


#include "Wallcontroller_StateMachine.h"
#include "GenericMacro.h"
#include "../../LVGL_Controller/LVGL_Display_Controller.h"
#include "../../LVGL_Controller/UI_ScreenManager/UI_ScreenManager.h"

#ifdef DEBUG_WALLCONTROLLER_STATEMACHINE
    #define print_WallCtrl                      printf
#else
    #define print_WallCtrl(...)
#endif

sT_WallCtrl_StateMachine_t stTWallCtrlStateMachine;

static inline void vWallCtrl_Set_State(eWallCtrlState_t eNewState);
static inline eWallCtrlState_t eGet_WallCtrl_State(void);
static inline bool IsStateFirstEntry( void );
static eWallCtrlCmd_t eGet_WallCtrl_Command(void);

static void vHandle_IdleState( void );
static void vHandle_StartupState( void );
static void vHandle_BootloaderState( void );
static void vHandle_ActiveState( void );

void vRun_WallCtrl_StateMachine( void )
{
    eWallCtrlState_t eState = eGet_WallCtrl_State();

    switch (eState)
    {
        case eWallCtrlState_Idle:
            vHandle_IdleState();
            break;
        case eWallCtrlState_Startup:
            vHandle_StartupState();
            break;
        case eWallCtrlState_Bootloader:
            vHandle_BootloaderState();
            break;
        case eWallCtrlState_Active:
            vHandle_ActiveState();
            break;
        default:
            FHALT("Invalid Wall Controller State");
            break;
    }
}

static void vHandle_StartupState( void )
{
    static uint32_t uiTrigttime_ms = 0U;

    if(IsStateFirstEntry())
    {
        print_WallCtrl("Wall Controller State: Startup\n");
        uiTrigttime_ms = k_uptime_get_32();
    }

    eWallCtrlCmd_t eCmd = eGet_WallCtrl_Command();
    switch(eCmd)
    {
        case eWallCtrlCmd_None:
        case eWallCtrlCmd_Startup:
            if((k_uptime_get_32() - uiTrigttime_ms) >= WALLCTRL_STARTUP_STATE_TIMEOUT_ms)
            {
                vWallCtrl_Set_State(eWallCtrlState_Active);
            }
            break;
        case eWallCtrlCmd_EnterBootloader:
            // Handle EnterBootloader command
            break;
        case eWallCtrlState_Active:
            break;
        default:
            FHALT("Invalid Wall Controller Command");
            break;
    }
}

static void vHandle_BootloaderState( void )
{
    if(IsStateFirstEntry())
    {
        print_WallCtrl("Wall Controller State: Bootloader\n");
    }

    eWallCtrlCmd_t eCmd = eGet_WallCtrl_Command();
    switch(eCmd)
    {
        case eWallCtrlCmd_None:
        case eWallCtrlCmd_EnterBootloader:
            // Handle None command
            break;
        case eWallCtrlCmd_Startup:
            // Handle Startup command
            break;
        case eWallCtrlCmd_Active:
            break;
        default:
            FHALT("Invalid Wall Controller Command");
            break;
    }
}

static void vHandle_ActiveState( void )
{
    if(IsStateFirstEntry())
    {
        print_WallCtrl("Wall Controller State: Active\n");
        if(!bRequest_UIScreen(eScreen_AudioSrcInfo))
        {
            FHALT("Screen Load Request for Screen[%d] failed", eScreen_AudioSrcInfo);
            stTWallCtrlStateMachine.bIsFirstEntry = true;
            return;
        }
    }

    eWallCtrlCmd_t eCmd = eGet_WallCtrl_Command();
    switch(eCmd)
    {
        case eWallCtrlCmd_None:
            // Handle None command
            break;
        case eWallCtrlCmd_EnterBootloader:
            break;
        case eWallCtrlCmd_Startup:
            // Handle Startup command
            break;
        default:
            FHALT("Invalid Wall Controller Command");
            break;
    }
}

static void vHandle_IdleState( void )
{
    if(IsStateFirstEntry())
    {
        print_WallCtrl("Wall Controller State: Idle\n");
    }

    eWallCtrlCmd_t eCmd = eGet_WallCtrl_Command();
    switch(eCmd)
    {
        case eWallCtrlCmd_None:
            // Handle None command
            break;
        case eWallCtrlCmd_EnterBootloader:
            // Handle EnterBootloader command
            break;
        case eWallCtrlCmd_Startup:
            vWallCtrl_Set_State(eWallCtrlState_Startup);
            break;
        default:
            FHALT("Invalid Wall Controller Command");
            break;
    }
}

static inline bool IsStateFirstEntry( void )
{
    if(!stTWallCtrlStateMachine.bIsFirstEntry)
    {
        return false;
    }
    
    stTWallCtrlStateMachine.bIsFirstEntry = false;
    return true;
}

void vInit_WallCtrl_StateMachine( void )
{
    vWallCtrl_Set_State(eWallCtrlState_Idle);

    for(uint8_t i = 0; i < eNUMBER_OF_WALLCONTROLLER_CMDS; i++)
    {        
        atomic_store_explicit(&stTWallCtrlStateMachine.baWallCtrlCmds[i], false, memory_order_release);
    }
}

void vWallCtrl_Set_Command(eWallCtrlCmd_t eCmd)
{
    if(eCmd >= eNUMBER_OF_WALLCONTROLLER_CMDS)
    {
        FHALT("Invalid Wall Controller Command");
        return;
    }

    k_spinlock_key_t key = k_spin_lock(&stTWallCtrlStateMachine.spinlock_WallCtrlCmds);
    atomic_store_explicit(&stTWallCtrlStateMachine.baWallCtrlCmds[eCmd], true, memory_order_release);
    k_spin_unlock(&stTWallCtrlStateMachine.spinlock_WallCtrlCmds, key);
}

static eWallCtrlCmd_t eGet_WallCtrl_Command(void)
{
    k_spinlock_key_t key = k_spin_lock(&stTWallCtrlStateMachine.spinlock_WallCtrlCmds);

    for(eWallCtrlCmd_t eCmd = eWallCtrlCmd_None; eCmd < eNUMBER_OF_WALLCONTROLLER_CMDS; eCmd++)
    {
        bool bIsCmdSet = atomic_exchange_explicit(&stTWallCtrlStateMachine.baWallCtrlCmds[eCmd], false, memory_order_acq_rel);
        if(bIsCmdSet)
        {
            k_spin_unlock(&stTWallCtrlStateMachine.spinlock_WallCtrlCmds, key);
            return eCmd;
        }
    }

    k_spin_unlock(&stTWallCtrlStateMachine.spinlock_WallCtrlCmds, key);
    return eWallCtrlCmd_None;
}

static inline void vWallCtrl_Set_State(eWallCtrlState_t eNewState)
{
    if(eNewState >= eNUMBER_OF_WALLCONTROLLER_STATEs)
    {
        FHALT("Invalid Wall Controller State");
        return;
    }

    stTWallCtrlStateMachine.eCurrentState = eNewState;
    stTWallCtrlStateMachine.bIsFirstEntry = true;
}

static inline eWallCtrlState_t eGet_WallCtrl_State(void)
{
    return stTWallCtrlStateMachine.eCurrentState;
}

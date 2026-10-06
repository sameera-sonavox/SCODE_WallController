#ifndef WALLCONTROLLER_STATEMACHINE_H
#define WALLCONTROLLER_STATEMACHINE_H

#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <zephyr/kernel.h>

#define DEBUG_WALLCONTROLLER_STATEMACHINE

#define WALLCTRL_STARTUP_STATE_TIMEOUT_ms           3000U

typedef enum
{
    eWallCtrlState_Idle = 0,
    eWallCtrlState_Startup,
    eWallCtrlState_Bootloader,
    eWallCtrlState_Active,
    eNUMBER_OF_WALLCONTROLLER_STATEs,
} eWallCtrlState_t;

typedef enum
{
    eWallCtrlCmd_None = 0,
    eWallCtrlCmd_EnterBootloader,
    eWallCtrlCmd_Startup,
    eWallCtrlCmd_Active,
    eNUMBER_OF_WALLCONTROLLER_CMDS,
} eWallCtrlCmd_t;

typedef struct
{
    eWallCtrlState_t eCurrentState;
    bool bIsFirstEntry;

    _Atomic bool baWallCtrlCmds[eNUMBER_OF_WALLCONTROLLER_CMDS];
    struct k_spinlock spinlock_WallCtrlCmds;

} sT_WallCtrl_StateMachine_t;

extern void vRun_WallCtrl_StateMachine( void );
extern void vInit_WallCtrl_StateMachine( void );
extern void vWallCtrl_Set_Command(eWallCtrlCmd_t eCmd);

#endif // WALLCONTROLLER_STATEMACHINE_H
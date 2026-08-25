#include "ExtFlash_Controller.h"

#include "../Lib/GenericMacro.h"
#include "SPIController/SPIController.h"
#include "ExtFlashInitialSetup/ExtFlash_InitialSetupCtrl.h"
#include "Flash_Memory_ValidateTest/Flash_Memory_ValidateTest.h"
#include "ExtFlash_ControllerTypes.h"

sT_ExtFlash_Control_t stTExtFlashCtrl = {0};

static inline void vSet_SetupSuccess_Flag( void );
static inline void vClear_SetupSuccess_Flag( void );

bool bInit_ExtFlash( void )
{
    bool bResult = bInit_LPSPI_ForExtFlash();
    if(!bResult)
    {
        vClear_SetupSuccess_Flag();
        FHALT("Failed to initialize LPSPI1 for External Flash");
        return false;
    }

    if(!bExec_Flash_InitialSetup())
    {
        vClear_SetupSuccess_Flag();
        return false;
    }

    vSet_SetupSuccess_Flag();

    if(!bRun_FlashMemory_ValidationTests())
    {
        FHALT("Flash Test Failed");
        return false;
    }

    return true;
}

bool bIsExtFlash_SetupSUccess( void )
{
    return stTExtFlashCtrl.bIsExtFlashSetupSuccess;
}

static inline void vSet_SetupSuccess_Flag( void )
{
    stTExtFlashCtrl.bIsExtFlashSetupSuccess = true;
}

static inline void vClear_SetupSuccess_Flag( void )
{
    stTExtFlashCtrl.bIsExtFlashSetupSuccess = false;
}

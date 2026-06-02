#include "NXP_ADC_API.h"
#include "NXP_ADC_Types.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include "fsl_lpadc.h"
#include "fsl_common.h"
#include <zephyr/drivers/adc.h>
#include "../GenericMacro.h"
#include "../CPULoad/NXP_CPU_LoadMon.h"

typedef struct
{
    bool bIsADCInitialized;
    lpadc_config_t stADCConfig;

}stADC_HWmodConfig_t;

stADC_HWmodConfig_t staADC_HWConfig[eNUMBER_OF_ADC_MODULEs] = {0};

#define pstGetADCModule(eADCModule)                 (&staADC_HWConfig[eADCModule])

static void vValidate_ADCConfig(sT_ADC_ModuleConfig_t *pstADCModuleConfig);
static bool bADC_Init(sT_ADC_ModuleConfig_t *pstADCModuleConfig);

void vInit_ADC(sT_ADC_ModuleConfig_t *pstADCModuleConfig)
{
    if(pstADCModuleConfig == NULL)
    {
        FHALT("ADC Module Config pointer is NULL. Cannot initialize ADC module.\n");
        return;
    }

    pstADCModuleConfig->bIsConfigOk = true; // Set this flag based on actual configuration validation
    vValidate_ADCConfig(pstADCModuleConfig);
    if(!pstADCModuleConfig->bIsConfigOk)
        return;
    
    if(!bADC_Init(pstADCModuleConfig))
    {
        FHALT("ADC Module initialization failed.\n");
        pstADCModuleConfig->bIsConfigOk = false;
        return;
    }
    // ADC initialization code here
    // This function will configure the ADC module based on the provided configuration structure
}

static bool bADC_Init(sT_ADC_ModuleConfig_t *pstADCModuleConfig)
{
    if(pstADCModuleConfig == NULL)
    {
        FHALT("ADC Module Config pointer is NULL. Cannot initialize ADC module.\n");
        return false;
    }

    stADC_HWmodConfig_t *pstHWConfig = pstGetADCModule(pstADCModuleConfig->eADCModule);
    LPDAC_GetDefaultConfig(&pstHWConfig->stADCConfig);
    //pstHWConfig->stADCConfig.
    return true;
    // Customize pstHWConfig->stADCConfig based on pstADCModuleConfig settings

}

static void vValidate_ADCConfig(sT_ADC_ModuleConfig_t *pstADCModuleConfig)
{
    if(pstADCModuleConfig == NULL)
    {
        FHALT("ADC Module Config pointer is NULL. Cannot validate ADC module configuration.\n");
        return;
    }

    if(pstADCModuleConfig->eADCModule >= eNUMBER_OF_ADC_MODULEs || pstADCModuleConfig->eADCModule < eADC_ADC0)
    {
        FHALT("Invalid ADC module specified in configuration.\n");
        pstADCModuleConfig->bIsConfigOk = false;
        return;
    }
}

void vDeInit_ADC(eADC_Module_t eADCModule)
{
    // ADC de-initialization code here
    // This function will de-initialize the specified ADC module
}
#include <zephyr/kernel.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/drivers/gpio.h>
#include <stdio.h>

#include "Amp_GPIO.h"
#include "../Settings/GeneralSettings.h"

#if defined DEBUG_GPIO_CONFIG
    #define GPIO_DEBUG_PRINT(...) printf(__VA_ARGS__)
#else
    #define GPIO_DEBUG_PRINT(...)
#endif

#define ClassD_SD_GPIO_DT DT_ALIAS(debug_pin_0)
static const struct gpio_dt_spec stClassD_SD_GPIO = GPIO_DT_SPEC_GET(ClassD_SD_GPIO_DT, gpios);


void vInit_Amp_GPIO(void)
{
    int ret;
    uint8_t uiAttempts = 0;

    for(uiAttempts = 0; uiAttempts < AMP_IO_READY_MAX_ATTEMPTS;) 
    {
 
        if (!gpio_is_ready_dt(&stClassD_SD_GPIO)) {
            GPIO_DEBUG_PRINT("Amp GPIO device is not ready\n");
            k_msleep(AMP_IO_READY_TIMEOUT_MS);
            uiAttempts++;
            continue;
        }
        uiAttempts = 0;
        
        ret = gpio_pin_configure_dt(&stClassD_SD_GPIO, GPIO_OUTPUT_INACTIVE);
        if (ret < 0) {
            GPIO_DEBUG_PRINT("Failed to configure Amp GPIO: %d\n", ret);
            k_msleep(AMP_IO_READY_TIMEOUT_MS);
            uiAttempts++;
            continue;
        }   

        break;
    }

    if(uiAttempts >= AMP_IO_READY_MAX_ATTEMPTS) {
        GPIO_DEBUG_PRINT("Failed to configure Amp GPIO\n");
        FHALT("Amp GPIOs couldn't be set after maximum attempts. Device reboots...");
        k_msleep(AMP_IO_READY_TIMEOUT_MS);
        sys_reboot(SYS_REBOOT_COLD);
        return;
    }

    GPIO_DEBUG_PRINT("Amp GPIO configured\n");
}

void vSet_GPIO_OutputState(eAmp_GPIO eGPIO,eGPIO_OutputState eOutputState)
{
    int ret;

    switch (eGPIO)
    {
    case eAMP_SD:
        if(eOutputState == eOUTPUT_High) {
            ret = gpio_pin_set_dt(&stClassD_SD_GPIO, 1);
        } else if(eOutputState == eOUTPUT_Low) {
            ret = gpio_pin_set_dt(&stClassD_SD_GPIO, 0);
        } else {
            FHALT("Invalid Output State");
            break;
        }

        if (ret < 0) {
            GPIO_DEBUG_PRINT("Failed to set Amp GPIO: %d\n", ret);
        }
        break;
    
    default:
        FHALT("Invalid GPIO");
        break;
    }
}

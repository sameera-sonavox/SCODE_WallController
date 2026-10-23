#include <errno.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>

#define EXT_FLASH_NODE          DT_NODELABEL(extflash0)
#define SECONDARY_SLOT_NODE     DT_NODELABEL(slot1_partition)
#define EXT_FLASH_PROBE_LENGTH  16U

BUILD_ASSERT(DT_NODE_HAS_STATUS(EXT_FLASH_NODE, okay),
             "MCUboot external flash node must be enabled");
BUILD_ASSERT(DT_NODE_HAS_STATUS(SECONDARY_SLOT_NODE, okay),
             "MCUboot external secondary slot must be enabled");

static int iProbe_MCUbootExternalFlash( void )
{
    const struct device *pstFlashDevice = DEVICE_DT_GET(EXT_FLASH_NODE);
    uint8_t uiaProbeData[EXT_FLASH_PROBE_LENGTH];
    uint64_t uiFlashSize = 0U;
    int iResult;

    if(!device_is_ready(pstFlashDevice))
    {
        printk("MCUboot: External flash is not ready\n");
        return 0;
    }

    iResult = flash_get_size(pstFlashDevice, &uiFlashSize);
    if(iResult != 0)
    {
        printk("MCUboot: External flash size query failed: %d\n", iResult);
        return 0;
    }

    iResult = flash_read(pstFlashDevice,
                         DT_REG_ADDR(SECONDARY_SLOT_NODE),
                         uiaProbeData,
                         sizeof(uiaProbeData));
    if(iResult != 0)
    {
        printk("MCUboot: External flash read failed: %d\n", iResult);
        return 0;
    }

    printk("MCUboot: External flash ready (%llu bytes), read OK\n",
           (unsigned long long)uiFlashSize);
    return 0;
}

SYS_INIT(iProbe_MCUbootExternalFlash,
         APPLICATION,
         CONFIG_APPLICATION_INIT_PRIORITY);

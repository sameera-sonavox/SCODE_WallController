#include "ZephyrFlashMiddleWare.h"

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>

#include "../ExtFlash_Controller.h"
#include "../ExtFlashReadWrite/ExtFlash_ReadWriteControl.h"

#define EXT_FLASH_NODE  DT_NODELABEL(extflash0)

typedef struct
{
    size_t uiDeviceSize;
    size_t uiEraseBlockSize;
    size_t uiWriteBlockSize;
    uint8_t uiEraseValue;

} sT_ZephyrFlashConfig_t;

typedef struct
{
    struct k_mutex stFlashMutex;

} sT_ZephyrFlashData_t;

#pragma region Function Declarations

static int iZephyrFlash_Init(const struct device *pstDevice);
static int iZephyrFlash_Read(const struct device *pstDevice,
                             off_t tOffset,
                             void *pData,
                             size_t uiLength);
static int iZephyrFlash_Write(const struct device *pstDevice,
                              off_t tOffset,
                              const void *pData,
                              size_t uiLength);
static int iZephyrFlash_Erase(const struct device *pstDevice,
                             off_t tOffset,
                             size_t uiLength);

static const struct flash_parameters *pstGet_ZephyrFlashParameters(const struct device *pstDevice);
static int iGet_ZephyrFlashSize(const struct device *pstDevice, uint64_t *puiSize);

#if defined(CONFIG_FLASH_PAGE_LAYOUT)
static void vGet_ZephyrFlashPageLayout(const struct device *pstDevice,                                        
                                       const struct flash_pages_layout **ppstLayout,
                                       size_t *puiLayoutCount);
#endif

static int iValidate_FlashRange(const struct device *pstDevice, off_t tOffset, size_t uiLength);

#pragma endregion

static const struct flash_parameters stTFlashParameters =
{
    .write_block_size = EXT_FLASH_WRITE_BLOCK_SIZE_BYTEs,
    .erase_value = EXT_FLASH_ERASE_VALUE
};

#if defined(CONFIG_FLASH_PAGE_LAYOUT)
static const struct flash_pages_layout stTFlashPageLayout =
{
    .pages_count = EXT_FLASH_PAGE_COUNT,
    .pages_size = EXT_FLASH_ERASE_BLOCK_SIZE_BYTEs
};
#endif

static const struct flash_driver_api stTZephyrFlashAPI = {
    .get_parameters = pstGet_ZephyrFlashParameters,
    .get_size = iGet_ZephyrFlashSize,
    .erase = iZephyrFlash_Erase,
    .read = iZephyrFlash_Read,
    .write = iZephyrFlash_Write,

    #if defined(CONFIG_FLASH_PAGE_LAYOUT)
    .page_layout = vGet_ZephyrFlashPageLayout,
    #endif
};

static const sT_ZephyrFlashConfig_t stTZephyrFlashConfig = {
    .uiDeviceSize = DT_PROP(EXT_FLASH_NODE, size_in_bytes),
    .uiEraseBlockSize = DT_PROP(EXT_FLASH_NODE, erase_block_size),
    .uiWriteBlockSize = DT_PROP(EXT_FLASH_NODE, write_block_size),
    .uiEraseValue = EXT_FLASH_ERASE_VALUE
};

static sT_ZephyrFlashData_t stTZephyrFlashData;

static int iZephyrFlash_Init(const struct device *pstDevice)
{   
    if(pstDevice == NULL)
    {
        return -EINVAL;
    }
    if(pstDevice->data == NULL)
    {
        return -EINVAL;
    }
    
    sT_ZephyrFlashData_t *pstDeviceData = (sT_ZephyrFlashData_t *)pstDevice->data;

    k_mutex_init(&pstDeviceData->stFlashMutex);
    k_msleep(300U);
    
    if(!bInit_ExtFlash())
    {
        return -EIO;
    }
    return 0;
}

static int iZephyrFlash_Read(const struct device *pstDevice,
                             off_t tOffset,
                             void *pData,
                             size_t uiLength)
{
    int iResult = iValidate_FlashRange(pstDevice, tOffset, uiLength);
    if(iResult < 0)
    {
        return iResult;
    }
    if(uiLength == 0U)
    {
        return 0;
    }
    if(pData == NULL)
    {
        return -EINVAL;
    }
    if(pstDevice->data == NULL)
    {
        return -ENODEV;
    }
    
    sT_ZephyrFlashData_t *pstData = (sT_ZephyrFlashData_t *)pstDevice->data;

    iResult = k_mutex_lock(&pstData->stFlashMutex, K_FOREVER);
    if(iResult < 0)
        return iResult;
    
    iResult = iRead_DataFromFlash_Quad((uint32_t)tOffset, pData, uiLength);

    int iMuteUnlockResult = k_mutex_unlock(&pstData->stFlashMutex);
    if(iResult == 0 && iMuteUnlockResult < 0)
    {
        iResult = iMuteUnlockResult;
    }    
    return iResult;
}

static int iZephyrFlash_Write(const struct device *pstDevice,
                             off_t tOffset,
                             const void *pData,
                             size_t uiLength)
{
    int iResult = iValidate_FlashRange(pstDevice, tOffset, uiLength);
    if(iResult < 0)
        return iResult;
    if(uiLength == 0U)
        return 0;
    if(pData == NULL)
        return -EINVAL;
    if(pstDevice->data == NULL)
        return -ENODEV;
    
    const sT_ZephyrFlashConfig_t *pstConfig = (const sT_ZephyrFlashConfig_t *)pstDevice->config;
    if(pstConfig->uiWriteBlockSize == 0U)
        return -EINVAL;
    if(((uint64_t)tOffset % pstConfig->uiWriteBlockSize) != 0U ||
       (uiLength % pstConfig->uiWriteBlockSize) != 0U)
    {
        return -EINVAL;
    }
    
    sT_ZephyrFlashData_t *pstData = (sT_ZephyrFlashData_t *)pstDevice->data;

    iResult = k_mutex_lock(&pstData->stFlashMutex, K_FOREVER);
    if(iResult < 0)
        return iResult;
    
    iResult = iWrite_DataToFlash((uint32_t)tOffset, (const uint8_t*)pData, uiLength);

    int iMuteUnlockResult = k_mutex_unlock(&pstData->stFlashMutex);
    if(iResult == 0 && iMuteUnlockResult < 0)
    {
        iResult = iMuteUnlockResult;
    }    
    return iResult;
}

static int iZephyrFlash_Erase(const struct device *pstDevice,
                             off_t tOffset,
                             size_t uiLength)
{
    int iResult = iValidate_FlashRange(pstDevice, tOffset, uiLength);
    if(iResult < 0)
        return iResult;
    if(uiLength == 0U)
    {
        return 0;
    }
    if(pstDevice->data == NULL)
    {
        return -ENODEV;
    }
    
    const sT_ZephyrFlashConfig_t *pstConfig = (const sT_ZephyrFlashConfig_t *)pstDevice->config;

    const size_t uiEraseBlockSize = pstConfig->uiEraseBlockSize;
    if(uiEraseBlockSize == 0U)
        return -EINVAL;
    
    if(((uint64_t)tOffset % uiEraseBlockSize) != 0U ||
       (uiLength % uiEraseBlockSize) != 0U)
    {
        return -EINVAL;
    }
    
    sT_ZephyrFlashData_t *pstData = (sT_ZephyrFlashData_t *)pstDevice->data;

    iResult = k_mutex_lock(&pstData->stFlashMutex, K_FOREVER);
    if(iResult < 0)
        return iResult;
    
    uint32_t uiCurrentAddress = (uint32_t)tOffset;
    size_t uiRemainingLength = uiLength;

    while(uiRemainingLength > 0U)
    {
        iResult = iErase_Flash_4KB(uiCurrentAddress);
        if(iResult < 0)
        {
            break;
        }

        uiRemainingLength -= uiEraseBlockSize;
        uiCurrentAddress += (uint32_t)uiEraseBlockSize;
    }

    int iUnlockResult = k_mutex_unlock(&pstData->stFlashMutex);
    if(iResult == 0 && iUnlockResult < 0)
    {
        iResult = iUnlockResult;
    }

    return iResult;
}

static const struct flash_parameters *pstGet_ZephyrFlashParameters(const struct device *pstDevice)
{
    return (const struct flash_parameters *)&stTFlashParameters;
}

static int iGet_ZephyrFlashSize(const struct device *pstDevice, uint64_t *puiSize)
{
    *puiSize = EXT_FLASH_TOTAL_SIZE_BYTEs;
    return 0;
}

#if defined(CONFIG_FLASH_PAGE_LAYOUT)
static void vGet_ZephyrFlashPageLayout(const struct device *pstDevice,                                        
                                       const struct flash_pages_layout **ppstLayout,
                                       size_t *puiLayoutCount)
{
    ARG_UNUSED(pstDevice);

    *ppstLayout = &stTFlashPageLayout;
    *puiLayoutCount = 1U;    
}
#endif

static int iValidate_FlashRange(const struct device *pstDevice, off_t tOffset, size_t uiLength)
{
    if(pstDevice == NULL || pstDevice->config == NULL)
    {
        return -EINVAL;
    }

    const sT_ZephyrFlashConfig_t *pstConfig = (const sT_ZephyrFlashConfig_t *)pstDevice->config;
    if(tOffset < 0)
    {
        return -EINVAL;
    }

    uint64_t uiOffset = (uint64_t)tOffset;
    uint64_t uiDeviceSize = (uint64_t)pstConfig->uiDeviceSize;
    uint64_t uiRequestLength = (uint64_t)uiLength;

    if(uiOffset > uiDeviceSize)
    {
        return -EINVAL;
    }
    if(uiRequestLength > (uiDeviceSize - uiOffset))
    {
        return -EINVAL;
    }

    return 0;
}

DEVICE_DT_DEFINE( EXT_FLASH_NODE,
                  iZephyrFlash_Init,
                  NULL,
                  &stTZephyrFlashData,
                  &stTZephyrFlashConfig,
                  POST_KERNEL,
                  CONFIG_KERNEL_INIT_PRIORITY_DEVICE,
                  &stTZephyrFlashAPI
                );
